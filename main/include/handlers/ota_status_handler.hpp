// main/include/handlers/ota_status_handler.hpp
#pragma once

#include "espnow_manager.hpp"
#include "farm_protocol_types.hpp"
#include "interfaces/i_payload_handler.hpp"

namespace hub {

class OtaStatusHandler : public IPayloadHandler
{
public:
    explicit OtaStatusHandler(espnow::IEspNowManager& espnow);

    void handle_payload(const espnow::AppMessage& msg) override;

private:
    espnow::IEspNowManager& espnow_;
};

} // namespace hub
