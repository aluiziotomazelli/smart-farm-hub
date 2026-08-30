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
    manual_stop_cooldown_until_ = 0; // Clear cooldown on intentional manual user request
    evaluate_policy();
}

void TankController::on_pump_status_update(farm::LoadState state, farm::PowerSource source, uint32_t runtime_s)
{
    (void)source;
    (void)runtime_s;
    bool was_running = pump_running_;
    pump_running_ = (state == farm::LoadState::RUNNING);

    // If pump stopped prematurely by operator (clean transition to IDLE while not full)
    bool is_full = backup_mode_ ? float_switch_full_ : (current_permille_ >= config_.target_fill_permille);
    if (was_running && !pump_running_) {
        manual_fill_requested_ = false;
        if (state == farm::LoadState::IDLE && !is_full) {
            time_t now = time_mgr_.get_timestamp_sec();
            manual_stop_cooldown_until_ = now + config_.manual_stop_cooldown_s;
            ESP_LOGI(
                TAG,
                "Operator manual stop detected: pausing automatic fill for %lu s",
                static_cast<unsigned long>(config_.manual_stop_cooldown_s));
        }
    }

    evaluate_policy();
}

bool TankController::is_manual_stop_in_cooldown() const
{
    if (manual_stop_cooldown_until_ == 0) {
        return false;
    }
    time_t now = time_mgr_.get_timestamp_sec();
    return (now < manual_stop_cooldown_until_);
}

uint16_t TankController::get_target_permille_for_tier(TankFillTier tier) const
{
    switch (tier) {
    case TankFillTier::CRITICAL_RECOVERY:
        return config_.normal_min_permille;
    case TankFillTier::NORMAL_FILL:
        return config_.opportunistic_min_permille;
    case TankFillTier::OPPORTUNISTIC:
        return config_.surplus_min_permille;
    case TankFillTier::SOLAR_SURPLUS:
    case TankFillTier::MANUAL_REQUEST:
        return config_.target_fill_permille;
    case TankFillTier::NONE:
    default:
        return current_permille_;
    }
}

uint32_t TankController::calculate_estimated_duration_s() const
{
    if (backup_mode_) {
        return float_switch_full_ ? 0 : config_.backup_fill_duration_s;
    }

    if (current_tier_ == TankFillTier::NONE) {
        return 0;
    }

    uint16_t target = get_target_permille_for_tier(current_tier_);
    if (current_permille_ >= target || config_.fill_rate_permille_per_min_x10 == 0) {
        return 0;
    }

    uint16_t deficit = target - current_permille_;
    // Pure integer arithmetic: (deficit * 600) / rate_x10
    uint32_t duration_s = (static_cast<uint32_t>(deficit) * 600) / config_.fill_rate_permille_per_min_x10;
    return (duration_s < config_.min_fill_duration_s) ? config_.min_fill_duration_s : duration_s;
}

void TankController::evaluate_policy()
{
    // 1. Check physical full limit
    bool is_full = backup_mode_ ? float_switch_full_ : (current_permille_ >= config_.target_fill_permille);

    if (is_full) {
        if (state_ != TankState::IDLE) {
            ESP_LOGI(
                TAG,
                "Target level satisfied (mode=%s, permille=%u, float=%d). State -> IDLE",
                backup_mode_ ? "BACKUP" : "NORMAL",
                current_permille_,
                float_switch_full_);
        }
        state_ = TankState::IDLE;
        current_tier_ = TankFillTier::NONE;
        manual_fill_requested_ = false;
        return;
    }

    // 2. Determine demand tier
    time_t now = time_mgr_.get_timestamp_sec();
    bool is_day = sun_schedule_.is_daytime(now);
    float hours_to_sunset = sun_schedule_.hours_until_sunset(now);
    bool in_pre_sunset_window =
        (is_day && hours_to_sunset > 0.0f && hours_to_sunset <= config_.pre_sunset_window_hours);
    bool in_cooldown = is_manual_stop_in_cooldown();

    if (manual_fill_requested_) {
        current_tier_ = TankFillTier::MANUAL_REQUEST;
    }
    else if (backup_mode_) {
        current_tier_ = float_switch_full_ ? TankFillTier::NONE : TankFillTier::NORMAL_FILL;
    }
    else if (current_permille_ < config_.critical_min_permille) {
        // Critical level (<300‰): emergency refill on ANY source, overriding cooldown
        current_tier_ = TankFillTier::CRITICAL_RECOVERY;
    }
    else if (in_cooldown) {
        // Cooldown active after manual operator stop; suppress non-critical automatic start
        current_tier_ = TankFillTier::NONE;
    }
    else if (!is_day) {
        // Nighttime and not critical: do not start non-critical fill
        current_tier_ = TankFillTier::NONE;
    }
    else if (in_pre_sunset_window && current_permille_ < config_.opportunistic_min_permille) {
        // Pre-sunset window: escalate target for levels < 800‰ to top-off on SOLAR_PREFERRED
        current_tier_ = TankFillTier::OPPORTUNISTIC;
    }
    else if (current_permille_ < config_.normal_min_permille) {
        // < 500‰: Normal refill
        current_tier_ = TankFillTier::NORMAL_FILL;
    }
    else if (current_permille_ < config_.opportunistic_min_permille) {
        // 500..799‰: Opportunistic refill
        current_tier_ = TankFillTier::OPPORTUNISTIC;
    }
    else if (current_permille_ < config_.surplus_min_permille) {
        // 800..899‰: Pure solar surplus top-off (always SOLAR_ONLY)
        current_tier_ = TankFillTier::SOLAR_SURPLUS;
    }
    else {
        // >= 900‰: Satisfied / Hysteresis band
        current_tier_ = TankFillTier::NONE;
    }

    // Check if the determined tier is already satisfied by current level
    if (current_tier_ != TankFillTier::NONE && !backup_mode_) {
        uint16_t tier_target = get_target_permille_for_tier(current_tier_);
        if (current_permille_ >= tier_target) {
            current_tier_ = TankFillTier::NONE;
        }
    }

    // 3. Update FSM State
    if (current_tier_ == TankFillTier::NONE) {
        state_ = TankState::IDLE;
        return;
    }

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

    uint32_t duration_s = calculate_estimated_duration_s();

    if (state_ == TankState::IDLE || current_tier_ == TankFillTier::NONE || duration_s == 0) {
        intent.desired_state = LoadDesiredState::OFF;
        intent.urgency = LoadUrgency::SHEDDABLE;
        intent.source_preference = SourcePreference::SOLAR_ONLY;
        intent.estimated_on_duration_s = 0;
        return intent;
    }

    intent.desired_state = LoadDesiredState::ON;
    intent.estimated_on_duration_s = duration_s;

    time_t now = time_mgr_.get_timestamp_sec();
    bool is_day = sun_schedule_.is_daytime(now);

    switch (current_tier_) {
    case TankFillTier::CRITICAL_RECOVERY:
        intent.urgency = LoadUrgency::CRITICAL;
        intent.source_preference = SourcePreference::ANY;
        break;

    case TankFillTier::MANUAL_REQUEST:
        intent.urgency = LoadUrgency::NORMAL;
        intent.source_preference = is_day ? SourcePreference::SOLAR_PREFERRED : SourcePreference::ANY;
        break;

    case TankFillTier::NORMAL_FILL:
        intent.urgency = LoadUrgency::NORMAL;
        intent.source_preference = SourcePreference::SOLAR_PREFERRED;
        break;

    case TankFillTier::OPPORTUNISTIC:
        intent.urgency = LoadUrgency::OPPORTUNISTIC;
        intent.source_preference = SourcePreference::SOLAR_PREFERRED;
        break;

    case TankFillTier::SOLAR_SURPLUS:
        intent.urgency = LoadUrgency::OPPORTUNISTIC;
        intent.source_preference = SourcePreference::SOLAR_ONLY;
        break;

    case TankFillTier::NONE:
    default:
        intent.desired_state = LoadDesiredState::OFF;
        intent.urgency = LoadUrgency::SHEDDABLE;
        intent.source_preference = SourcePreference::SOLAR_ONLY;
        intent.estimated_on_duration_s = 0;
        break;
    }

    return intent;
}
