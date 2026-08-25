// main/src/tank_controller.cpp
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "tank_controller.hpp"

static const char* TAG = "TankController";

TankController::TankController(
    time_manager::ITimeManager& time_mgr,
    const SunSchedule& sun_schedule,
    const TankPolicyConfig& config)
    : time_mgr_(time_mgr)
    , sun_schedule_(sun_schedule)
    , config_(config)
{
}

void TankController::on_tank_report(uint16_t permille, bool float_switch_full, bool backup_mode, time_t timestamp)
{
    current_permille_ = permille;
    float_switch_full_ = float_switch_full;
    backup_mode_ = backup_mode;
    last_report_time_ = timestamp;

    evaluate_policy();
}

void TankController::on_manual_fill_request()
{
    ESP_LOGI(TAG, "Manual fill requested via button/UI");
    manual_fill_requested_ = true;
    evaluate_policy();
}

void TankController::on_pump_status_update(farm::LoadState state, farm::PowerSource source, uint32_t runtime_s)
{
    (void)source;
    (void)runtime_s;
    pump_running_ = (state == farm::LoadState::RUNNING);

    evaluate_policy();
}

uint32_t TankController::calculate_estimated_duration_s() const
{
    if (backup_mode_) {
        return config_.backup_fill_duration_s;
    }

    if (current_permille_ >= config_.target_fill_permille || float_switch_full_) {
        return 0;
    }

    if (config_.fill_rate_permille_per_min_x10 == 0) {
        return 0;
    }

    uint16_t deficit = config_.target_fill_permille - current_permille_;
    // Pure integer arithmetic: (deficit * 600) / rate_x10
    // Example: 240‰ deficit at 48 (4.8 ‰/min) => (240 * 600) / 48 = 3000s
    return (static_cast<uint32_t>(deficit) * 600) / config_.fill_rate_permille_per_min_x10;
}

void TankController::evaluate_policy()
{
    // In backup mode, satisfaction is governed exclusively by the float switch.
    // In normal mode, satisfaction is governed exclusively by ultrasonic permille.
    bool is_full = backup_mode_ ? float_switch_full_ : (current_permille_ >= config_.target_fill_permille);

    if (is_full) {
        if (state_ != TankState::IDLE) {
            ESP_LOGI(TAG, "Target level satisfied (mode=%s, permille=%u, float=%d). State -> IDLE",
                     backup_mode_ ? "BACKUP" : "NORMAL", current_permille_, float_switch_full_);
        }
        state_ = TankState::IDLE;
        manual_fill_requested_ = false;
        return;
    }

    // 2. Check if fill is needed
    bool needs_fill = manual_fill_requested_ || (backup_mode_ && !float_switch_full_) ||
                      (!backup_mode_ && current_permille_ < config_.target_fill_permille);

    if (!needs_fill) {
        state_ = TankState::IDLE;
        return;
    }

    // 3. Update FSM State
    if (pump_running_) {
        state_ = TankState::FILLING;
    }
    else {
        state_ = TankState::FILL_REQUESTED;
    }
}

LoadIntent TankController::get_current_intent() const
{
    LoadIntent intent;
    intent.load_index = LoadIndex::PUMP;
    intent.max_hold_duration_s = 0; // Pump cannot be shed when filling is requested

    if (state_ == TankState::IDLE) {
        intent.desired_state = LoadDesiredState::OFF;
        intent.urgency = LoadUrgency::SHEDDABLE;
        intent.source_preference = SourcePreference::SOLAR_ONLY;
        intent.estimated_on_duration_s = 0;
        return intent;
    }

    intent.desired_state = LoadDesiredState::ON;
    intent.estimated_on_duration_s = calculate_estimated_duration_s();

    // If manual request: force fill with solar preferred or any if night
    time_t now = time_mgr_.get_timestamp_sec();
    bool is_day = sun_schedule_.is_daytime(now);

    if (manual_fill_requested_) {
        intent.urgency = LoadUrgency::NORMAL;
        intent.source_preference = is_day ? SourcePreference::SOLAR_PREFERRED : SourcePreference::ANY;
        return intent;
    }

    // Backup mode policy (based purely on day/night and float switch)
    if (backup_mode_) {
        intent.urgency = LoadUrgency::NORMAL;
        intent.source_preference = is_day ? SourcePreference::SOLAR_PREFERRED : SourcePreference::ANY;
        return intent;
    }

    // Normal policy based on permille and solar clock
    float hours_to_sunset = sun_schedule_.hours_until_sunset(now);
    bool in_pre_sunset_window =
        (is_day && hours_to_sunset > 0.0f && hours_to_sunset <= config_.pre_sunset_window_hours);

    if (current_permille_ < config_.critical_min_permille) {
        // Critical level: emergency fill on any source immediately
        intent.urgency = LoadUrgency::CRITICAL;
        intent.source_preference = SourcePreference::ANY;
    }
    else if (!is_day) {
        // Nighttime and not critical: do not fill, wait for sun
        intent.desired_state = LoadDesiredState::OFF;
        intent.urgency = LoadUrgency::SHEDDABLE;
        intent.estimated_on_duration_s = 0;
    }
    else if (in_pre_sunset_window && current_permille_ < config_.opportunistic_min_permille) {
        // Pre-sunset acceleration window: fill to top before night
        intent.urgency = LoadUrgency::NORMAL;
        intent.source_preference = SourcePreference::SOLAR_PREFERRED;
    }
    else if (current_permille_ < config_.normal_min_permille) {
        // Low daylight level: normal fill
        intent.urgency = LoadUrgency::NORMAL;
        intent.source_preference = SourcePreference::SOLAR_PREFERRED;
    }
    else if (current_permille_ < config_.opportunistic_min_permille) {
        // Moderate level (500-899‰): opportunistic solar top-off
        intent.urgency = LoadUrgency::OPPORTUNISTIC;
        intent.source_preference = SourcePreference::SOLAR_PREFERRED;
    }
    else {
        // High level (>=900‰): top-off strictly on solar surplus
        intent.urgency = LoadUrgency::OPPORTUNISTIC;
        intent.source_preference = SourcePreference::SOLAR_ONLY;
    }

    return intent;
}
