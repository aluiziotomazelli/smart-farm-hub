// main/main.cpp
#include "esp_log.h"
#include "espnow_manager.hpp"
#include "hub_app.hpp"
#include "hub_nvs.hpp"
#include "hal_nvs.hpp"
#include "wifi_manager.hpp"
#include "ota_manager.hpp"
#include "farm_protocol_types.hpp"
#include "secrets.hpp"         // WIFI_SSID, WIFI_PASS, SERVER_URL (not committed)

static const char *TAG = "main";

static constexpr gpio_num_t BOOT_BUTTON_GPIO = GPIO_NUM_0;

// NVS
static idf_hals::NvsHAL hal_nvs;
static HubNvs          nvs{hal_nvs};

// OtaManager (hub self-update via WiFi)
static HttpClient      http_client;
static ManifestParser  manifest_parser;
static OtaSession      ota_session;
static System          ota_system;
static TaskScheduler   task_scheduler;
static RollbackManager rollback_manager;
static OtaDependencies ota_deps = {
    .http_client      = http_client,
    .manifest_parser  = manifest_parser,
    .ota_session      = ota_session,
    .system           = ota_system,
    .task_scheduler   = task_scheduler,
    .rollback_manager = rollback_manager,
};
static OtaConfig ota_config{
    .device_type        = "hub",
    .manifest_url       = SERVER_URL,
    .task_stack_size    = 8192,
    .task_priority      = 5,
    .transport          = {.manifest_timeout_ms = 30000, .firmware_timeout_ms = 30000},
    .security           = {.allow_http_during_development = true},
    .allow_same_version = false,
    .restart_on_success = true,
};
static OtaManager ota_manager(ota_deps);

static QueueHandle_t app_rx_queue = nullptr;

static esp_err_t setup_hardware()
{
    esp_err_t err;

    // NVS
    if ((err = nvs.init_partition()) != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return err;
    }
    nvs.load();
    nvs.stats.boot_count++;
    ESP_LOGI(TAG, "Boot #%lu | Messages received: %lu | Commands sent: %lu",
        nvs.stats.boot_count, nvs.stats.messages_received, nvs.stats.commands_sent);

    // Log any armed pending commands
    for (const auto &p : nvs.stats.pending_cmds) {
        if (p.active) {
            ESP_LOGW(TAG, "Pending command 0x%02X armed for node 0x%02X",
                static_cast<uint8_t>(p.command), static_cast<uint8_t>(p.node_id));
        }
    }

    // WiFi
    auto &wifi = wifi_manager::WiFiManager::get_instance();
    if ((err = wifi.init()) != ESP_OK) return err;
    wifi.add_credentials(WIFI_SSID, WIFI_PASS); // best-effort
    if ((err = wifi.start()) != ESP_OK) return err;

    // ESP-NOW (HUB role)
    app_rx_queue = xQueueCreate(30, sizeof(espnow::AppMessage));
    espnow::EspNowConfig espnow_cfg;
    espnow_cfg.node_id              = espnow::ReservedIds::HUB;
    espnow_cfg.node_type            = espnow::ReservedTypes::HUB;
    espnow_cfg.app_rx_queue         = app_rx_queue;
    espnow_cfg.wifi_channel         = 1;
    espnow_cfg.heartbeat_interval_ms = 0; // Hub does not send heartbeats

    auto &espnow = espnow::EspNowManager::instance();
    if ((err = espnow.init(espnow_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Connect to WiFi synchronously
    ESP_LOGI(TAG, "Connecting to WiFi synchronously...");
    if ((err = wifi.connect(15000)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", esp_err_to_name(err));
        return err;
    }

    // Set ChannelPolicy to FIXED now that WiFi is connected to AP
    espnow.set_channel_policy(espnow::ChannelPolicy::FIXED);

    // BOOT button (active-low, input pull-up)
    gpio_config_t btn_cfg = {};
    btn_cfg.pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO;
    btn_cfg.mode         = GPIO_MODE_INPUT;
    btn_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    btn_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&btn_cfg);

    // OtaManager (hub self-update)
    if (!ota_manager.init(ota_config)) {
        ESP_LOGE(TAG, "OTA Manager init failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Smart Farm Hub starting...");

    if (setup_hardware() != ESP_OK) {
        ESP_LOGE(TAG, "Critical init failure. Rebooting in 10s...");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
        return;
    }

    auto &espnow = espnow::EspNowManager::instance();
    HubApp app(espnow, nvs, BOOT_BUTTON_GPIO, app_rx_queue);
    app.run();
}
