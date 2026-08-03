// main/include/message_dispatcher.hpp
#pragma once

#include <unordered_map>

#include "farm_protocol_types.hpp"
#include "hub_tasks.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_payload_handler.hpp"

namespace hub {

class MessageDispatcher
{
public:
    MessageDispatcher(QueueHandle_t rx_queue, idf_hals::IHalFreertos& rtos);
    ~MessageDispatcher();

    esp_err_t register_handler(farm::PayloadType payload_type, IPayloadHandler* handler);

    esp_err_t start();
    esp_err_t stop();

    TaskHandle_t get_task_handle() const { return task_handle_; }

private:
    static void task_entry(void* arg);
    void dispatch_loop();

    QueueHandle_t rx_queue_;
    idf_hals::IHalFreertos& rtos_;
    TaskHandle_t task_handle_{nullptr};
    bool running_{false};

    std::unordered_map<uint8_t, IPayloadHandler*> handlers_;
};

} // namespace hub
