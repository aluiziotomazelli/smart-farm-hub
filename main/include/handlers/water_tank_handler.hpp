// main/include/handlers/water_tank_handler.hpp
#pragma once

#include "espnow_manager.hpp"
#include "farm_protocol_types.hpp"
#include "hub_stats.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hub_nvs.hpp"
#include "interfaces/i_payload_handler.hpp"
#include "interfaces/i_time_manager.hpp"
#include "system_state.hpp"

namespace hub {

class WaterTankHandler : public IPayloadHandler
{
public:
    WaterTankHandler(
        SystemState& state,
        SemaphoreHandle_t state_mutex,
        HubStats& stats,
        IHubNvs& hub_storage,
        espnow::IEspNowManager& espnow,
        time_manager::ITimeManager& time_manager,
        idf_hals::IHalFreertos& rtos);

    void handle_payload(const espnow::AppMessage& msg) override;

private:
    SystemState& state_;
    SemaphoreHandle_t state_mutex_;
    HubStats& stats_;
    IHubNvs& hub_storage_;
    espnow::IEspNowManager& espnow_;
    time_manager::ITimeManager& time_manager_;
    idf_hals::IHalFreertos& rtos_;

    void check_and_send_time_sync(farm::NodeId node_id, uint64_t node_unix_time_ms);
    void dispatch_pending_command(farm::NodeId node_id);
    bool has_pending_command(farm::NodeId node_id, espnow::CommandType& out_cmd, bool& out_requires_ack);
    void clear_pending_command(farm::NodeId node_id);
};

} // namespace hub
