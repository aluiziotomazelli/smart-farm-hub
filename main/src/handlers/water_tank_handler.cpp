// main/src/handlers/water_tank_handler.cpp
#include "handlers/water_tank_handler.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "WaterTankHandler";

namespace hub {

WaterTankHandler::WaterTankHandler(
    SystemState& state,
    SemaphoreHandle_t state_mutex,
    CommandManager& command_mgr,
    idf_hals::ITimerHAL& timer,
    idf_hals::IHalFreertos& rtos)
    : state_(state)
    , state_mutex_(state_mutex)
    , command_mgr_(command_mgr)
    , timer_(timer)
    , rtos_(rtos)
{
}

#pragma pack(push, 1)
struct LegacyWaterLevelReport
{
    uint16_t level_permille;
    float distance_cm;
    uint16_t battery_mv;
    uint8_t battery_percent;
    farm::BatteryState battery_state;
    farm::SensorStatus status;
    bool float_switch_is_full;
    bool backup_mode_active;
    uint64_t unix_time;
};
#pragma pack(pop)

espnow::AckStatus WaterTankHandler::handle_payload(const espnow::AppMessage& msg)
{
    if (msg.payload_len == 0) {
        return espnow::AckStatus::ERROR_INVALID_DATA;
    }

    farm::WaterLevelReport report{};

    if (msg.payload_len >= sizeof(farm::WaterLevelReport)) {
        memcpy(&report, msg.payload, sizeof(farm::WaterLevelReport));
    }
    else if (msg.payload_len >= sizeof(LegacyWaterLevelReport)) {
        LegacyWaterLevelReport legacy{};
        memcpy(&legacy, msg.payload, sizeof(LegacyWaterLevelReport));

        report.power_profile = farm::PowerProfile::DEEP_SLEEP;
        report.level_permille = legacy.level_permille;
        report.distance_cm = legacy.distance_cm;
        report.battery_mv = legacy.battery_mv;
        report.battery_percent = legacy.battery_percent;
        report.battery_state = legacy.battery_state;
        report.status = legacy.status;
        report.float_switch_is_full = legacy.float_switch_is_full;
        report.backup_mode_active = legacy.backup_mode_active;
        report.unix_time = legacy.unix_time;

        ESP_LOGI(TAG, "Legacy report (21 bytes) received from node 0x%02X - parsed with default DEEP_SLEEP", msg.sender_id);
    }
    else {
        ESP_LOGE(TAG, "Invalid payload length %zu received from node 0x%02X", msg.payload_len, msg.sender_id);
        return espnow::AckStatus::ERROR_INVALID_DATA;
    }

    if (rtos_.semaphore_take(state_mutex_, portMAX_DELAY) == pdTRUE) {
        state_.last_water_update_ts = timer_.get_time_us() / 1000;
        state_.water_level_permille = report.level_permille;
        state_.water_distance_cm = report.distance_cm;
        state_.water_battery_mv = report.battery_mv;
        state_.water_battery_percent = report.battery_percent;
        state_.water_battery_state = report.battery_state;
        state_.water_sensor_status = report.status;
        state_.water_float_switch_full = report.float_switch_is_full;
        state_.water_backup_mode = report.backup_mode_active;
        state_.water_node_unix_time = report.unix_time;
        state_.set_node_power_profile(static_cast<farm::NodeId>(msg.sender_id), report.power_profile);

        rtos_.semaphore_give(state_mutex_);
    }

    command_mgr_.get_stats().set_node_power_profile(static_cast<farm::NodeId>(msg.sender_id), report.power_profile);

    ESP_LOGI(
        TAG,
        "[WATER TANK] Level: %u\u2030 | Distance: %.1f cm | Battery: %u mV (%u%%) "
        "| Float: %s | Backup: %s | RSSI: %d dBm | Time: %llu ms",
        report.level_permille,
        report.distance_cm,
        report.battery_mv,
        report.battery_percent,
        report.float_switch_is_full ? "FULL" : "EMPTY",
        report.backup_mode_active ? "ON" : "OFF",
        msg.rssi,
        static_cast<unsigned long long>(report.unix_time));

    return espnow::AckStatus::OK;
}

void WaterTankHandler::post_handle_payload(const espnow::AppMessage& msg)
{
    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    uint64_t unix_time = 0;

    if (msg.payload_len >= sizeof(farm::WaterLevelReport)) {
        const auto* report = reinterpret_cast<const farm::WaterLevelReport*>(msg.payload);
        unix_time = report->unix_time;
    }
    else if (msg.payload_len >= sizeof(LegacyWaterLevelReport)) {
        const auto* legacy = reinterpret_cast<const LegacyWaterLevelReport*>(msg.payload);
        unix_time = legacy->unix_time;
    }

    command_mgr_.process_node_wake(node_id, unix_time);
}

} // namespace hub
