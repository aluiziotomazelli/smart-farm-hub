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
     * @return espnow::AckStatus Status of processing (OK, ERROR_INVALID_DATA, etc.)
     */
    virtual espnow::AckStatus handle_payload(const espnow::AppMessage& msg) = 0;

    /**
     * @brief Post-ACK action hook executed after ACK confirmation is transmitted over RF.
     * @param msg De-serialized message containing sender_id, payload_type, RSSI, and raw bytes.
     */
    virtual void post_handle_payload(const espnow::AppMessage& msg) {}
};

} // namespace hub
