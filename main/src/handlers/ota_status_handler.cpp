// main/src/handlers/ota_status_handler.cpp
#include "handlers/ota_status_handler.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "OtaStatusHandler";

namespace hub {

OtaStatusHandler::OtaStatusHandler(espnow::IEspNowManager& espnow)
    : espnow_(espnow)
{
}

void OtaStatusHandler::handle_payload(const espnow::AppMessage& msg)
{
    const auto* report = reinterpret_cast<const farm::OtaStatusReport*>(msg.payload);
    ESP_LOGI(
        TAG,
        "Node: 0x%02X | Result: %u | Error: 0x%02X | FW Version: %u.%u.%u",
        msg.sender_id,
        static_cast<uint8_t>(report->result),
        static_cast<uint8_t>(report->error_code),
        report->fw_major,
        report->fw_minor,
        report->fw_patch);

    if (msg.requires_ack) {
        espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
    }
}

} // namespace hub
