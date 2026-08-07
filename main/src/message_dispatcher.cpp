// main/src/message_dispatcher.cpp
#include "message_dispatcher.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "MessageDispatcher";

namespace hub {

MessageDispatcher::MessageDispatcher(
    QueueHandle_t rx_queue, espnow::IEspNowManager& espnow, idf_hals::IHalFreertos& rtos)
    : rx_queue_(rx_queue)
    , espnow_(espnow)
    , rtos_(rtos)
{
}

MessageDispatcher::~MessageDispatcher()
{
    stop();
}

esp_err_t MessageDispatcher::register_handler(farm::PayloadType payload_type, IPayloadHandler* handler)
{
    if (handler == nullptr) {
        ESP_LOGE(TAG, "Cannot register null handler for payload 0x%02X", static_cast<uint8_t>(payload_type));
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t type_key = static_cast<uint8_t>(payload_type);
    handlers_[type_key] = handler;
    ESP_LOGI(TAG, "Registered payload handler for 0x%02X", type_key);
    return ESP_OK;
}

esp_err_t MessageDispatcher::start()
{
    if (running_) {
        ESP_LOGW(TAG, "MessageDispatcher is already running");
        return ESP_OK;
    }

    if (rx_queue_ == nullptr) {
        ESP_LOGE(TAG, "rx_queue is null, cannot start MessageDispatcher");
        return ESP_ERR_INVALID_STATE;
    }

    running_ = true;
    BaseType_t ret = rtos_.task_create(
        task_entry, "msg_dispatcher", tasks::DISPATCHER_STACK_SIZE, this, tasks::DISPATCHER_PRIORITY, &task_handle_);

    if (ret != pdPASS) {
        running_ = false;
        ESP_LOGE(TAG, "Failed to create msg_dispatcher task");
        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG, "MessageDispatcher task started (Priority: %lu)", static_cast<unsigned long>(tasks::DISPATCHER_PRIORITY));
    return ESP_OK;
}

esp_err_t MessageDispatcher::stop()
{
    if (!running_) {
        return ESP_OK;
    }

    running_ = false;
    if (task_handle_ != nullptr) {
        rtos_.task_delete(task_handle_);
        task_handle_ = nullptr;
    }
    return ESP_OK;
}

void MessageDispatcher::task_entry(void* arg)
{
    auto* self = static_cast<MessageDispatcher*>(arg);
    if (self) {
        self->dispatch_loop();
    }
}

void MessageDispatcher::dispatch_loop()
{
    espnow::AppMessage msg;
    while (running_) {
        if (rtos_.queue_receive(rx_queue_, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            uint8_t type_key = msg.payload_type;
            auto it = handlers_.find(type_key);
            if (it != handlers_.end() && it->second != nullptr) {
                espnow::AckStatus status = it->second->handle_payload(msg);
                if (msg.requires_ack) {
                    espnow_.confirm_reception(msg.sender_id, msg.sequence_number, status);
                }
                if (status == espnow::AckStatus::OK) {
                    it->second->post_handle_payload(msg);
                }
            }
            else {
                ESP_LOGW(TAG, "Unhandled payload 0x%02X from node 0x%02X", msg.payload_type, msg.sender_id);
                if (msg.requires_ack) {
                    espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::ERROR_INVALID_DATA);
                }
            }
        }
    }
}

} // namespace hub
