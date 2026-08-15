// main/src/handlers/solar_sensor_handler.cpp
#include "handlers/solar_sensor_handler.hpp"

#include <algorithm>
#include <cstring>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "SolarSensorHandler";

namespace hub {

SolarSensorHandler::SolarSensorHandler(
    SystemState& state,
    SemaphoreHandle_t state_mutex,
    CommandManager& command_mgr,
    idf_hals::ITimerHAL& timer,
    idf_hals::IHalFreertos& rtos,
    EventGroupHandle_t solar_events,
    solar::SolarSystemConfig solar_cfg)
    : state_(state)
    , state_mutex_(state_mutex)
    , command_mgr_(command_mgr)
    , timer_(timer)
    , rtos_(rtos)
    , solar_events_(solar_events)
    , solar_cfg_(solar_cfg)
{
}

espnow::AckStatus SolarSensorHandler::handle_payload(const espnow::AppMessage& msg)
{
    if (msg.payload_len < sizeof(farm::SolarSensorReport)) {
        ESP_LOGE(TAG, "Invalid payload length %zu received from node 0x%02X", msg.payload_len, msg.sender_id);
        return espnow::AckStatus::ERROR_INVALID_DATA;
    }

    farm::SolarSensorReport report{};
    memcpy(&report, msg.payload, sizeof(farm::SolarSensorReport));

    const int64_t now_ms = timer_.get_time_us() / 1000;

    // ── Pure PV physical power estimation (decoupled domain logic) ──
    const solar::SolarPowerEstimate est = solar::estimate(report, solar_cfg_);

    // ── Time integration for estimated daily energy generated (Wh) ──
    float delta_h = 0.0f;
    if (last_update_ts_ms_ > 0 && !report.is_night_mode) {
        delta_h = static_cast<float>(now_ms - last_update_ts_ms_) / 3600000.0f;
        // Ignore intervals larger than 5 minutes (e.g. node reboot or reconnect)
        if (delta_h > (5.0f / 60.0f)) {
            delta_h = 0.0f;
        }
    }
    last_update_ts_ms_ = now_ms;

    // ── Update SystemState under mutex ──
    if (rtos_.semaphore_take(state_mutex_, portMAX_DELAY) == pdTRUE) {
        state_.last_solar_update_ts = now_ms;

        // Raw telemetry (filtered at node level)
        state_.solar_isc_current_ma = report.isc_current_ma;
        state_.solar_irradiance_wm2 = report.irradiance_wm2;
        state_.solar_panel_temp_c = report.panel_temp_c;
        state_.solar_battery_mv = report.battery_mv;
        state_.solar_battery_percent = report.battery_percent;
        state_.solar_battery_state = report.battery_state;
        state_.solar_sensor_status = report.status;
        state_.solar_max_current_ma = report.max_current_ma;
        state_.solar_daily_yield_mah = report.daily_yield_mah;
        state_.solar_is_night_mode = report.is_night_mode;
        state_.solar_node_unix_time = report.unix_time;

        // Derived Hub telemetry
        state_.solar_power_w_instant = est.power_w_instant;
        state_.solar_power_w_avg = est.power_w_instant;
        state_.solar_daily_yield_wh_hub += static_cast<float>(est.power_w_instant) * delta_h;

        // Daily min/max temperature tracking
        if (report.panel_temp_c != INT16_MIN) {
            if (report.panel_temp_c > state_.solar_panel_temp_max_c) {
                state_.solar_panel_temp_max_c = report.panel_temp_c;
            }
            if (report.panel_temp_c < state_.solar_panel_temp_min_c) {
                state_.solar_panel_temp_min_c = report.panel_temp_c;
            }
        }

        state_.set_node_power_profile(static_cast<farm::NodeId>(msg.sender_id), report.power_profile);

        rtos_.semaphore_give(state_mutex_);
    }

    command_mgr_.get_stats().set_node_power_profile(static_cast<farm::NodeId>(msg.sender_id), report.power_profile);

    // ── Reactive notification for Load Control Task (outside critical section) ──
    if (solar_events_ != nullptr) {
        rtos_.event_group_set_bits(solar_events_, 1 << 0);
    }

    ESP_LOGI(
        TAG,
        "[SOLAR] Irr: %u W/m² | Isc: %u mA | Pwr: %u W | "
        "Temp: %.1f°C | Yield: %lu mAh / %.1f Wh(hub) | Night: %s | Bat: %u mV (%u%%)",
        report.irradiance_wm2,
        report.isc_current_ma,
        est.power_w_instant,
        (report.panel_temp_c != INT16_MIN) ? (report.panel_temp_c / 10.0f) : 0.0f,
        static_cast<unsigned long>(report.daily_yield_mah),
        state_.solar_daily_yield_wh_hub,
        report.is_night_mode ? "YES" : "NO",
        report.battery_mv,
        report.battery_percent);

    return espnow::AckStatus::OK;
}

void SolarSensorHandler::post_handle_payload(const espnow::AppMessage& msg)
{
    if (msg.payload_len < sizeof(farm::SolarSensorReport)) {
        return;
    }

    const auto* report = reinterpret_cast<const farm::SolarSensorReport*>(msg.payload);
    command_mgr_.process_node_wake(static_cast<farm::NodeId>(msg.sender_id), report->unix_time);
}

} // namespace hub
