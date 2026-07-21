// main/include/hub_app.hpp
#pragma once

#include "i_espnow_manager.hpp"
#include "farm_protocol_types.hpp"
#include "hub_nvs.hpp"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

class HubApp {
public:
    HubApp(espnow::IEspNowManager &espnow,
           HubNvs                 &nvs,
           gpio_num_t              boot_button_gpio,
           QueueHandle_t           app_rx_queue);

    /** @brief Main application loop — never returns. */
    void run();

private:
    espnow::IEspNowManager &espnow_;
    HubNvs                 &nvs_;
    gpio_num_t              boot_button_gpio_;
    QueueHandle_t           app_rx_queue_;

    void handle_message(const espnow::AppMessage &msg);
    void handle_boot_button();
    void dispatch_pending_command(farm::NodeId node_id);

    // Pending command helpers
    bool set_pending_command(farm::NodeId node_id, espnow::CommandType cmd);
    bool has_pending_command(farm::NodeId node_id, espnow::CommandType &out_cmd);
    void clear_pending_command(farm::NodeId node_id);
};
