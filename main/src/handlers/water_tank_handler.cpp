// main/src/handlers/water_tank_handler.cpp
#include <cstring>
#include <cstdint>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "handlers/water_tank_handler.hpp"

static const char* TAG = "WaterTankHandler";

namespace hub {

WaterTankHandler::WaterTankHandler(
    UiSnapshot& ui_snapshot,
    INodeRegistry& node_registry,
    TankController& tank_controller,
    ILoadControlTask& load_control_task,
    CommandManager& command_mgr,
    idf_hals::ITimerHAL& timer)
    : ui_snapshot_(ui_snapshot)
    , node_registry_(node_registry)
    , tank_controller_(tank_controller)
    , load_control_task_(load_control_task)
    , command_mgr_(command_mgr)
    , timer_(timer)
{
}

espnow::AckStatus WaterTankHandler::handle_payload(const espnow::AppMessage& msg)
{
    if (msg.payload_len < sizeof(farm::WaterLevelReport)) {
        ESP_LOGE(TAG, "Invalid payload length %zu received from node 0x%02X (expected >= %zu)",
                 msg.payload_len, msg.sender_id, sizeof(farm::WaterLevelReport));
        return espnow::AckStatus::ERROR_INVALID_DATA;
    }

    farm::WaterLevelReport report{};
    memcpy(&report, msg.payload, sizeof(farm::WaterLevelReport));

    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    int64_t now_ms = static_cast<int64_t>(timer_.get_time_us() / 1000);

    // 1. Update NodeRegistry power profile
    node_registry_.set_power_profile(node_id, report.power_profile);

    // 2. Update UiSnapshot with tank telemetry
    ui_snapshot_.update_water_tank(
        now_ms,
        report.level_permille,
        report.distance_cm,
        report.battery_mv,
        report.battery_percent,
        report.battery_state,
        report.status,
        report.float_switch_is_full,
        report.backup_mode_active,
        report.unix_time);

    // 4. Ingest telemetry into TankController domain logic
    time_t report_time = static_cast<time_t>(report.unix_time / 1000);
    tank_controller_.on_tank_report(
        report.level_permille,
        report.float_switch_is_full,
        report.backup_mode_active,
        report_time);

    // 5. Post arbitrated LoadIntent to LoadControlTask
    LoadIntent intent = tank_controller_.get_current_intent();
    load_control_task_.post_load_intent(intent);

    // 6. Forward level update to Pump Control node for local display
    command_mgr_.broadcast_tank_level(
        0, report.level_permille, report.backup_mode_active, report.float_switch_is_full);

    ESP_LOGI(
        TAG,
        "[WATER TANK] Level: %u\u2030 | Distance: %.1f cm | Battery: %u mV (%u%%) "
        "| Float: %s | Backup: %s | Intent State: %s | Urgency: %u | RSSI: %d dBm",
        report.level_permille,
        report.distance_cm,
        report.battery_mv,
        report.battery_percent,
        report.float_switch_is_full ? "FULL" : "EMPTY",
        report.backup_mode_active ? "ON" : "OFF",
        (intent.desired_state == LoadDesiredState::ON) ? "ON" : "OFF",
        static_cast<unsigned>(intent.urgency),
        msg.rssi);

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

    command_mgr_.process_node_wake(node_id, unix_time);
}

} // namespace hub
