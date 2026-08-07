// main/src/command_manager.cpp
#include "command_manager.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "CommandManager";

namespace hub {

CommandManager::CommandManager(
    espnow::IEspNowManager& espnow,
    HubStats& stats,
    IHubNvs& hub_storage,
    time_manager::ITimeManager& time_manager,
    SystemState& state,
    SemaphoreHandle_t state_mutex,
    idf_hals::IHalFreertos& rtos)
    : espnow_(espnow)
    , stats_(stats)
    , hub_storage_(hub_storage)
    , time_manager_(time_manager)
    , state_(state)
    , state_mutex_(state_mutex)
    , rtos_(rtos)
{
}

bool CommandManager::send_command(farm::NodeId target_node, espnow::CommandType cmd, bool requires_ack)
{
    farm::PowerProfile profile = farm::PowerProfile::DEEP_SLEEP;

    if (state_mutex_ != nullptr && rtos_.semaphore_take(state_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        profile = state_.node_power_profile[static_cast<uint8_t>(target_node)];
        rtos_.semaphore_give(state_mutex_);
    }

    if (profile == farm::PowerProfile::ALWAYS_ON) {
        ESP_LOGI(
            TAG,
            "Node 0x%02X is ALWAYS_ON, attempting instant command 0x%02X dispatch...",
            static_cast<uint8_t>(target_node),
            static_cast<uint8_t>(cmd));

        esp_err_t err = dispatch_single_command(target_node, cmd, requires_ack);
        if (err == ESP_OK) {
            stats_.commands_sent++;
            return true;
        }

        ESP_LOGW(
            TAG,
            "Instant dispatch to 0x%02X failed (%s), fallback enqueuing in FIFO...",
            static_cast<uint8_t>(target_node),
            esp_err_to_name(err));
    }

    return push_pending_command(target_node, cmd, requires_ack);
}

bool CommandManager::push_pending_command(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack)
{
    bool success = stats_.push_pending(node_id, cmd, requires_ack);
    if (success) {
        hub_storage_.save_app_data(stats_);
        ESP_LOGI(
            TAG,
            "Armed command 0x%02X (requires_ack=%d) in FIFO for node 0x%02X",
            static_cast<uint8_t>(cmd),
            requires_ack,
            static_cast<uint8_t>(node_id));
    }
    else {
        ESP_LOGW(
            TAG,
            "Failed to arm command 0x%02X for node 0x%02X (FIFO queue full or capacity limit)",
            static_cast<uint8_t>(cmd),
            static_cast<uint8_t>(node_id));
    }
    return success;
}

void CommandManager::process_node_wake(farm::NodeId node_id, uint64_t node_unix_time_ms)
{
    stats_.messages_received++;
    check_and_arm_time_sync(node_id, node_unix_time_ms);
    dispatch_pending_commands(node_id);
    hub_storage_.save_app_data(stats_);
}

void CommandManager::check_and_arm_time_sync(farm::NodeId node_id, uint64_t node_unix_time_ms)
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

    if (node_unix_time_ms == 0 || drift > MAX_DRIFT_MS) {
        ESP_LOGW(
            TAG,
            "Node 0x%02X clock out of sync (node: %llu ms, hub: %llu ms, drift: %lld ms). Arming SYNC_TIME...",
            static_cast<uint8_t>(node_id),
            static_cast<unsigned long long>(node_unix_time_ms),
            static_cast<unsigned long long>(hub_time_ms),
            static_cast<long long>(drift));

        push_pending_command(node_id, static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME), false);
    }
}

void CommandManager::dispatch_pending_commands(farm::NodeId node_id)
{
    PendingNodeCommand pending;
    while (stats_.pop_pending(node_id, pending)) {
        esp_err_t err = dispatch_single_command(node_id, pending.command, pending.requires_ack);
        if (err == ESP_OK) {
            stats_.commands_sent++;
            ESP_LOGI(
                TAG,
                "Command 0x%02X successfully dispatched from FIFO to node 0x%02X",
                static_cast<uint8_t>(pending.command),
                static_cast<uint8_t>(node_id));
        }
        else {
            ESP_LOGE(
                TAG,
                "Failed to dispatch FIFO command 0x%02X to node 0x%02X: %s",
                static_cast<uint8_t>(pending.command),
                static_cast<uint8_t>(node_id),
                esp_err_to_name(err));
        }
    }
}

esp_err_t CommandManager::dispatch_single_command(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack)
{
    if (cmd == static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME)) {
        if (!time_manager_.is_synchronized()) {
            ESP_LOGW(
                TAG,
                "Cannot dispatch SYNC_TIME to 0x%02X: Hub is not time-synchronized",
                static_cast<uint8_t>(node_id));
            return ESP_ERR_INVALID_STATE;
        }

        auto packet = time_manager_.create_time_packet();
        farm::TimeSyncCommand sync_cmd;
        sync_cmd.timestamp_ms = packet.timestamp_ms;
        sync_cmd.tz_offset_min = packet.tz_offset_min;
        sync_cmd.sync_source = static_cast<uint8_t>(packet.sync_source);
        sync_cmd.flags = packet.flags;

        return espnow_.send_command(node_id, cmd, &sync_cmd, sizeof(sync_cmd), requires_ack);
    }

    return espnow_.send_command(node_id, cmd, nullptr, 0, requires_ack);
}

} // namespace hub
