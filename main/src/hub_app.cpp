#include "driver/gpio.h"

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "hub_app.hpp"
#include "farm_protocol_types.hpp" // WaterLevelReport, FarmPayloadType, FarmNodeId
#include "espnow_types.hpp"        // AppMessage, CommandType
#include "wifi_manager.hpp"
#include "espnow_manager.hpp"
#include "secrets.hpp" // WIFI_SSID, WIFI_PASS, SERVER_URL (not committed)
#include "system_state.hpp"
#include "version_helper.hpp"
#include "i18n/i18n.hpp"

static const char* TAG = "HubApp";

static constexpr uint16_t START_WIFI_TIMEOUT_MS = 10000;
static constexpr uint16_t CONNECT_WIFI_TIMEOUT_MS = 15000;
static constexpr uint16_t DISCONNECT_WIFI_TIMEOUT_MS = 2000;

SystemState g_system_state;
SemaphoreHandle_t g_state_mutex = nullptr;

HubApp::HubApp(
    INvsCore& core_storage,
    IHubNvs& hub_storage,
    hub::INodeRegistry& node_registry,
    hub::ICommandManager& cmd_mgr,
    UiSnapshot& ui_snapshot,
    espnow::IEspNowManager& espnow,
    wifi_manager::IWiFiManager& wifi,
    IOtaManager& ota_manager,
    time_manager::ITimeManager& time_manager,
    idf_hals::IHalFreertos& rtos,
    idf_hals::ISystemHAL& hal_system,
    idf_hals::ISleepHAL& hal_sleep,
    idf_hals::ITimerHAL& hal_timer)
    : core_storage_(core_storage)
    , hub_storage_(hub_storage)
    , node_registry_(node_registry)
    , cmd_mgr_(cmd_mgr)
    , ui_snapshot_(ui_snapshot)
    , espnow_(espnow)
    , wifi_(wifi)
    , ota_manager_(ota_manager)
    , time_manager_(time_manager)
    , hal_rtos_(rtos)
    , hal_system_(hal_system)
    , hal_sleep_(hal_sleep)
    , hal_timer_(hal_timer)
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
    CoreData default_core = {};
    default_core.reset();
    default_core.node_id = farm::NodeId::HUB;
    default_core.node_type = farm::NodeType::HUB;
    default_core.power_profile = farm::PowerProfile::ALWAYS_ON;

    esp_err_t ret = core_storage_.init(core_, default_core);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize core storage: %s", esp_err_to_name(ret));
        return ret;
    }

    core_storage_.process_boot_reasons(
        core_, hal_system_.reset_reason(), hal_sleep_.get_wakeup_cause(), pending_core_commit_);

    return ESP_OK;
}

esp_err_t HubApp::init_hub_storage()
{
    HubStats default_stats = {};
    default_stats.reset();

    esp_err_t ret = hub_storage_.init_app_data(stats_, default_stats);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize hub storage: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Loaded hub stats from storage (language=%u)", stats_.language);
    if (stats_.language < static_cast<uint8_t>(Language::COUNT)) {
        I18n::set_language(static_cast<Language>(stats_.language));
    }

    // Populate in-memory NodeRegistry from persistent storage
    for (size_t i = 0; i < farm::MAX_HUB_NODES; ++i) {
        const auto& info = stats_.node_info[i];
        if (info.node_id != farm::NodeId::UNKNOWN && info.node_id != farm::NodeId::BROADCAST) {
            node_registry_.set_node_metadata(
                info.node_id, info.power_profile, info.fw_major, info.fw_minor, info.fw_patch);
        }
    }

    return ESP_OK;
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

    for (size_t r = 0; r < farm::MAX_HUB_NODES; ++r) {
        for (size_t c = 0; c < MAX_PENDING_PER_NODE; ++c) {
            const auto& p = stats_.pending_cmds[r][c];
            if (p.active) {
                ESP_LOGW(
                    TAG,
                    "Pending command 0x%02X armed for node 0x%02X (slot %zu)",
                    static_cast<uint8_t>(p.command),
                    static_cast<uint8_t>(p.node_id),
                    c);
            }
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

    ESP_LOGI(TAG, "Connecting to WiFi async...");
    // err = wifi_.connect(CONNECT_WIFI_TIMEOUT_MS, 3, 1500);
    err = wifi_.connect();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected successfully via WiFiManager::connect");
    }
    else {
        ESP_LOGE(TAG, "WiFi connection failed after retries: %s", esp_err_to_name(err));
    }

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

    ui_snapshot_.update_wifi(is_connected, is_connected ? rssi : 0);
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
    espnow_cfg.enable_heartbeat = false;
    espnow_cfg.logical_ack_retries = 0;
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

static constexpr uint32_t NVS_PERIODIC_COMMIT_INTERVAL_MS = 15 * 60 * 1000; // 15 minutes

void HubApp::save_persistent_state()
{
    int64_t now_ms = hal_timer_.get_time_us() / 1000;

    bool periodic_commit =
        (last_nvs_commit_ts_ > 0) && ((now_ms - last_nvs_commit_ts_) >= NVS_PERIODIC_COMMIT_INTERVAL_MS);

    if (periodic_commit) {
        ESP_LOGI(
            TAG,
            "Periodic NVS commit triggered (%lu ms elapsed)",
            static_cast<unsigned long>(now_ms - last_nvs_commit_ts_));
    }

    bool force_core = pending_core_commit_ || periodic_commit;
    bool force_tank = pending_hub_commit_ || periodic_commit;

    if (!force_core && !force_tank) {
        return;
    }

    if (core_storage_.save_core(core_, force_core) == ESP_OK) {
        pending_core_commit_ = false;
    }
    else {
        ESP_LOGE(TAG, "Failed to save core storage to NVS");
    }

    if (hub_storage_.save_app_data(stats_, force_tank) == ESP_OK) {
        pending_hub_commit_ = false;
    }
    else {
        ESP_LOGE(TAG, "Failed to save hub stats to NVS");
    }

    last_nvs_commit_ts_ = now_ms;
}

void HubApp::run()
{
    ESP_LOGI(
        TAG,
        "Hub running. Boot #%lu. Listening for application commands...",
        static_cast<unsigned long>(core_.boot_count));

    update_wifi_status();
    last_wifi_poll_ts_ = hal_timer_.get_time_us() / 1000;
    last_nvs_commit_ts_ = last_wifi_poll_ts_;

    AppCommand cmd;
    while (true) {
        if (app_cmd_queue_ != nullptr && hal_rtos_.queue_receive(app_cmd_queue_, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            handle_app_command(cmd);
        }

        save_persistent_state();

        int64_t now_ms = hal_timer_.get_time_us() / 1000;
        if (now_ms - last_wifi_poll_ts_ >= 5000) {
            last_wifi_poll_ts_ = now_ms;
            update_wifi_status();
        }
    }
}

void HubApp::on_node_version_received(uint8_t node_id, uint8_t major, uint8_t minor, uint8_t patch)
{
    auto farm_node_id = static_cast<farm::NodeId>(node_id);

    // 1. Update in-memory NodeRegistry for instantaneous UI/system queries
    node_registry_.set_fw_version(farm_node_id, major, minor, patch);

    // 2. Update persistent stats and save to NVS immediately on new version report
    stats_.set_node_fw_version(farm_node_id, major, minor, patch);
    hub_storage_.save_app_data(stats_);

    ESP_LOGI(
        TAG,
        "Received FW version v%u.%u.%u for node 0x%02X, updated NodeRegistry and saved to NVS",
        major,
        minor,
        patch,
        node_id);
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
        if (cmd_mgr_.send_command(cmd.target_node, cmd.espnow_cmd, cmd.requires_ack)) {
            ESP_LOGI(
                TAG,
                "Command 0x%02X processed for target node 0x%02X",
                static_cast<uint8_t>(cmd.espnow_cmd),
                static_cast<uint8_t>(cmd.target_node));
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