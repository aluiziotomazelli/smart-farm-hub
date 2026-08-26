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

bool CommandManager::send_command(farm::NodeId target_node, espnow::CommandType cmd, bool requires_ack)
{
    farm::PowerProfile profile = node_registry_.get_power_profile(target_node);

    if (profile == farm::PowerProfile::ALWAYS_ON) {
        ESP_LOGI(
            TAG,
            "Node 0x%02X is ALWAYS_ON, attempting instant command 0x%02X dispatch...",
            static_cast<uint8_t>(target_node),
            static_cast<uint8_t>(cmd));

        esp_err_t err = dispatch_single_command(target_node, cmd, requires_ack);
        if (err == ESP_OK) {
            commands_sent_++;
            return true;
        }

        ESP_LOGW(
            TAG,
            "Instant dispatch to 0x%02X failed (%s), fallback enqueuing in RAM FIFO...",
            static_cast<uint8_t>(target_node),
            esp_err_to_name(err));
    }

    return push_pending_command(target_node, cmd, requires_ack);
}

bool CommandManager::push_pending_command(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack)
{
    if (node_id == farm::NodeId::UNKNOWN || node_id == farm::NodeId::BROADCAST) {
        return false;
    }

    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (pending_queue_.full()) {
        ESP_LOGW(
            TAG,
            "Failed to arm command 0x%02X for node 0x%02X (RAM FIFO queue full)",
            static_cast<uint8_t>(cmd),
            static_cast<uint8_t>(node_id));
        return false;
    }

    pending_queue_.push(PendingCommandItem{node_id, cmd, requires_ack});
    ESP_LOGI(
        TAG,
        "Armed command 0x%02X (requires_ack=%d) in RAM FIFO for node 0x%02X (queue depth: %zu)",
        static_cast<uint8_t>(cmd),
        requires_ack,
        static_cast<uint8_t>(node_id),
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

        push_pending_command(node_id, static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME), false);
    }
}

void CommandManager::dispatch_pending_commands(farm::NodeId node_id)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    size_t count = pending_queue_.size();
    for (size_t i = 0; i < count; ++i) {
        PendingCommandItem item = pending_queue_.front();
        pending_queue_.pop();

        if (item.node_id == node_id) {
            esp_err_t err = dispatch_single_command(node_id, item.command, item.requires_ack);
            if (err == ESP_OK) {
                commands_sent_++;
                ESP_LOGI(
                    TAG,
                    "Command 0x%02X successfully dispatched from RAM FIFO to node 0x%02X",
                    static_cast<uint8_t>(item.command),
                    static_cast<uint8_t>(node_id));
            } else {
                ESP_LOGE(
                    TAG,
                    "Failed to dispatch RAM FIFO command 0x%02X to node 0x%02X: %s",
                    static_cast<uint8_t>(item.command),
                    static_cast<uint8_t>(node_id),
                    esp_err_to_name(err));
            }
        } else {
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

    ESP_LOGI(
        TAG,
        "Broadcasting TANK_LEVEL_UPDATE: tank=%u, level=%u permille, backup=%d, full=%d to PUMP_CONTROL",
        tank_id,
        level_permille,
        backup_mode_active,
        float_switch_is_full);

    return espnow_.send_data(
        farm::NodeId::PUMP_CONTROL,
        farm::PayloadType::TANK_LEVEL_UPDATE,
        &update_pkt,
        sizeof(update_pkt),
        false);
}

bool CommandManager::dispatch_decision(const LoadControlDecision& decision)
{
    if (decision.should_be_on) {
        farm::LoadOnCommand cmd{
            .circuit_id = decision.circuit_id,
            .power_source = decision.target_source,
            .watchdog_timeout_s = static_cast<uint16_t>(decision.watchdog_s),
        };

        ESP_LOGI(
            TAG,
            "Dispatching LOAD_ON to node 0x%02X (circuit=%u, source=%u, watchdog=%u s)",
            static_cast<uint8_t>(decision.node_id),
            decision.circuit_id,
            static_cast<uint8_t>(decision.target_source),
            static_cast<unsigned>(decision.watchdog_s));

        esp_err_t err = espnow_.send_command(
            decision.node_id,
            static_cast<espnow::CommandType>(farm::CommandType::LOAD_ON),
            &cmd,
            sizeof(cmd),
            true);

        if (err == ESP_OK) {
            commands_sent_++;
            return true;
        }
        ESP_LOGE(
            TAG,
            "Failed to send LOAD_ON to node 0x%02X: %s",
            static_cast<uint8_t>(decision.node_id),
            esp_err_to_name(err));
        return false;
    } else {
        farm::LoadOffCommand cmd{
            .circuit_id = decision.circuit_id,
        };

        ESP_LOGI(
            TAG,
            "Dispatching LOAD_OFF to node 0x%02X (circuit=%u)",
            static_cast<uint8_t>(decision.node_id),
            decision.circuit_id);

        esp_err_t err = espnow_.send_command(
            decision.node_id,
            static_cast<espnow::CommandType>(farm::CommandType::LOAD_OFF),
            &cmd,
            sizeof(cmd),
            true);

        if (err == ESP_OK) {
            commands_sent_++;
            return true;
        }
        ESP_LOGE(
            TAG,
            "Failed to send LOAD_OFF to node 0x%02X: %s",
            static_cast<uint8_t>(decision.node_id),
            esp_err_to_name(err));
        return false;
    }
}

esp_err_t CommandManager::dispatch_single_command(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack)
{
    if (cmd == static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME)) {
        farm::TimeSyncCommand sync_pkt{};
        if (create_sync_time_packet(sync_pkt) != ESP_OK) {
            return ESP_ERR_INVALID_STATE;
        }

        return espnow_.send_command(node_id, cmd, &sync_pkt, sizeof(sync_pkt), requires_ack);
    }

    return espnow_.send_command(node_id, cmd, nullptr, 0, requires_ack);
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
