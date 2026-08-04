// main/src/handlers/request_sync_time_handler.cpp
#include "handlers/request_sync_time_handler.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "RequestSyncTimeHandler";

namespace hub {

RequestSyncTimeHandler::RequestSyncTimeHandler(espnow::IEspNowManager& espnow, time_manager::ITimeManager& time)
    : espnow_(espnow)
    , time_(time)
{
}

void RequestSyncTimeHandler::handle_payload(const espnow::AppMessage& msg)
{
    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    ESP_LOGI(TAG, "[TIME SYNC REQUEST] Node: 0x%02X", static_cast<uint8_t>(node_id));

    if (!time_.is_synchronized()) {
        ESP_LOGW(TAG, "Cannot respond to time sync request: Hub is not synchronized");
        return;
    }

    auto packet = time_.create_time_packet();
    espnow_.send_command(
        node_id, static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME), &packet, sizeof(packet), false);
}

} // namespace hub
