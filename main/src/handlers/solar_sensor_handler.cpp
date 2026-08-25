// main/src/handlers/solar_sensor_handler.cpp
#include <algorithm>
#include <cstdint>
#include <cstring>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "handlers/solar_sensor_handler.hpp"

static const char* TAG = "SolarSensorHandler";

namespace hub {

SolarSensorHandler::SolarSensorHandler(
    UiSnapshot& ui_snapshot,
    INodeRegistry& node_registry,
    ILoadControlTask& load_control_task,
    CommandManager& command_mgr,
    idf_hals::ITimerHAL& timer,
    solar::SolarSystemConfig solar_cfg)
    : ui_snapshot_(ui_snapshot)
    , node_registry_(node_registry)
    , load_control_task_(load_control_task)
    , command_mgr_(command_mgr)
    , timer_(timer)
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

    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    const int64_t now_ms = static_cast<int64_t>(timer_.get_time_us() / 1000);

    // 1. Pure PV physical power estimation (decoupled domain logic)
    const solar::SolarPowerEstimate est = solar::estimate(report, solar_cfg_);

    // 2. Time integration for estimated daily energy generated (Wh)
    float delta_h = 0.0f;
    if (last_update_ts_ms_ > 0 && !report.is_night_mode) {
        delta_h = static_cast<float>(now_ms - last_update_ts_ms_) / 3600000.0f;
        // Ignore intervals larger than 5 minutes (e.g. node reboot or long sleep)
        if (delta_h > (5.0f / 60.0f)) {
            delta_h = 0.0f;
        }
    }
    last_update_ts_ms_ = now_ms;
    daily_yield_wh_hub_ += static_cast<float>(est.power_w_instant) * delta_h;

    // 3. Update NodeRegistry power profile
    node_registry_.set_power_profile(node_id, report.power_profile);

    // 4. Update UiSnapshot with solar telemetry and estimation
    ui_snapshot_.update_solar(
        now_ms,
        report.isc_current_ma,
        report.irradiance_wm2,
        report.panel_temp_c,
        report.battery_mv,
        report.battery_percent,
        report.battery_state,
        report.status,
        report.max_current_ma,
        report.daily_yield_mah,
        report.is_night_mode,
        report.unix_time,
        est.power_w_instant,
        daily_yield_wh_hub_);

    // 5. Post reactive solar update to LoadControlTask
    SolarPowerUpdate solar_update{
        .power_w = est.power_w_instant,
        .irradiance_wm2 = report.irradiance_wm2,
        .is_night_mode = report.is_night_mode,
        .timestamp_ms = now_ms,
    };
    load_control_task_.post_solar_update(solar_update);

    ESP_LOGI(
        TAG,
        "[SOLAR] Irr: %u W/m² | Isc: %u mA | Pwr: %u W | "
        "Temp: %.1f°C | Yield: %lu mAh / %.1f Wh(hub) | Night: %s | Bat: %u mV (%u%%)",
        report.irradiance_wm2,
        report.isc_current_ma,
        est.power_w_instant,
        (report.panel_temp_c != INT16_MIN) ? (report.panel_temp_c / 10.0f) : 0.0f,
        static_cast<unsigned long>(report.daily_yield_mah),
        daily_yield_wh_hub_,
        report.is_night_mode ? "YES" : "NO",
        report.battery_mv,
        report.battery_percent);

    return espnow::AckStatus::OK;
}

void SolarSensorHandler::post_handle_payload(const espnow::AppMessage& msg)
{
    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    uint64_t unix_time = 0;

    if (msg.payload_len >= sizeof(farm::SolarSensorReport)) {
        const auto* report = reinterpret_cast<const farm::SolarSensorReport*>(msg.payload);
        unix_time = report->unix_time;
    }

    command_mgr_.process_node_wake(node_id, unix_time);
}

} // namespace hub
