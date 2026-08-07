// main/src/handlers/water_tank_handler.cpp
#include "handlers/water_tank_handler.hpp"

#include "esp_timer.h"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "WaterTankHandler";

namespace hub {

WaterTankHandler::WaterTankHandler(
    SystemState& state,
    SemaphoreHandle_t state_mutex,
    HubStats& stats,
    IHubNvs& hub_storage,
    espnow::IEspNowManager& espnow,
    time_manager::ITimeManager& time_manager,
    idf_hals::IHalFreertos& rtos)
    : state_(state)
    , state_mutex_(state_mutex)
    , stats_(stats)
    , hub_storage_(hub_storage)
    , espnow_(espnow)
    , time_manager_(time_manager)
    , rtos_(rtos)
{
    if (stats_.last_wt_level_permille > 0 || stats_.last_wt_battery_mv > 0) {
        if (state_mutex_ != nullptr && rtos_.semaphore_take(state_mutex_, portMAX_DELAY) == pdTRUE) {
            state_.water_level_permille = stats_.last_wt_level_permille;
            state_.water_distance_cm = stats_.last_wt_distance_cm;
            state_.water_battery_mv = stats_.last_wt_battery_mv;
            rtos_.semaphore_give(state_mutex_);
        }
    }
}

void WaterTankHandler::handle_payload(const espnow::AppMessage& msg)
{
    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    const auto* report = reinterpret_cast<const farm::WaterLevelReport*>(msg.payload);

    stats_.messages_received++;
    stats_.last_wt_level_permille = report->level_permille;
    stats_.last_wt_distance_cm = report->distance_cm;
    stats_.last_wt_battery_mv = report->battery_mv;

    if (rtos_.semaphore_take(state_mutex_, portMAX_DELAY) == pdTRUE) {
        state_.last_water_update_ts = esp_timer_get_time() / 1000;
        state_.water_level_permille = report->level_permille;
        state_.water_distance_cm = report->distance_cm;
        state_.water_battery_mv = report->battery_mv;
        state_.water_battery_percent = report->battery_percent;
        state_.water_battery_state = report->battery_state;
        state_.water_sensor_status = report->status;
        state_.water_float_switch_full = report->float_switch_is_full;
        state_.water_backup_mode = report->backup_mode_active;
        state_.water_node_unix_time = report->unix_time;

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

    if (msg.requires_ack) {
        espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
    }

    check_and_send_time_sync(node_id, report->unix_time);
    dispatch_pending_command(node_id);
    hub_storage_.save_app_data(stats_);
}

void WaterTankHandler::check_and_send_time_sync(farm::NodeId node_id, uint64_t node_unix_time_ms)
{
    if (!time_manager_.is_synchronized()) {
        return;
    }

    constexpr int64_t MAX_DRIFT_MS = 5000;

    uint64_t hub_time_ms = time_manager_.get_timestamp_ms();
    int64_t drift = static_cast<int64_t>(hub_time_ms) - static_cast<int64_t>(node_unix_time_ms);
    if (drift < 0) {
        drift = -drift;
    }

    // If node clock is un-synced (0) or drifted > MAX_DRIFT_MS, send SYNC_TIME
    if (node_unix_time_ms == 0 || drift > MAX_DRIFT_MS) {
        ESP_LOGW(
            TAG,
            "Node 0x%02X clock out of sync (node: %llu ms, hub: %llu ms, drift: %lld ms). Sending SYNC_TIME...",
            static_cast<uint8_t>(node_id),
            static_cast<unsigned long long>(node_unix_time_ms),
            static_cast<unsigned long long>(hub_time_ms),
            static_cast<long long>(drift));

        auto packet = time_manager_.create_time_packet();

        espnow_.send_command(
            node_id, static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME), &packet, sizeof(packet), false);
    }
}

void WaterTankHandler::dispatch_pending_command(farm::NodeId node_id)
{
    espnow::CommandType cmd;
    bool requires_ack = true;
    if (!has_pending_command(node_id, cmd, requires_ack)) {
        return;
    }

    esp_err_t err;
    if (cmd == static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME)) {
        if (!time_manager_.is_synchronized()) {
            ESP_LOGW(
                TAG,
                "Cannot dispatch SYNC_TIME to 0x%02X: Hub is not time-synchronized",
                static_cast<uint8_t>(node_id));
            return;
        }
        auto packet = time_manager_.create_time_packet();
        farm::TimeSyncCommand sync_cmd;
        sync_cmd.timestamp_ms = packet.timestamp_ms;
        sync_cmd.tz_offset_min = packet.tz_offset_min;
        sync_cmd.sync_source = static_cast<uint8_t>(packet.sync_source);
        sync_cmd.flags = packet.flags;

        err = espnow_.send_command(node_id, cmd, &sync_cmd, sizeof(sync_cmd), requires_ack);
    }
    else {
        err = espnow_.send_command(node_id, cmd, nullptr, 0, requires_ack);
    }

    if (err == ESP_OK) {
        clear_pending_command(node_id);
        stats_.commands_sent++;

        ESP_LOGW(
            TAG, "Command 0x%02X dispatched to node 0x%02X", static_cast<uint8_t>(cmd), static_cast<uint8_t>(node_id));
    }
    else {
        ESP_LOGE(
            TAG, "Failed to dispatch command to node 0x%02X: %s", static_cast<uint8_t>(node_id), esp_err_to_name(err));
    }
}

bool WaterTankHandler::has_pending_command(farm::NodeId node_id, espnow::CommandType& out_cmd, bool& out_requires_ack)
{
    for (const auto& entry : stats_.pending_cmds) {
        if (entry.active && entry.node_id == node_id) {
            out_cmd = entry.command;
            out_requires_ack = entry.requires_ack;
            return true;
        }
    }
    return false;
}

void WaterTankHandler::clear_pending_command(farm::NodeId node_id)
{
    for (auto& entry : stats_.pending_cmds) {
        if (entry.active && entry.node_id == node_id) {
            entry = {};
            hub_storage_.save_app_data(stats_);
            return;
        }
    }
}

} // namespace hub
