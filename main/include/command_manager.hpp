// main/include/command_manager.hpp
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include "etl/queue.h"

#include "espnow_types.hpp"
#include "farm_protocol_types.hpp"
#include "i_espnow_manager.hpp"
#include "interfaces/i_command_manager.hpp"
#include "interfaces/i_node_registry.hpp"
#include "interfaces/i_time_manager.hpp"

namespace hub {

/**
 * @brief Represents a pending command queued in RAM for a sleeping node.
 */
struct PendingCommandItem
{
    farm::NodeId node_id = farm::NodeId::UNKNOWN;
    espnow::CommandType command = espnow::CommandType::START_OTA;
    bool requires_ack = true;
};

/**
 * @class CommandManager
 * @brief Thread-safe implementation of ICommandManager.
 *
 * Dispatches commands immediately for ALWAYS_ON nodes or queues them in a RAM FIFO
 * for DEEP_SLEEP nodes until their next wake report. Implements ILoadActuatorDispatcher
 * to translate LoadControlEngine decisions into LoadOn/LoadOff network commands.
 */
class CommandManager : public ICommandManager
{
public:
    static constexpr size_t MAX_PENDING_QUEUE_SIZE = 16;

    CommandManager(
        espnow::IEspNowManager& espnow,
        hub::INodeRegistry& node_registry,
        time_manager::ITimeManager& time_manager);

    ~CommandManager() override = default;

    /** @copydoc ICommandManager::send_command */
    bool send_command(farm::NodeId target_node, espnow::CommandType cmd, bool requires_ack = true) override;

    /** @copydoc ICommandManager::process_node_wake */
    void process_node_wake(farm::NodeId node_id, uint64_t node_unix_time_ms) override;

    /** @copydoc ICommandManager::broadcast_tank_level */
    esp_err_t broadcast_tank_level(
        uint8_t tank_id,
        uint16_t level_permille,
        bool backup_mode_active,
        bool float_switch_is_full) override;

    /** @copydoc ILoadActuatorDispatcher::dispatch_decision */
    bool dispatch_decision(const LoadControlDecision& decision) override;

    /** @copydoc ICommandManager::get_messages_received */
    uint32_t get_messages_received() const override { return messages_received_.load(); }

    /** @copydoc ICommandManager::get_commands_sent */
    uint32_t get_commands_sent() const override { return commands_sent_.load(); }

    /**
     * @brief Pushes a command into the pending RAM FIFO for a node.
     */
    bool push_pending_command(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack = true);

    /**
     * @brief Drains and dispatches all pending FIFO commands for a specific node.
     */
    void dispatch_pending_commands(farm::NodeId node_id);

    /**
     * @brief Checks clock drift between Hub and Node, arming SYNC_TIME if drift exceeds threshold.
     */
    void check_and_arm_time_sync(farm::NodeId node_id, uint64_t node_unix_time_ms);

    /**
     * @brief Dispatches a single command over ESP-NOW transport.
     */
    esp_err_t dispatch_single_command(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack);

private:
    espnow::IEspNowManager& espnow_;
    hub::INodeRegistry& node_registry_;
    time_manager::ITimeManager& time_manager_;

    std::atomic<uint32_t> messages_received_{0};
    std::atomic<uint32_t> commands_sent_{0};

    mutable std::mutex queue_mutex_;
    etl::queue<PendingCommandItem, MAX_PENDING_QUEUE_SIZE> pending_queue_{};

    esp_err_t create_sync_time_packet(farm::TimeSyncCommand& out_sync_cmd) const;
};

} // namespace hub
