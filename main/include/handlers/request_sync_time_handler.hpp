// main/include/handlers/request_sync_time_handler.hpp
#pragma once

#include "espnow_manager.hpp"
#include "time_manager.hpp"
#include "farm_protocol_types.hpp"
#include "interfaces/i_payload_handler.hpp"

namespace hub {

class RequestSyncTimeHandler : public IPayloadHandler
{
public:
    explicit RequestSyncTimeHandler(espnow::IEspNowManager& espnow, time_manager::ITimeManager& time);

    void handle_payload(const espnow::AppMessage& msg) override;

private:
    espnow::IEspNowManager& espnow_;
    time_manager::ITimeManager& time_;
};

} // namespace hub
