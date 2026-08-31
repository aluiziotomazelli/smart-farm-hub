// main/src/command_manager.cpp
#include <cstdint>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "command_manager.hpp"

static const char* TAG = "CommandManager";

namespace hub {

CommandManager::CommandManager(
    espnow::IEspNowManager& espnow,
    hub::INodeRegistry& node_registry,
    time_manager::ITimeManager& time_manager)
    : espnow_(espnow)
    , node_registry_(node_registry)
    , time_manager_(time_manager)
{
}

bool CommandManager::send_command(const CommandItem& item)
{
    if (item.node_id == farm::NodeId::UNKNOWN || item.node_id == farm::NodeId::BROADCAST) {
        return false;
    }

    farm::PowerProfile profile = node_registry_.get_power_profile(item.node_id);

    if (profile == farm::PowerProfile::ALWAYS_ON) {
        ESP_LOGI(
            TAG,
            "Node 0x%02X is ALWAYS_ON, attempting instant command 0x%02X dispatch (len=%zu)...",
            static_cast<uint8_t>(item.node_id),
            static_cast<uint8_t>(item.command),
            item.payload_len);

        esp_err_t err = dispatch_single_command(item.node_id, item);
        if (err == ESP_OK) {
            commands_sent_++;
            return true;
        }

        ESP_LOGW(
            TAG,
            "Instant dispatch to 0x%02X failed (%s), fallback enqueuing in RAM FIFO...",
            static_cast<uint8_t>(item.node_id),
            esp_err_to_name(err));
    }

    return push_pending_command(item);
}

bool CommandManager::push_pending_command(const CommandItem& item)
{
    if (item.node_id == farm::NodeId::UNKNOWN || item.node_id == farm::NodeId::BROADCAST) {
        return false;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);

    // 1. Deduplication: update existing pending command for this node if already queued
    size_t count = pending_queue_.size();
    for (size_t i = 0; i < count; ++i) {
        CommandItem existing = pending_queue_.front();
        pending_queue_.pop();

        if (existing.node_id == item.node_id && existing.command == item.command) {
            pending_queue_.push(item);
            for (size_t j = i + 1; j < count; ++j) {
                CommandItem rest = pending_queue_.front();
                pending_queue_.pop();
                pending_queue_.push(rest);
            }
            ESP_LOGD(
                TAG,
                "Updated existing pending command 0x%02X in RAM FIFO for node 0x%02X",
                static_cast<uint8_t>(item.command),
                static_cast<uint8_t>(item.node_id));
            return true;
        }
        pending_queue_.push(existing);
    }

    // 2. Queue full: discard oldest item
    if (pending_queue_.full()) {
        ESP_LOGW(
            TAG,
            "RAM FIFO queue full, discarding oldest item to enqueue 0x%02X for node 0x%02X",
            static_cast<uint8_t>(item.command),
            static_cast<uint8_t>(item.node_id));
        pending_queue_.pop();
    }

    pending_queue_.push(item);
    ESP_LOGI(
        TAG,
        "Armed command 0x%02X (requires_ack=%d, len=%zu) in RAM FIFO for node 0x%02X (queue depth: %zu)",
        static_cast<uint8_t>(item.command),
        item.requires_ack,
        item.payload_len,
        static_cast<uint8_t>(item.node_id),
        pending_queue_.size());

    return true;
}

void CommandManager::process_node_wake(farm::NodeId node_id, uint64_t node_unix_time_ms)
{
    messages_received_++;
    check_and_arm_time_sync(node_id, node_unix_time_ms);
    dispatch_pending_commands(node_id);
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

        push_pending_command(CommandItem{node_id, static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME), false});
    }
}

void CommandManager::dispatch_pending_commands(farm::NodeId node_id)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    size_t count = pending_queue_.size();
    for (size_t i = 0; i < count; ++i) {
        CommandItem item = pending_queue_.front();
        pending_queue_.pop();

        if (item.node_id == node_id) {
            esp_err_t err = dispatch_single_command(node_id, item);
            if (err == ESP_OK) {
                commands_sent_++;
                ESP_LOGI(
                    TAG,
                    "Command 0x%02X successfully dispatched from RAM FIFO to node 0x%02X",
                    static_cast<uint8_t>(item.command),
                    static_cast<uint8_t>(node_id));
            }
            else {
                ESP_LOGE(
                    TAG,
                    "Failed to dispatch RAM FIFO command 0x%02X to node 0x%02X: %s",
                    static_cast<uint8_t>(item.command),
                    static_cast<uint8_t>(node_id),
                    esp_err_to_name(err));
            }
        }
        else {
            // Keep items for other nodes in the queue
            pending_queue_.push(item);
        }
    }
}

esp_err_t CommandManager::broadcast_tank_level(
    uint8_t tank_id,
    uint16_t level_permille,
    bool backup_mode_active,
    bool float_switch_is_full)
{
    farm::TankLevelUpdate update_pkt{};
    update_pkt.tank_id = tank_id;
    update_pkt.level_permille = level_permille;
    update_pkt.backup_mode_active = backup_mode_active;
    update_pkt.float_switch_is_full = float_switch_is_full;

    ESP_LOGI(TAG, "Broadcasting TANK_LEVEL_UPDATE");

    return espnow_.send_data(
        farm::NodeId::PUMP_CONTROL, farm::PayloadType::TANK_LEVEL_UPDATE, &update_pkt, sizeof(update_pkt), false);
}

bool CommandManager::dispatch_decision(const LoadControlDecision& decision)
{
    farm::NodeId target_node = decision.node_id;
    if (target_node == farm::NodeId::UNKNOWN || static_cast<uint8_t>(target_node) == 0) {
        if (decision.load_index == LoadIndex::PUMP) {
            target_node = farm::NodeId::PUMP_CONTROL;
        } else {
            ESP_LOGW(
                TAG,
                "Cannot dispatch decision: Unknown target node for load %u",
                static_cast<unsigned>(decision.load_index));
            return false;
        }
    }

    if (decision.should_be_on) {
        farm::LoadOnCommand cmd{
            .circuit_id = decision.circuit_id,
            .power_source = decision.target_source,
            .watchdog_timeout_s = static_cast<uint16_t>(decision.watchdog_s),
        };

        return send_command(CommandItem{
            target_node,
            static_cast<espnow::CommandType>(farm::CommandType::LOAD_ON),
            true,
            &cmd,
            sizeof(cmd)});
    }
    else {
        farm::LoadOffCommand cmd{
            .circuit_id = decision.circuit_id,
        };

        return send_command(CommandItem{
            target_node,
            static_cast<espnow::CommandType>(farm::CommandType::LOAD_OFF),
            true,
            &cmd,
            sizeof(cmd)});
    }
}

esp_err_t CommandManager::dispatch_single_command(farm::NodeId node_id, const CommandItem& item)
{
    if (item.command == static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME)) {
        farm::TimeSyncCommand sync_pkt{};
        if (create_sync_time_packet(sync_pkt) != ESP_OK) {
            return ESP_ERR_INVALID_STATE;
        }

        return espnow_.send_command(node_id, item.command, &sync_pkt, sizeof(sync_pkt), item.requires_ack);
    }

    if (item.payload_len > 0) {
        return espnow_.send_command(node_id, item.command, item.payload, item.payload_len, item.requires_ack);
    }

    return espnow_.send_command(node_id, item.command, nullptr, 0, item.requires_ack);
}

esp_err_t CommandManager::create_sync_time_packet(farm::TimeSyncCommand& out_sync_cmd) const
{
    if (!time_manager_.is_synchronized()) {
        return ESP_ERR_INVALID_STATE;
    }

    auto packet = time_manager_.create_time_packet();
    out_sync_cmd.timestamp_ms = packet.timestamp_ms;
    out_sync_cmd.tz_offset_min = packet.tz_offset_min;
    out_sync_cmd.sync_source = static_cast<uint8_t>(packet.sync_source);
    out_sync_cmd.flags = packet.flags;

    return ESP_OK;
}

} // namespace hub
