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

espnow::AckStatus WaterTankHandler::handle_payload(const espnow::AppMessage& msg)
{
    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    const auto* report = reinterpret_cast<const farm::WaterLevelReport*>(msg.payload);

    if (report == nullptr || msg.payload_len < sizeof(farm::WaterLevelReport)) {
        ESP_LOGE(TAG, "Invalid payload length received from node 0x%02X", msg.sender_id);
        return espnow::AckStatus::ERROR_INVALID_DATA;
    }

    if (rtos_.semaphore_take(state_mutex_, portMAX_DELAY) == pdTRUE) {
        state_.last_water_update_ts = timer_.get_time_us() / 1000;
        state_.water_level_permille = report->level_permille;
        state_.water_distance_cm = report->distance_cm;
        state_.water_battery_mv = report->battery_mv;
        state_.water_battery_percent = report->battery_percent;
        state_.water_battery_state = report->battery_state;
        state_.water_sensor_status = report->status;
        state_.water_float_switch_full = report->float_switch_is_full;
        state_.water_backup_mode = report->backup_mode_active;
        state_.water_node_unix_time = report->unix_time;
        state_.node_power_profile[msg.sender_id] = report->power_profile;

        rtos_.semaphore_give(state_mutex_);
    }

    ESP_LOGI(
        TAG,
        "[WATER TANK] Level: %u\u2030 | Distance: %.1f cm | Battery: %u mV (%u%%) "
        "| Float: %s | Backup: %s | RSSI: %d dBm | Time: %llu ms",
        report->level_permille,
        report->distance_cm,
        report->battery_mv,
        report->battery_percent,
        report->float_switch_is_full ? "FULL" : "EMPTY",
        report->backup_mode_active ? "ON" : "OFF",
        msg.rssi,
        static_cast<unsigned long long>(report->unix_time));

    command_mgr_.process_node_wake(node_id, report->unix_time);
    return espnow::AckStatus::OK;
}

} // namespace hub
