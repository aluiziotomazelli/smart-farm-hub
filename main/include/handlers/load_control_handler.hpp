// main/include/handlers/load_control_handler.hpp
#pragma once

#include "farm_protocol_types.hpp"
#include "interfaces/i_command_manager.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_load_control_task.hpp"
#include "interfaces/i_node_registry.hpp"
#include "interfaces/i_payload_handler.hpp"
#include "load_control_types.hpp"
#include "tank_controller.hpp"

namespace hub {

/**
 * @class LoadControlHandler
 * @brief Handles incoming LOAD_CONTROL_STATUS telemetry messages from actuator nodes.
 *
 * Ingests actuator status reports, updates NodeRegistry, and forwards LoadStatusUpdate
 * directly to LoadControlTask and TankController.
 */
class LoadControlHandler : public IPayloadHandler
{
public:
    LoadControlHandler(
        INodeRegistry& node_registry,
        TankController& tank_controller,
        ILoadControlTask& load_control_task,
        ICommandManager& command_mgr,
        idf_hals::ITimerHAL& timer);

    ~LoadControlHandler() override = default;

    /** @copydoc IPayloadHandler::handle_payload */
    espnow::AckStatus handle_payload(const espnow::AppMessage& msg) override;

    /** @copydoc IPayloadHandler::post_handle_payload */
    void post_handle_payload(const espnow::AppMessage& msg) override;

private:
    INodeRegistry& node_registry_;
    TankController& tank_controller_;
    ILoadControlTask& load_control_task_;
    ICommandManager& command_mgr_;
    idf_hals::ITimerHAL& timer_;
};

} // namespace hub
