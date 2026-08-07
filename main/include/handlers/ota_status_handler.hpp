// main/include/handlers/ota_status_handler.hpp
#pragma once

#include "farm_protocol_types.hpp"
#include "interfaces/i_payload_handler.hpp"

namespace hub {

class OtaStatusHandler : public IPayloadHandler
{
public:
    OtaStatusHandler() = default;

    espnow::AckStatus handle_payload(const espnow::AppMessage& msg) override;
};

} // namespace hub
