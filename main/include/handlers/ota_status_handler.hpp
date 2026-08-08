// main/include/handlers/ota_status_handler.hpp
#pragma once

#include <functional>
#include "farm_protocol_types.hpp"
#include "interfaces/i_payload_handler.hpp"

namespace hub {

class OtaStatusHandler : public IPayloadHandler
{
public:
    using OnNodeVersionCb = std::function<void(uint8_t node_id, uint8_t major, uint8_t minor, uint8_t patch)>;

    explicit OtaStatusHandler(OnNodeVersionCb on_version_cb = nullptr);

    espnow::AckStatus handle_payload(const espnow::AppMessage& msg) override;

private:
    OnNodeVersionCb on_version_cb_;
};

} // namespace hub
