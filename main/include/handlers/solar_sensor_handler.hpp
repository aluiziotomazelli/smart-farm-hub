// main/include/handlers/solar_sensor_handler.hpp
#pragma once

#include "command_manager.hpp"
#include "farm_protocol_types.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_load_control_task.hpp"
#include "interfaces/i_node_registry.hpp"
#include "interfaces/i_payload_handler.hpp"
#include "solar_power_estimator.hpp"
#include "ui_snapshot.hpp"

namespace hub {

/**
 * @class SolarSensorHandler
 * @brief Handles incoming SOLAR_SENSOR_REPORT payloads, estimating PV power and notifying LCT and UI.
 */
class SolarSensorHandler : public IPayloadHandler
{
public:
    SolarSensorHandler(
        UiSnapshot& ui_snapshot,
        INodeRegistry& node_registry,
        ILoadControlTask& load_control_task,
        CommandManager& command_mgr,
        idf_hals::ITimerHAL& timer,
        solar::SolarSystemConfig solar_cfg = solar::SolarSystemConfig::from_hub_config());

    ~SolarSensorHandler() override = default;

    /** @copydoc IPayloadHandler::handle_payload */
    espnow::AckStatus handle_payload(const espnow::AppMessage& msg) override;

    /** @copydoc IPayloadHandler::post_handle_payload */
    void post_handle_payload(const espnow::AppMessage& msg) override;

private:
    UiSnapshot& ui_snapshot_;
    INodeRegistry& node_registry_;
    ILoadControlTask& load_control_task_;
    CommandManager& command_mgr_;
    idf_hals::ITimerHAL& timer_;
    solar::SolarSystemConfig solar_cfg_;

    int64_t last_update_ts_ms_{0};
    float daily_yield_wh_hub_{0.0f};
};

} // namespace hub
