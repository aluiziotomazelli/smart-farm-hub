// main/main.cpp
#include "esp_attr.h"
#include "esp_log.h"

#include "hal_freertos.hpp"
#include "hal_gpio.hpp"
#include "hal_i2c.hpp"
#include "hal_nvs.hpp"
#include "hal_pcnt.hpp"
#include "hal_sleep.hpp"
#include "hal_sntp.hpp"
#include "hal_system.hpp"
#include "hal_system_time.hpp"
#include "hal_timer.hpp"

#include "button.hpp"
#include "rotary_encoder.hpp"

#include "app_commands.hpp"
#include "command_manager.hpp"
#include "display_manager.hpp"
#include "espnow_manager.hpp"
#include "handlers/load_control_handler.hpp"
#include "handlers/ota_status_handler.hpp"
#include "handlers/solar_sensor_handler.hpp"
#include "handlers/water_tank_handler.hpp"
#include "hub_app.hpp"
#include "hub_nvs.hpp"
#include "hub_tasks.hpp"
#include "message_dispatcher.hpp"
#include "nvs_core.hpp"
#include "ota_manager.hpp"
#include "persistence_backend.hpp"
#include "system_state.hpp"
#include "time_manager.hpp"
#include "ui_events.hpp"
#include "ui_input_manager.hpp"
#include "wifi_manager.hpp"

static const char* TAG = "main";

static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_0;
static constexpr gpio_num_t I2C_SDA_GPIO = GPIO_NUM_8;
static constexpr gpio_num_t I2C_SCL_GPIO = GPIO_NUM_9;
static constexpr gpio_num_t ENCODER_PIN_A = GPIO_NUM_4;
static constexpr gpio_num_t ENCODER_PIN_B = GPIO_NUM_5;
static constexpr gpio_num_t ENCODER_PIN_SW = GPIO_NUM_6;

// HALs intances to be used by the application and other components
static idf_hals::NvsHAL hal_nvs;
static idf_hals::HalFreertos hal_freertos;
static idf_hals::SystemHAL hal_system;
static idf_hals::SleepHAL hal_sleep;
static idf_hals::HalSntp hal_sntp;
static idf_hals::HalSystemTime hal_system_time;
static idf_hals::GpioHAL hal_gpio;
static idf_hals::TimerHAL hal_timer;
static idf_hals::HalPcnt hal_pcnt;
static idf_hals::I2cHAL hal_i2c;

// NVS
static constexpr const char* CORE_NVS_KEY = "core";
static constexpr const char* STATS_NVS_KEY = "hub_stats";

// Core NVS
static RTC_DATA_ATTR CoreStorage g_rtc_core;
static RtcBackend rtc_core_backend(&g_rtc_core, sizeof(CoreStorage));
static NvsBackend nvs_core_backend{hal_nvs, CORE_NVS_KEY};
static NvsCore nvs_core{rtc_core_backend, nvs_core_backend};

// Hub Stats NVS
static RTC_DATA_ATTR HubStorage g_rtc_hub_stats;
static RtcBackend rtc_stats_backend(&g_rtc_hub_stats, sizeof(HubStorage));
static NvsBackend nvs_stats_backend{hal_nvs, STATS_NVS_KEY};
static HubNvs nvs_hub{rtc_stats_backend, nvs_stats_backend};

// OtaManager - hub self-update via WiFi
static HttpClient http_client;
static ManifestParser manifest_parser;
static OtaSession ota_session;
static System ota_system;
static TaskScheduler task_scheduler;
static RollbackManager rollback_manager;
static OtaDependencies ota_deps = {
    .http_client = http_client,
    .manifest_parser = manifest_parser,
    .ota_session = ota_session,
    .system = ota_system,
    .task_scheduler = task_scheduler,
    .rollback_manager = rollback_manager,
};

static OtaManager ota_manager(ota_deps);
static time_manager::TimeManager app_time_manager(hal_sntp, hal_system_time);

// UI Inputs & Queues
static ui_inputs::RotaryEncoderConfig g_encoder_cfg{
    .half_step_mode = false,
    .acceleration_enabled = true,
    .accel_gap_ms = 50,
    .accel_max_multiplier = 5,
    .glitch_filter_ns = 1000,
};
static ui_inputs::RotaryEncoder g_encoder(hal_pcnt, hal_timer, ENCODER_PIN_A, ENCODER_PIN_B, g_encoder_cfg);
static ui_inputs::Button g_encoder_push(hal_gpio, hal_timer, ENCODER_PIN_SW, /*active_low=*/true);
static ui_inputs::Button g_boot_button(hal_gpio, hal_timer, BOOT_BUTTON_GPIO, /*active_low=*/true);

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Smart Farm Hub starting...");

    g_state_mutex = hal_freertos.mutex_create();
    if (g_state_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create g_state_mutex");
    }

    QueueHandle_t ui_event_queue = hal_freertos.queue_create(20, sizeof(UiEvent));
    QueueHandle_t app_cmd_queue = hal_freertos.queue_create(20, sizeof(AppCommand));
    QueueHandle_t rx_queue = hal_freertos.queue_create(30, sizeof(espnow::AppMessage));

    if (ui_event_queue == nullptr || app_cmd_queue == nullptr || rx_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create UI / App / RX queues");
    }

    static UiInputManager ui_input_mgr(g_encoder, g_encoder_push, g_boot_button, ui_event_queue, hal_freertos);
    if (ui_input_mgr.init() == ESP_OK) {
        ui_input_mgr.start();
    }
    else {
        ESP_LOGE(TAG, "Failed to initialize UiInputManager");
    }

    auto& wifi = wifi_manager::WiFiManager::get_instance();
    auto& espnow = espnow::EspNowManager::instance();

    DisplayManagerConfig display_cfg{
        .sda_pin = I2C_SDA_GPIO,
        .scl_pin = I2C_SCL_GPIO,
    };
    static DisplayManager display_mgr(ui_event_queue, app_cmd_queue, &espnow, hal_freertos, hal_i2c, display_cfg);
    if (display_mgr.init() == ESP_OK) {
        display_mgr.start();
    }
    else {
        ESP_LOGE(TAG, "Failed to initialize DisplayManager");
    }

    HubApp app(nvs_core, nvs_hub, espnow, wifi, ota_manager, app_time_manager, hal_freertos, hal_system, hal_sleep, hal_timer);

    HubAppConfig config;

    if (app.init(config, app_cmd_queue, rx_queue) != ESP_OK) {
        ESP_LOGE(TAG, "Critical hardware/application initialization failure. Rebooting in 10s...");
        hal_freertos.task_delay(pdMS_TO_TICKS(10000));
        esp_restart();
        return;
    }

    static hub::CommandManager command_mgr(
        espnow, app.get_stats(), nvs_hub, app_time_manager, g_system_state, g_state_mutex, hal_freertos);
    app.set_command_manager(command_mgr);

    // Create and register payload handlers with the MessageDispatcher
    EventGroupHandle_t solar_events = hal_freertos.event_group_create();
    if (solar_events == nullptr) {
        ESP_LOGE(TAG, "Failed to create solar_events EventGroup");
    }

    static hub::MessageDispatcher msg_dispatcher(rx_queue, espnow, hal_freertos);
    static hub::WaterTankHandler water_tank_handler(
        g_system_state, g_state_mutex, command_mgr, hal_timer, hal_freertos);
    static hub::SolarSensorHandler solar_sensor_handler(
        g_system_state, g_state_mutex, command_mgr, hal_timer, hal_freertos, solar_events);
    static hub::LoadControlHandler load_control_handler(
        g_system_state, g_state_mutex, command_mgr, hal_timer, hal_freertos);
    static hub::OtaStatusHandler ota_status_handler(
        [&app](uint8_t node_id, uint8_t major, uint8_t minor, uint8_t patch) {
            app.on_node_version_received(node_id, major, minor, patch);
        });

    msg_dispatcher.register_handler(farm::PayloadType::WATER_LEVEL_REPORT, &water_tank_handler);
    msg_dispatcher.register_handler(farm::PayloadType::SOLAR_SENSOR_REPORT, &solar_sensor_handler);
    msg_dispatcher.register_handler(farm::PayloadType::LOAD_CONTROL_STATUS, &load_control_handler);
    msg_dispatcher.register_handler(farm::PayloadType::OTA_STATUS_REPORT, &ota_status_handler);

    if (msg_dispatcher.start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MessageDispatcher");
    }

    app.run();
}
