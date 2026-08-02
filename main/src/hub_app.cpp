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

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "ui_controller.hpp"
#include "hal_display_ssd1306.hpp"
#include "framebuffer_graphics_context.hpp"

static const char* TAG = "HubApp";

static constexpr uint16_t START_WIFI_TIMEOUT_MS = 10000;
static constexpr uint16_t CONNECT_WIFI_TIMEOUT_MS = 15000;
static constexpr uint16_t DISCONNECT_WIFI_TIMEOUT_MS = 2000;

SystemState g_system_state;
SemaphoreHandle_t g_state_mutex = nullptr;

static i2c_master_bus_handle_t i2c_bus = nullptr;
static HalDisplaySsd1306* display_hal = nullptr;
static FramebufferGraphicsContext* gfx_ctx = nullptr;

struct DisplayTaskArgs
{
    FramebufferGraphicsContext* gfx;
    QueueHandle_t ui_event_queue;
    QueueHandle_t app_cmd_queue;
};

extern "C" void display_task(void* arg)
{
    auto* args = static_cast<DisplayTaskArgs*>(arg);
    UIController ui(*args->gfx, args->app_cmd_queue);

    while (true) {
        UiEvent event;
        if (args->ui_event_queue != nullptr) {
            if (xQueueReceive(args->ui_event_queue, &event, pdMS_TO_TICKS(500)) == pdTRUE) {
                ui.handle_event(event);
                while (xQueueReceive(args->ui_event_queue, &event, 0) == pdTRUE) {
                    ui.handle_event(event);
                }
            }
        }
        else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        SystemState snapshot;

        bool is_wifi_connected =
            (wifi_manager::WiFiManager::get_instance().get_state() == wifi_manager::State::CONNECTED_GOT_IP);
        bool ota_active = false;
        uint8_t espnow_peers = espnow::EspNowManager::instance().get_peers().size();

        if (xSemaphoreTake(g_state_mutex, portMAX_DELAY) == pdTRUE) {
            g_system_state.wifi_connected = is_wifi_connected;
            g_system_state.ota_in_progress = ota_active;
            g_system_state.espnow_peers = espnow_peers;
            snapshot = g_system_state;
            xSemaphoreGive(g_state_mutex);
        }

        args->gfx->clear(0);
        ui.render_current_screen(snapshot);
        args->gfx->flush();
    }
}

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

esp_err_t HubApp::init(const HubAppConfig& config, QueueHandle_t ui_event_queue, QueueHandle_t app_cmd_queue)
{
    config_ = config;
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

    if ((err = init_espnow()) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(err));
        session_healthy_ = false;
        return err;
    }

    if ((err = init_display(ui_event_queue, app_cmd_queue)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Display: %s", esp_err_to_name(err));
        session_healthy_ = false;
        // Non-critical, continue
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
        ESP_LOGI(TAG, "Loaded hub stats from storage");
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
    return err;
}

esp_err_t HubApp::init_espnow()
{
    rx_queue_ = hal_rtos_.queue_create(30, sizeof(espnow::AppMessage));
    if (rx_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create rx_queue_");
        return ESP_FAIL;
    }

    espnow::EspNowConfig espnow_cfg;
    espnow_cfg.node_id = espnow::ReservedIds::HUB;
    espnow_cfg.node_type = espnow::ReservedTypes::HUB;
    espnow_cfg.app_rx_queue = rx_queue_;
    espnow_cfg.wifi_channel = 1;
    espnow_cfg.heartbeat_interval_ms = 0;

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

esp_err_t HubApp::init_display(QueueHandle_t ui_event_queue, QueueHandle_t app_cmd_queue)
{
    ui_event_queue_ = ui_event_queue;
    app_cmd_queue_ = app_cmd_queue;

    g_state_mutex = hal_rtos_.mutex_create();
    if (g_state_mutex == nullptr) {
        ESP_LOGE(TAG, "Failed to create g_state_mutex");
        return ESP_FAIL;
    }

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = config_.i2c_sda_gpio;
    bus_config.scl_io_num = config_.i2c_scl_gpio;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    if (i2c_new_master_bus(&bus_config, &i2c_bus) == ESP_OK) {
        Ssd1306Config display_config;
        display_config.i2c_bus = i2c_bus;
        display_config.i2c_address = 0x3C;
        display_config.width = 128;
        display_config.height = 64;
        display_config.rst_gpio = -1;
        display_config.i2c_clk_speed_hz = 400000;

        display_hal = new HalDisplaySsd1306(display_config);
        display_hal->init();

        gfx_ctx = new FramebufferGraphicsContext(*display_hal);
        gfx_ctx->set_rotation(Rotation::ROTATION_180);
        gfx_ctx->clear(0);
        gfx_ctx->flush();
        ESP_LOGI(TAG, "Display initialized");

        static DisplayTaskArgs display_args;
        display_args.gfx = gfx_ctx;
        display_args.ui_event_queue = ui_event_queue_;
        display_args.app_cmd_queue = app_cmd_queue_;

        hal_rtos_.task_create(display_task, "display_task", 4096, &display_args, 2, nullptr);
    }
    else {
        ESP_LOGE(TAG, "Failed to initialize I2C master bus");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void HubApp::run()
{
    ESP_LOGI(
        TAG, "Hub running. Boot #%lu. Listening for node messages...", static_cast<unsigned long>(core_.boot_count));

    espnow::AppMessage msg;
    AppCommand cmd;
    while (true) {
        if (rx_queue_ != nullptr && hal_rtos_.queue_receive(rx_queue_, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            handle_message(msg);
        }
        if (app_cmd_queue_ != nullptr && hal_rtos_.queue_receive(app_cmd_queue_, &cmd, 0) == pdTRUE) {
            handle_app_command(cmd);
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
    }
    else {
        set_pending_command(cmd.target_node, cmd.espnow_cmd);
        ESP_LOGI(
            TAG,
            "Armed command 0x%02X for target node 0x%02X",
            static_cast<uint8_t>(cmd.espnow_cmd),
            static_cast<uint8_t>(cmd.target_node));
    }
}

void HubApp::handle_message(const espnow::AppMessage& msg)
{
    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    auto payload_type = static_cast<farm::PayloadType>(msg.payload_type);

    stats_.messages_received++;

    switch (payload_type) {
    case farm::PayloadType::WATER_LEVEL_REPORT:
    {
        const auto* report = reinterpret_cast<const farm::WaterLevelReport*>(msg.payload);

        stats_.last_wt_level_permille = report->level_permille;
        stats_.last_wt_distance_cm = report->distance_cm;
        stats_.last_wt_battery_mv = report->battery_mv;

        if (hal_rtos_.semaphore_take(g_state_mutex, portMAX_DELAY) == pdTRUE) {
            g_system_state.water_level_permille = report->level_permille;
            g_system_state.water_distance_cm = report->distance_cm;
            g_system_state.last_water_update_ts = esp_timer_get_time();
            g_system_state.water_battery_mv = report->battery_mv;

            uint8_t raw_status = static_cast<uint8_t>(report->status);
            g_system_state.water_fill_state = (raw_status >> 4) & 0x0F;
            uint8_t status_lower = raw_status & 0x0F;
            g_system_state.water_sensor_status =
                static_cast<farm::SensorStatus>((status_lower == 0x0F) ? 0xFF : status_lower);

            // Assuming this is the only node right now, or we just track overall peers
            g_system_state.espnow_last_rssi = msg.rssi;

            // Simple moving average for RSSI (or just set it for now)
            if (g_system_state.espnow_avg_rssi == 0) {
                g_system_state.espnow_avg_rssi = msg.rssi;
            }
            else {
                g_system_state.espnow_avg_rssi = (g_system_state.espnow_avg_rssi * 3 + msg.rssi) / 4;
            }

            hal_rtos_.semaphore_give(g_state_mutex);
        }

        ESP_LOGI(
            TAG,
            "[WATER TANK] Level: %u\u2030 | Distance: %.1f cm | Battery: %u mV (%u%%) "
            "| Float: %s | Backup: %s | RSSI: %d dBm | Time: %llu ms",
            report->level_permille,
            report->distance_cm,
            report->battery_mv,
            report->battery_percent,
            report->float_switch_is_full ? "FULL" : "EMPTY",
            report->backup_mode_active ? "ON" : "OFF",
            msg.rssi,
            static_cast<unsigned long long>(report->unix_time));

        // Dispatch pending command if armed
        dispatch_pending_command(node_id);

        // Temporary SyncTime command for test
        // if (set_pending_command(
        //         farm::NodeId::WATER_TANK, static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME))) {
        //     ESP_LOGW(
        //         TAG,
        //         "SyncTime command armed for WATER_TANK. "
        //         "Will be dispatched on its next message.");
        // }

        // ACK the message if required
        if (msg.requires_ack) {
            espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
        }
        break;
    }
    case farm::PayloadType::OTA_STATUS_REPORT:
    {
        const auto* report = reinterpret_cast<const farm::OtaStatusReport*>(msg.payload);
        ESP_LOGI(
            TAG,
            "[OTA STATUS REPORT] Node: 0x%02X | Result: %u | Error: 0x%02X | FW Version: %u.%u.%u",
            msg.sender_id,
            static_cast<uint8_t>(report->result),
            static_cast<uint8_t>(report->error_code),
            report->fw_major,
            report->fw_minor,
            report->fw_patch);

        if (msg.requires_ack) {
            espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
        }
        break;
    }
    default:
        ESP_LOGW(TAG, "Unknown payload 0x%02X from node 0x%02X", msg.payload_type, msg.sender_id);
        break;
    }

    hub_storage_.save_app_data(stats_);
}

void HubApp::dispatch_pending_command(farm::NodeId node_id)
{
    espnow::CommandType cmd;
    if (!has_pending_command(node_id, cmd))
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

        err = espnow_.send_command(static_cast<espnow::NodeId>(node_id), cmd, &sync_cmd, sizeof(sync_cmd), false);
    }
    else {
        err = espnow_.send_command(static_cast<espnow::NodeId>(node_id), cmd, nullptr, 0, false);
    }

    if (err == ESP_OK) {
        clear_pending_command(node_id);
        stats_.commands_sent++;

        if (hal_rtos_.semaphore_take(g_state_mutex, portMAX_DELAY) == pdTRUE) {
            g_system_state.messages_sent++;
            hal_rtos_.semaphore_give(g_state_mutex);
        }

        ESP_LOGW(
            TAG, "Command 0x%02X dispatched to node 0x%02X", static_cast<uint8_t>(cmd), static_cast<uint8_t>(node_id));
    }
    else {
        if (hal_rtos_.semaphore_take(g_state_mutex, portMAX_DELAY) == pdTRUE) {
            g_system_state.messages_lost++;
            hal_rtos_.semaphore_give(g_state_mutex);
        }
        ESP_LOGE(
            TAG, "Failed to dispatch command to node 0x%02X: %s", static_cast<uint8_t>(node_id), esp_err_to_name(err));
    }
}

bool HubApp::set_pending_command(farm::NodeId node_id, espnow::CommandType cmd)
{
    // Check if already set
    for (auto& entry : stats_.pending_cmds) {
        if (entry.active && entry.node_id == node_id)
            return false;
    }
    // Find empty slot
    for (auto& entry : stats_.pending_cmds) {
        if (!entry.active) {
            entry = {true, node_id, cmd};
            hub_storage_.save_app_data(stats_);
            return true;
        }
    }
    ESP_LOGE(TAG, "No free slot for pending command (MAX_HUB_NODES=%d)", MAX_HUB_NODES);
    return false;
}

bool HubApp::has_pending_command(farm::NodeId node_id, espnow::CommandType& out_cmd)
{
    for (const auto& entry : stats_.pending_cmds) {
        if (entry.active && entry.node_id == node_id) {
            out_cmd = entry.command;
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