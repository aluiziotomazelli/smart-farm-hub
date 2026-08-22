// main/include/handlers/load_control_handler.hpp
#pragma once

#include "command_manager.hpp"
#include "farm_protocol_types.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_payload_handler.hpp"
#include "system_state.hpp"

namespace hub {

/**
 * @brief Handles incoming LOAD_CONTROL_STATUS telemetry messages from any actuator node.
 */
class LoadControlHandler : public IPayloadHandler
{
public:
    LoadControlHandler(
        SystemState& state,
        SemaphoreHandle_t state_mutex,
        CommandManager& command_mgr,
        idf_hals::ITimerHAL& timer,
        idf_hals::IHalFreertos& rtos,
        EventGroupHandle_t load_events = nullptr);

    espnow::AckStatus handle_payload(const espnow::AppMessage& msg) override;
    void post_handle_payload(const espnow::AppMessage& msg) override;

private:
    SystemState& state_;
    SemaphoreHandle_t state_mutex_;
    CommandManager& command_mgr_;
    idf_hals::ITimerHAL& timer_;
    idf_hals::IHalFreertos& rtos_;
    EventGroupHandle_t load_events_{nullptr};
};

} // namespace hub
