// main/include/interfaces/i_command_manager.hpp
#pragma once

#include <cstdint>
#include "esp_err.h"
#include "farm_protocol_types.hpp"
#include "interfaces/i_load_actuator_dispatcher.hpp"
#include "protocol_types.hpp" // espnow::CommandType

namespace hub {

/**
 * @interface ICommandManager
 * @brief Interface for commanding remote farm nodes (immediate dispatch or sleep-aware FIFO)
 *        and dispatching electrical load control decisions.
 */
class ICommandManager : public ILoadActuatorDispatcher
{
public:
    ~ICommandManager() override = default;

    /**
     * @brief Send a command to a target node.
     * Checks the node's PowerProfile in NodeRegistry:
     * - ALWAYS_ON: Sends immediately via ESP-NOW.
     * - DEEP_SLEEP / LOW_POWER: Enqueues into RAM FIFO queue for next wake cycle.
     * @param target_node Destination node identifier.
     * @param cmd Command type to dispatch.
     * @param requires_ack If true, requires transport acknowledgment.
     * @return true if dispatched or enqueued successfully, false if queue full.
     */
    virtual bool send_command(farm::NodeId target_node, espnow::CommandType cmd, bool requires_ack = true) = 0;

    /**
     * @brief Called when a node wakes up and sends telemetry.
     * Performs clock drift check, arms SYNC_TIME if needed, and drains pending FIFO commands.
     * @param node_id Remote node identifier.
     * @param node_unix_time_ms Remote node epoch time in ms.
     */
    virtual void process_node_wake(farm::NodeId node_id, uint64_t node_unix_time_ms) = 0;

    /**
     * @brief Dispatches a tank level update to registered actuator nodes (e.g. PumpController).
     * @param tank_id Tank identifier.
     * @param level_permille Water level in permille (0-1000).
     * @param backup_mode_active Backup float mode status.
     * @param float_switch_is_full Float switch physical state.
     * @return ESP_OK on success, or ESP-NOW error code.
     */
    virtual esp_err_t broadcast_tank_level(
        uint8_t tank_id,
        uint16_t level_permille,
        bool backup_mode_active,
        bool float_switch_is_full) = 0;

    /**
     * @brief Number of messages received during this session (in RAM).
     */
    virtual uint32_t get_messages_received() const = 0;

    /**
     * @brief Number of commands dispatched during this session (in RAM).
     */
    virtual uint32_t get_commands_sent() const = 0;
};

} // namespace hub
