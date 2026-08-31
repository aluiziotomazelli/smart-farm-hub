// main/include/handlers/water_tank_handler.hpp
#pragma once

#include "farm_protocol_types.hpp"
#include "interfaces/i_command_manager.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_load_control_task.hpp"
#include "interfaces/i_node_registry.hpp"
#include "interfaces/i_payload_handler.hpp"
#include "tank_controller.hpp"
#include "ui_snapshot.hpp"

namespace hub {

/**
 * @class WaterTankHandler
 * @brief Handles incoming WATER_LEVEL_REPORT payloads, updating telemetry, TankController, and LoadControlTask.
 */
class WaterTankHandler : public IPayloadHandler
{
public:
    WaterTankHandler(
        UiSnapshot& ui_snapshot,
        INodeRegistry& node_registry,
        TankController& tank_controller,
        ILoadControlTask& load_control_task,
        ICommandManager& command_mgr,
        idf_hals::ITimerHAL& timer);

    ~WaterTankHandler() override = default;

    /** @copydoc IPayloadHandler::handle_payload */
    espnow::AckStatus handle_payload(const espnow::AppMessage& msg) override;

    /** @copydoc IPayloadHandler::post_handle_payload */
    void post_handle_payload(const espnow::AppMessage& msg) override;

private:
    void post_handle_fill_request(const espnow::AppMessage& msg);
    void post_handle_water_report(const espnow::AppMessage& msg);

    UiSnapshot& ui_snapshot_;
    INodeRegistry& node_registry_;
    TankController& tank_controller_;
    ILoadControlTask& load_control_task_;
    ICommandManager& command_mgr_;
    idf_hals::ITimerHAL& timer_;
};

} // namespace hub
