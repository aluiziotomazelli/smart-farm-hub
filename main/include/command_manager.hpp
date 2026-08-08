// main/include/command_manager.hpp
#pragma once

#include <cstdint>

#include "core_types.hpp"
#include "farm_protocol_types.hpp"
#include "hub_stats.hpp"
#include "i_espnow_manager.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hub_nvs.hpp"
#include "interfaces/i_time_manager.hpp"
#include "system_state.hpp"

namespace hub {

class CommandManager
{
public:
    CommandManager(
        espnow::IEspNowManager& espnow,
        HubStats& stats,
        IHubNvs& hub_storage,
        time_manager::ITimeManager& time_manager,
        SystemState& state,
        SemaphoreHandle_t state_mutex,
        idf_hals::IHalFreertos& rtos);

    HubStats& get_stats() { return stats_; }

    /**
     * @brief Send a command to a target node.
     * Checks the node's PowerProfile:
     * - ALWAYS_ON: Sends immediately via ESP-NOW.
     * - DEEP_SLEEP / LOW_POWER: Enqueues into FIFO queue for next wake cycle.
     * @return true if sent or armed, false if failed/queue full.
     */
    bool send_command(farm::NodeId target_node, espnow::CommandType cmd, bool requires_ack = true);

    /**
     * @brief Called when a node wakes up and sends telemetry.
     * Increments message counter, performs clock drift check, and drains pending FIFO commands.
     */

    void process_node_wake(farm::NodeId node_id, uint64_t node_unix_time_ms);

    /**
     * @brief Manually push a command into the pending FIFO queue for a node.
     */
    bool push_pending_command(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack = true);

    /**
     * @brief Drain and dispatch all pending FIFO commands for a node.
     */
    void dispatch_pending_commands(farm::NodeId node_id);

    /**
     * @brief Check clock drift between Hub and Node, arming SYNC_TIME if needed.
     */
    void check_and_arm_time_sync(farm::NodeId node_id, uint64_t node_unix_time_ms);

    /**
     * @brief Dispatch a single command over ESP-NOW directly.
     */
    esp_err_t dispatch_single_command(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack);

private:
    espnow::IEspNowManager& espnow_;
    HubStats& stats_;
    IHubNvs& hub_storage_;
    time_manager::ITimeManager& time_manager_;
    SystemState& state_;
    SemaphoreHandle_t state_mutex_;
    idf_hals::IHalFreertos& rtos_;

    esp_err_t create_sync_time_packet(farm::TimeSyncCommand& out_sync_cmd) const;
};

} // namespace hub
