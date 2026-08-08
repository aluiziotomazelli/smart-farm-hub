// main/src/handlers/ota_status_handler.cpp
#include "handlers/ota_status_handler.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "OtaStatusHandler";

namespace hub {

OtaStatusHandler::OtaStatusHandler(OnNodeVersionCb on_version_cb)
    : on_version_cb_(on_version_cb)
{
}

espnow::AckStatus OtaStatusHandler::handle_payload(const espnow::AppMessage& msg)
{
    const auto* report = reinterpret_cast<const farm::OtaStatusReport*>(msg.payload);
    if (report == nullptr || msg.payload_len < sizeof(farm::OtaStatusReport)) {
        ESP_LOGE(TAG, "Invalid OTA status payload length from node 0x%02X", msg.sender_id);
        return espnow::AckStatus::ERROR_INVALID_DATA;
    }

    ESP_LOGI(
        TAG,
        "Node: 0x%02X | Result: %u | Error: 0x%02X | FW Version: %u.%u.%u",
        msg.sender_id,
        static_cast<uint8_t>(report->result),
        static_cast<uint8_t>(report->error_code),
        report->fw_major,
        report->fw_minor,
        report->fw_patch);

    if (on_version_cb_) {
        on_version_cb_(msg.sender_id, report->fw_major, report->fw_minor, report->fw_patch);
    }

    return espnow::AckStatus::OK;
}

} // namespace hub
