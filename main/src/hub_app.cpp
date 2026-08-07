// main/src/hub_app.cpp
#include "hub_app.hpp"
#include "farm_protocol_types.hpp" // WaterLevelReport, FarmPayloadType, FarmNodeId
#include "espnow_types.hpp"        // AppMessage, CommandType
#include "wifi_manager.hpp"
#include "espnow_manager.hpp"
#include "secrets.hpp" // WIFI_SSID, WIFI_PASS, SERVER_URL (not committed)
#include "system_state.hpp"
#include "esp_timer.h"

#include "version_helper.hpp"
#include "i18n/i18n.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "HubApp";

static constexpr uint16_t START_WIFI_TIMEOUT_MS = 10000;
static constexpr uint16_t CONNECT_WIFI_TIMEOUT_MS = 15000;
static constexpr uint16_t DISCONNECT_WIFI_TIMEOUT_MS = 2000;

SystemState g_system_state;
SemaphoreHandle_t g_state_mutex = nullptr;

HubApp::HubApp(
    INvsCore& core_storage,
    IHubNvs& hub_storage,
    espnow::IEspNowManager& espnow,
    wifi_manager::IWiFiManager& wifi,
    IOtaManager& ota_manager,
    time_manager::ITimeManager& time_manager,
    idf_hals::IHalFreertos& rtos,
    idf_hals::ISystemHAL& hal_system,
    idf_hals::ISleepHAL& hal_sleep)
    : core_storage_(core_storage)
    , hub_storage_(hub_storage)
    , espnow_(espnow)
    , wifi_(wifi)
    , ota_manager_(ota_manager)
    , time_manager_(time_manager)
    , hal_rtos_(rtos)
    , hal_system_(hal_system)
    , hal_sleep_(hal_sleep)

{
}

esp_err_t HubApp::init(const HubAppConfig& config, QueueHandle_t app_cmd_queue, QueueHandle_t rx_queue)
{
    config_ = config;
    app_cmd_queue_ = app_cmd_queue;
    esp_err_t err;

    if ((err = init_ota_manager()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize OTA Manager: %s", esp_err_to_name(err));
        return err;
    }
    if (ota_manager_.check_pending_verify()) {
        pending_firmware_verify_ = true;
    }

    if ((err = init_core_storage()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize core storage: %s", esp_err_to_name(err));
        session_healthy_ = false;
        return err;
    }

    if ((err = init_hub_storage()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize hub storage: %s", esp_err_to_name(err));
        session_healthy_ = false;
        return err;
    }

    log_boot_summary();

    if ((err = init_wifi()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        session_healthy_ = false;
        return err;
    }

    if ((err = init_espnow(rx_queue)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(err));
        session_healthy_ = false;
        return err;
    }

    time_manager::TimeManagerConfig time_cfg;
    time_cfg.use_dhcp_sntp = false;
    time_cfg.smooth_sync = false;
    time_cfg.sync_interval_ms = 3600000; // 1 hour
    time_cfg.default_server = "pool.ntp.org";
    time_cfg.timezone = "<-04>4"; // UTC-4 timezone

    if ((err = time_manager_.init(time_cfg)) == ESP_OK) {
        time_manager_.start_sntp();

        uint8_t timeout_s = 10;
        while (!time_manager_.is_synchronized() && timeout_s > 0) {
            hal_rtos_.task_delay(pdMS_TO_TICKS(1000));
            timeout_s--;
        }
        if (time_manager_.is_synchronized()) {
            ESP_LOGI(TAG, "Time successfully synchronized via SNTP");
        }
        else {
            ESP_LOGW(TAG, "Time sync timeout reached (10s), proceeding without initial SNTP sync");
        }
    }
    else {
        ESP_LOGE(TAG, "Failed to init time manager: %s", esp_err_to_name(err));
    }

    // 9. Roolback if session is not healthy
    if (!session_healthy_) {
        if (pending_firmware_verify_) {
            ESP_LOGE(TAG, "Session is not healthy during OTA verification.");
            check_firmware();
        }
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t HubApp::init_core_storage()
{
    esp_err_t ret = core_storage_.load_core(core_);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Core storage load failed (%s), recreating default storage", esp_err_to_name(ret));

        CoreStorage default_core = {};
        default_core.reset();
        default_core.node_id = farm::NodeId::HUB;
        default_core.node_type = farm::NodeType::HUB;
        default_core.power_profile = PowerProfile::ALWAYS_ON;

        ret = core_storage_.create_default_storage(core_, default_core);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    else {
        ESP_LOGI(TAG, "Loaded core data from storage");
    }

    core_storage_.process_boot_reasons(
        core_, hal_system_.reset_reason(), hal_sleep_.get_wakeup_cause(), pending_core_commit_);

    return ESP_OK;
}

esp_err_t HubApp::init_hub_storage()
{
    esp_err_t ret = hub_storage_.load_app_data(stats_);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Loaded hub stats from storage (language=%u)", stats_.language);
        if (stats_.language < static_cast<uint8_t>(Language::COUNT)) {
            I18n::set_language(static_cast<Language>(stats_.language));
        }
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Hub storage load failed (%s), recreating default storage", esp_err_to_name(ret));
    stats_.reset();
    ret = hub_storage_.save_app_data(stats_, /*force_nvs_commit=*/true);
    if (ret == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to initialize tank storage: %s", esp_err_to_name(ret));
    return ret;
}

void HubApp::log_boot_summary()
{
    auto version = ota_manager_.get_running_version();
    if (version.has_value()) {
        ESP_LOGI(
            TAG,
            "Boot #%lu | FW v%u.%u.%u | Messages received: %lu | Commands sent: %lu",
            static_cast<unsigned long>(core_.boot_count),
            version->major,
            version->minor,
            version->patch,
            static_cast<unsigned long>(stats_.messages_received),
            static_cast<unsigned long>(stats_.commands_sent));
    }
    else {
        ESP_LOGI(
            TAG,
            "Boot #%lu | Crash: %u | FW v%u.%u.%u | Messages received: %lu | Commands sent: %lu",
            static_cast<unsigned long>(core_.boot_count),
            core_.crash_count,
            core_.fw_major,
            core_.fw_minor,
            core_.fw_patch,
            static_cast<unsigned long>(stats_.messages_received),
            static_cast<unsigned long>(stats_.commands_sent));
    }

    for (const auto& p : stats_.pending_cmds) {
        if (p.active) {
            ESP_LOGW(
                TAG,
                "Pending command 0x%02X armed for node 0x%02X",
                static_cast<uint8_t>(p.command),
                static_cast<uint8_t>(p.node_id));
        }
    }
}

esp_err_t HubApp::init_wifi()
{
    esp_err_t err = wifi_.init();
    if (err != ESP_OK)
        return err;

    wifi_.add_credentials(WIFI_SSID, WIFI_PASS);
    err = wifi_.start(START_WIFI_TIMEOUT_MS);
    if (err != ESP_OK)
        return err;

    return connect_wifi_with_retry(4);
}

esp_err_t HubApp::connect_wifi_with_retry(uint8_t max_attempts)
{
    if (wifi_.get_state() == wifi_manager::State::CONNECTED_GOT_IP) {
        return ESP_OK;
    }

    static constexpr uint16_t DELAY_BETWEEN_ATTEMPTS_MS = 1500;
    esp_err_t err = ESP_FAIL;
    for (uint8_t attempt = 1; attempt <= max_attempts; ++attempt) {
        ESP_LOGI(TAG, "Connecting to WiFi (attempt %u/%u)...", attempt, max_attempts);
        err = wifi_.connect(CONNECT_WIFI_TIMEOUT_MS);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WiFi connected successfully");
            update_wifi_status();
            return ESP_OK;
        }

        ESP_LOGW(TAG, "WiFi connection attempt %u failed: %s", attempt, esp_err_to_name(err));
        if (attempt < max_attempts) {
            wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
            uint32_t delay_ms = DELAY_BETWEEN_ATTEMPTS_MS * attempt;
            hal_rtos_.task_delay(pdMS_TO_TICKS(delay_ms));
        }
    }

    ESP_LOGE(TAG, "Failed to connect to WiFi after %u attempts: %s", max_attempts, esp_err_to_name(err));
    update_wifi_status();
    return err;
}

void HubApp::update_wifi_status()
{
    bool is_connected = (wifi_.get_state() == wifi_manager::State::CONNECTED_GOT_IP);
    int8_t rssi = 0;
    if (is_connected) {
        wifi_.get_rssi(rssi);
    }

    if (g_state_mutex != nullptr && hal_rtos_.semaphore_take(g_state_mutex, 0) == pdTRUE) {
        g_system_state.wifi_connected = is_connected;
        g_system_state.wifi_rssi = is_connected ? rssi : 0;
        hal_rtos_.semaphore_give(g_state_mutex);
    }
}

esp_err_t HubApp::init_espnow(QueueHandle_t rx_queue)
{
    if (rx_queue == nullptr) {
        ESP_LOGE(TAG, "rx_queue is null");
        return ESP_ERR_INVALID_ARG;
    }

    espnow::EspNowConfig espnow_cfg;
    espnow_cfg.node_id = espnow::ReservedIds::HUB;
    espnow_cfg.node_type = espnow::ReservedTypes::HUB;
    espnow_cfg.app_rx_queue = rx_queue;
    espnow_cfg.wifi_channel = 1;
    espnow_cfg.heartbeat_interval_ms = 0;
    espnow_cfg.ack_timeout_ms = 500;

    esp_err_t err = espnow_.init(espnow_cfg);
    if (err != ESP_OK) {
        return err;
    }

    espnow_.set_channel_policy(espnow::ChannelPolicy::FIXED);
    return ESP_OK;
}

esp_err_t HubApp::init_ota_manager()
{
    OtaConfig ota_config{
        .device_type = "hub",
        .manifest_url = SERVER_URL,
        .task_stack_size = 8192,
        .task_priority = 5,
        .transport = {.manifest_timeout_ms = 30000, .firmware_timeout_ms = 30000},
        .security = {.allow_http_during_development = true},
        .allow_same_version = false,
        .restart_on_success = false,
    };

    if (!ota_manager_.init(ota_config)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

void HubApp::run()
{
    ESP_LOGI(
        TAG,
        "Hub running. Boot #%lu. Listening for application commands...",
        static_cast<unsigned long>(core_.boot_count));

    update_wifi_status();
    last_wifi_poll_ts_ = esp_timer_get_time() / 1000;

    AppCommand cmd;
    while (true) {
        if (app_cmd_queue_ != nullptr && hal_rtos_.queue_receive(app_cmd_queue_, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            handle_app_command(cmd);
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_wifi_poll_ts_ >= 5000) {
            last_wifi_poll_ts_ = now_ms;
            update_wifi_status();
        }
    }
}

void HubApp::handle_app_command(const AppCommand& cmd)
{
    ESP_LOGI(
        TAG,
        "Processing AppCommand: espnow_cmd=0x%02X, target_node=0x%02X, param=%lu",
        static_cast<uint8_t>(cmd.espnow_cmd),
        static_cast<uint8_t>(cmd.target_node),
        static_cast<unsigned long>(cmd.param));

    if (cmd.target_node == farm::NodeId::HUB) {
        if (cmd.espnow_cmd == espnow::CommandType::REBOOT) {
            ESP_LOGW(TAG, "Reboot requested via AppCommand! Disconnecting WiFi and rebooting in 1s...");
            if (wifi_.get_state() != wifi_manager::State::UNINITIALIZED &&
                wifi_.get_state() != wifi_manager::State::INITIALIZED) {
                wifi_.disconnect(1000);
                wifi_.stop(1000);
            }
            hal_rtos_.task_delay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
        else if (static_cast<uint8_t>(cmd.espnow_cmd) == 0xFE) {
            stats_.language = static_cast<uint8_t>(cmd.param);
            hub_storage_.save_app_data(stats_, /*force_nvs_commit=*/true);
            ESP_LOGI(TAG, "Persisted new language preference to NVS: %lu", static_cast<unsigned long>(cmd.param));
        }
    }
    else {
        if (set_pending_command(cmd.target_node, cmd.espnow_cmd, cmd.requires_ack)) {
            ESP_LOGI(
                TAG,
                "Armed command 0x%02X (requires_ack=%d) for target node 0x%02X",
                static_cast<uint8_t>(cmd.espnow_cmd),
                cmd.requires_ack,
                static_cast<uint8_t>(cmd.target_node));
        }
    }
}

void HubApp::dispatch_pending_command(farm::NodeId node_id)
{
    espnow::CommandType cmd;
    bool requires_ack = true;
    if (!has_pending_command(node_id, cmd, requires_ack))
        return;

    esp_err_t err;
    if (cmd == static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME)) {
        if (!time_manager_.is_synchronized()) {
            ESP_LOGW(
                TAG,
                "Cannot dispatch SYNC_TIME to 0x%02X: Hub is not time-synchronized",
                static_cast<uint8_t>(node_id));
            return;
        }
        auto packet = time_manager_.create_time_packet();
        farm::TimeSyncCommand sync_cmd;
        sync_cmd.timestamp_ms = packet.timestamp_ms;
        sync_cmd.tz_offset_min = packet.tz_offset_min;
        sync_cmd.sync_source = static_cast<uint8_t>(packet.sync_source);
        sync_cmd.flags = packet.flags;

        err = espnow_.send_command(node_id, cmd, &sync_cmd, sizeof(sync_cmd), requires_ack);
    }
    else {
        err = espnow_.send_command(node_id, cmd, nullptr, 0, requires_ack);
    }

    if (err == ESP_OK) {
        clear_pending_command(node_id);
        stats_.commands_sent++;

        ESP_LOGW(
            TAG, "Command 0x%02X dispatched to node 0x%02X", static_cast<uint8_t>(cmd), static_cast<uint8_t>(node_id));
    }
    else {
        ESP_LOGE(
            TAG, "Failed to dispatch command to node 0x%02X: %s", static_cast<uint8_t>(node_id), esp_err_to_name(err));
    }
}

bool HubApp::set_pending_command(farm::NodeId node_id, espnow::CommandType cmd, bool requires_ack)
{
    // Check if already set
    for (auto& entry : stats_.pending_cmds) {
        if (entry.active && entry.node_id == node_id) {
            ESP_LOGW(
                TAG,
                "Cannot arm command 0x%02X for node 0x%02X: Node already has pending command 0x%02X",
                static_cast<uint8_t>(cmd),
                static_cast<uint8_t>(node_id),
                static_cast<uint8_t>(entry.command));
            return false;
        }
    }
    // Find empty slot
    for (auto& entry : stats_.pending_cmds) {
        if (!entry.active) {
            entry = {true, node_id, cmd, requires_ack};
            hub_storage_.save_app_data(stats_);
            return true;
        }
    }
    ESP_LOGE(TAG, "No free slot for pending command (MAX_HUB_NODES=%d)", MAX_HUB_NODES);
    return false;
}

bool HubApp::has_pending_command(farm::NodeId node_id, espnow::CommandType& out_cmd, bool& out_requires_ack)
{
    for (const auto& entry : stats_.pending_cmds) {
        if (entry.active && entry.node_id == node_id) {
            out_cmd = entry.command;
            out_requires_ack = entry.requires_ack;
            return true;
        }
    }
    return false;
}

void HubApp::clear_pending_command(farm::NodeId node_id)
{
    for (auto& entry : stats_.pending_cmds) {
        if (entry.active && entry.node_id == node_id) {
            entry = {};
            hub_storage_.save_app_data(stats_);
            return;
        }
    }
}

void HubApp::check_firmware()
{
    if (!pending_firmware_verify_) {
        return;
    }

    if (!session_healthy_ || !ota_manager_.confirm_app_valid()) {
        farm::OtaErrorCode err =
            !session_healthy_ ? farm::OtaErrorCode::HEALTH_CHECK_FAILED : farm::OtaErrorCode::PARTITION_CONFIRM_FAILED;

        ESP_LOGE(TAG, "Failed to confirm firmware. Triggering rollback (reason: %d).", static_cast<int>(err));
        wifi_.disconnect(DISCONNECT_WIFI_TIMEOUT_MS);
        ota_manager_.rollback_and_reboot();
        return;
    }

    // If we get here, the firmware is valid and confirme
    pending_firmware_verify_ = false;

    auto version = ota_manager_.get_running_version();
    if (version.has_value()) {
        core_.fw_major = version->major;
        core_.fw_minor = version->minor;
        core_.fw_patch = version->patch;
    }
    pending_core_commit_ = true;

    ESP_LOGI(TAG, "Firmware confirmed successfully. Versio: %d.%d.%d", core_.fw_major, core_.fw_minor, core_.fw_patch);
}