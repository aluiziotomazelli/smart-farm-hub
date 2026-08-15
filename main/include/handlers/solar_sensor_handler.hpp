// main/include/handlers/solar_sensor_handler.hpp
#pragma once

#include "command_manager.hpp"
#include "farm_protocol_types.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_payload_handler.hpp"
#include "solar_power_estimator.hpp"
#include "system_state.hpp"

namespace hub {

class SolarSensorHandler : public IPayloadHandler
{
public:
    SolarSensorHandler(
        SystemState& state,
        SemaphoreHandle_t state_mutex,
        CommandManager& command_mgr,
        idf_hals::ITimerHAL& timer,
        idf_hals::IHalFreertos& rtos,
        EventGroupHandle_t solar_events = nullptr,
        solar::SolarSystemConfig solar_cfg = solar::SolarSystemConfig::from_hub_config());

    espnow::AckStatus handle_payload(const espnow::AppMessage& msg) override;
    void post_handle_payload(const espnow::AppMessage& msg) override;

private:
    SystemState& state_;
    SemaphoreHandle_t state_mutex_;
    CommandManager& command_mgr_;
    idf_hals::ITimerHAL& timer_;
    idf_hals::IHalFreertos& rtos_;
    EventGroupHandle_t solar_events_;
    solar::SolarSystemConfig solar_cfg_;
    int64_t last_update_ts_ms_{0};
};

} // namespace hub
