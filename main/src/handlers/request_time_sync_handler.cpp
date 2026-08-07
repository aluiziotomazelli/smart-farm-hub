// main/src/handlers/request_time_sync_handler.cpp
#include "handlers/request_time_sync_handler.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "RequestTimeSyncHandler";

namespace hub {

RequestTimeSyncHandler::RequestTimeSyncHandler(CommandManager& command_mgr)
    : command_mgr_(command_mgr)
{
}

espnow::AckStatus RequestTimeSyncHandler::handle_payload(const espnow::AppMessage& msg)
{
    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    ESP_LOGI(TAG, "Time sync requested by Node: 0x%02X", static_cast<uint8_t>(node_id));

    esp_err_t err = command_mgr_.dispatch_single_command(
        node_id, static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME), /*requires_ack=*/false);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to respond to time sync request for Node 0x%02X", static_cast<uint8_t>(node_id));
        return espnow::AckStatus::ERROR_PROCESSING;
    }

    return espnow::AckStatus::OK;
}

} // namespace hub
