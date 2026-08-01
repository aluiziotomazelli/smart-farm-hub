// main/include/hub_app.hpp
#pragma once

#include "i_espnow_manager.hpp"
#include "farm_protocol_types.hpp"
#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_hub_nvs.hpp"
#include "interfaces/i_wifi_manager.hpp"
#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_time_manager.hpp"

#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_system.hpp"
#include "interfaces/i_hal_sleep.hpp"

#include "core_types.hpp"
#include "hub_stats.hpp"
#include "driver/gpio.h"

struct HubAppConfig
{
    gpio_num_t boot_button_gpio = GPIO_NUM_0;
    gpio_num_t i2c_sda_gpio = GPIO_NUM_8;
    gpio_num_t i2c_scl_gpio = GPIO_NUM_9;
};

class HubApp
{
public:
    HubApp(
        INvsCore& core_storage,
        IHubNvs& hub_storage,
        espnow::IEspNowManager& espnow,
        wifi_manager::IWiFiManager& wifi,
        IOtaManager& ota_manager,
        time_manager::ITimeManager& time_manager,
        idf_hals::IHalFreertos& rtos,
        idf_hals::ISystemHAL& hal_system,
        idf_hals::ISleepHAL& hal_sleep);

    esp_err_t init(const HubAppConfig& config = {});
    void run();

protected:
    CoreStorage core_;
    HubStats stats_;

    bool session_healthy_ = true;
    bool pending_firmware_verify_ = false;
    bool pending_core_commit_ = false;
    bool pending_tank_commit_ = false;

private:
    INvsCore& core_storage_;
    IHubNvs& hub_storage_;
    espnow::IEspNowManager& espnow_;
    wifi_manager::IWiFiManager& wifi_;
    IOtaManager& ota_manager_;
    time_manager::ITimeManager& time_manager_;
    idf_hals::IHalFreertos& hal_rtos_;
    idf_hals::ISystemHAL& hal_system_;
    idf_hals::ISleepHAL& hal_sleep_;

    QueueHandle_t rx_queue_ = nullptr;
    HubAppConfig config_;

    esp_err_t init_hub_storage();
    esp_err_t init_core_storage();
    esp_err_t init_wifi();
    esp_err_t connect_wifi_with_retry(uint8_t max_attempts = 2);
    esp_err_t init_espnow();
    esp_err_t init_ota_manager();
    esp_err_t init_boot_button();
    esp_err_t init_display();

    void log_boot_summary();
    void check_firmware();

    void handle_message(const espnow::AppMessage& msg);
    void handle_boot_button();
    void dispatch_pending_command(farm::NodeId node_id);

    // Pending command helpers
    bool set_pending_command(farm::NodeId node_id, espnow::CommandType cmd);
    bool has_pending_command(farm::NodeId node_id, espnow::CommandType& out_cmd);
    void clear_pending_command(farm::NodeId node_id);
};
