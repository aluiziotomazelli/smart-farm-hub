// main/include/interfaces/i_payload_handler.hpp
#pragma once

#include "espnow_types.hpp"

namespace hub {

/**
 * @brief Interface for processing specific ESP-NOW payload types.
 */
class IPayloadHandler
{
public:
    virtual ~IPayloadHandler() = default;

    /**
     * @brief Process an incoming ESP-NOW message.
     * @param msg De-serialized message containing sender_id, payload_type, RSSI, and raw bytes.
     */
    virtual void handle_payload(const espnow::AppMessage& msg) = 0;
};

} // namespace hub
