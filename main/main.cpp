// main/main.cpp
#include "esp_log.h"
#include "espnow_manager.hpp"
#include "hub_app.hpp"
#include "nvs_core.hpp"
#include "hub_nvs.hpp"

#include "hal_system.hpp"
#include "hal_nvs.hpp"
#include "hal_freertos.hpp"
#include "hal_sleep.hpp"
#include "hal_sntp.hpp"
#include "hal_system_time.hpp"

#include "persistence_backend.hpp"
#include "esp_attr.h"
#include "wifi_manager.hpp"
#include "ota_manager.hpp"
#include "time_manager.hpp"

static const char* TAG = "main";

static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_0;
static constexpr gpio_num_t I2C_SDA_GPIO = GPIO_NUM_8;
static constexpr gpio_num_t I2C_SCL_GPIO = GPIO_NUM_9;

static idf_hals::NvsHAL hal_nvs;
static idf_hals::HalFreertos hal_freertos;
static idf_hals::SystemHAL hal_system;
static idf_hals::SleepHAL hal_sleep;
static idf_hals::HalSntp hal_sntp;
static idf_hals::HalSystemTime hal_system_time;

// NVS
static constexpr const char* CORE_NVS_KEY = "core";
static constexpr const char* STATS_NVS_KEY = "hub_stats";

static RTC_DATA_ATTR CoreStorage g_rtc_core;
static RtcBackend rtc_core_backend(&g_rtc_core, sizeof(CoreStorage));
static NvsBackend nvs_core_backend{hal_nvs, CORE_NVS_KEY};
static NvsCore nvs_core{rtc_core_backend, nvs_core_backend};

static RTC_DATA_ATTR HubStats g_rtc_hub_stats;
static RtcBackend rtc_stats_backend(&g_rtc_hub_stats, sizeof(HubStats));
static NvsBackend nvs_stats_backend{hal_nvs, STATS_NVS_KEY};
static HubNvs nvs_hub{rtc_stats_backend, nvs_stats_backend};

// OtaManager (hub self-update via WiFi)
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

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Smart Farm Hub starting...");

    auto& wifi = wifi_manager::WiFiManager::get_instance();
    auto& espnow = espnow::EspNowManager::instance();

    HubApp app(nvs_core, nvs_hub, espnow, wifi, ota_manager, app_time_manager, hal_freertos, hal_system, hal_sleep);

    HubAppConfig config;
    config.boot_button_gpio = BOOT_BUTTON_GPIO;
    config.i2c_sda_gpio = I2C_SDA_GPIO;
    config.i2c_scl_gpio = I2C_SCL_GPIO;

    if (app.init(config) != ESP_OK) {
        ESP_LOGE(TAG, "Critical hardware/application initialization failure. Rebooting in 10s...");
        hal_freertos.task_delay(pdMS_TO_TICKS(10000));
        esp_restart();
        return;
    }

    app.run();
}
