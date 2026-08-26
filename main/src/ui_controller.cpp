#include <stdio.h>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_timer.h"

#include "fonts/font8x12.hpp"
#include "fonts/font14x22_num.hpp"
#include "i_graphics_context.hpp"
#include "i18n/i18n.hpp"
#include "ui_snapshot.hpp"
#include "ui_events.hpp"
#include "app_commands.hpp"
#include "ui_controller.hpp"

static const char* TAG = "UIController";

// Forward declarations for helpers
static void format_compact_num(char* buf, size_t buf_size, uint32_t val);
static void format_elapsed_time(char* buf, size_t buf_size, int64_t last_update_ts);
static void format_dynamic_uptime(char* buf, size_t buf_size, uint32_t uptime_s);
static const char* sensor_status_to_string(farm::SensorStatus status);
static const char* battery_state_to_string(farm::BatteryState state);
static ScreenMode get_screen_for_node(farm::NodeId node);
static const char* get_node_name(farm::NodeId node);

UIController::UIController(
    IGraphicsContext& gfx,
    hub::INodeRegistry* node_registry,
    QueueHandle_t app_cmd_queue,
    espnow::IEspNowManager* espnow)
    : gfx_(gfx)
    , node_registry_(node_registry)
    , app_cmd_queue_(app_cmd_queue)
    , espnow_(espnow)
{
}

void UIController::handle_event(const UiEvent& event)
{
    switch (event.type) {
    case UiEventType::NAV_NEXT:
        if (current_screen_ == ScreenMode::NODE_SUBMENU) {
            submenu_index_ = (submenu_index_ + 1) % SUBMENU_TOTAL_ITEMS;
        }
        else if (current_screen_ == ScreenMode::MAIN_SCREEN) {
            current_screen_ = ScreenMode::WATER_TANK_SCREEN;
        }
        else if (current_screen_ == ScreenMode::WATER_TANK_SCREEN) {
            current_screen_ = ScreenMode::PUMP_SCREEN;
        }
        else if (current_screen_ == ScreenMode::PUMP_SCREEN) {
            current_screen_ = ScreenMode::SOLAR_SCREEN;
        }
        else if (current_screen_ == ScreenMode::SOLAR_SCREEN) {
            current_screen_ = ScreenMode::LOADS_SCREEN;
        }
        else if (current_screen_ == ScreenMode::LOADS_SCREEN) {
            current_screen_ = ScreenMode::STATS_SCREEN;
        }
        else if (current_screen_ == ScreenMode::STATS_SCREEN) {
            current_screen_ = ScreenMode::SETTINGS_SCREEN;
        }
        else if (current_screen_ == ScreenMode::SETTINGS_SCREEN) {
            settings_index_ = (settings_index_ + 1) % SETTINGS_TOTAL_ITEMS;
        }
        ESP_LOGI(
            TAG, "Navigated next -> screen %d (submenu idx %d)", static_cast<int>(current_screen_), submenu_index_);
        break;

    case UiEventType::NAV_PREV:
        if (current_screen_ == ScreenMode::NODE_SUBMENU) {
            submenu_index_ = (submenu_index_ - 1 + SUBMENU_TOTAL_ITEMS) % SUBMENU_TOTAL_ITEMS;
        }
        else if (current_screen_ == ScreenMode::MAIN_SCREEN) {
            current_screen_ = ScreenMode::SETTINGS_SCREEN;
        }
        else if (current_screen_ == ScreenMode::SETTINGS_SCREEN) {
            settings_index_ = (settings_index_ - 1 + SETTINGS_TOTAL_ITEMS) % SETTINGS_TOTAL_ITEMS;
        }
        else if (current_screen_ == ScreenMode::STATS_SCREEN) {
            current_screen_ = ScreenMode::LOADS_SCREEN;
        }
        else if (current_screen_ == ScreenMode::LOADS_SCREEN) {
            current_screen_ = ScreenMode::SOLAR_SCREEN;
        }
        else if (current_screen_ == ScreenMode::SOLAR_SCREEN) {
            current_screen_ = ScreenMode::PUMP_SCREEN;
        }
        else if (current_screen_ == ScreenMode::PUMP_SCREEN) {
            current_screen_ = ScreenMode::WATER_TANK_SCREEN;
        }
        else if (current_screen_ == ScreenMode::WATER_TANK_SCREEN) {
            current_screen_ = ScreenMode::MAIN_SCREEN;
        }
        ESP_LOGI(
            TAG, "Navigated prev -> screen %d (submenu idx %d)", static_cast<int>(current_screen_), submenu_index_);
        break;

    case UiEventType::CONFIRM:
        if (current_screen_ == ScreenMode::WATER_TANK_SCREEN) {
            active_node_ = farm::NodeId::WATER_TANK;
            current_screen_ = ScreenMode::NODE_SUBMENU;
            submenu_index_ = 0;
            ESP_LOGI(TAG, "Entered NODE_SUBMENU for WATER_TANK");
        }
        else if (current_screen_ == ScreenMode::PUMP_SCREEN) {
            active_node_ = farm::NodeId::PUMP_CONTROL;
            current_screen_ = ScreenMode::NODE_SUBMENU;
            submenu_index_ = 0;
            ESP_LOGI(TAG, "Entered NODE_SUBMENU for PUMP_CONTROL");
        }
        else if (current_screen_ == ScreenMode::SOLAR_SCREEN) {
            active_node_ = farm::NodeId::SOLAR_SENSOR;
            current_screen_ = ScreenMode::NODE_SUBMENU;
            submenu_index_ = 0;
            ESP_LOGI(TAG, "Entered NODE_SUBMENU for SOLAR_SENSOR");
        }
        else if (current_screen_ == ScreenMode::LOADS_SCREEN) {
            // General loads overview screen
        }
        else if (current_screen_ == ScreenMode::NODE_SUBMENU) {
            switch (static_cast<SubmenuItem>(submenu_index_)) {
            case SubmenuItem::LAST_REPORT:
                if (active_node_ == farm::NodeId::WATER_TANK) {
                    current_screen_ = ScreenMode::WATER_TANK_LAST_REPORT_SCREEN;
                    ESP_LOGI(TAG, "Entered WATER_TANK_LAST_REPORT_SCREEN");
                }
                else if (active_node_ == farm::NodeId::SOLAR_SENSOR) {
                    current_screen_ = ScreenMode::SOLAR_SENSOR_LAST_REPORT_SCREEN;
                    ESP_LOGI(TAG, "Entered SOLAR_SENSOR_LAST_REPORT_SCREEN");
                }
                else if (active_node_ == farm::NodeId::PUMP_CONTROL) {
                    current_screen_ = ScreenMode::PUMP_LAST_REPORT_SCREEN;
                    ESP_LOGI(TAG, "Entered PUMP_LAST_REPORT_SCREEN");
                }
                else {
                    current_screen_ = get_screen_for_node(active_node_);
                }
                break;
            case SubmenuItem::ESPNOW_STATS:
                current_screen_ = ScreenMode::NODE_STATS_SCREEN;
                ESP_LOGI(TAG, "Entered NODE_STATS_SCREEN for node %d", static_cast<int>(active_node_));
                break;
            case SubmenuItem::CONFIG:
                current_screen_ = ScreenMode::NODE_INFO_SCREEN;
                ESP_LOGI(TAG, "Entered NODE_INFO_SCREEN for node %d", static_cast<int>(active_node_));
                break;
            case SubmenuItem::START_OTA:
                if (app_cmd_queue_ != nullptr) {
                    AppCommand cmd;
                    cmd.espnow_cmd = espnow::CommandType::START_OTA;
                    cmd.target_node = active_node_;
                    cmd.param = 0;
                    cmd.requires_ack = true;
                    xQueueSend(app_cmd_queue_, &cmd, 0);
                    ESP_LOGI(TAG, "Queued START_OTA for node %d", static_cast<int>(active_node_));
                }
                current_screen_ = get_screen_for_node(active_node_);
                break;
            case SubmenuItem::REBOOT_NODE:
                if (app_cmd_queue_ != nullptr) {
                    AppCommand cmd;
                    cmd.espnow_cmd = espnow::CommandType::REBOOT;
                    cmd.target_node = active_node_;
                    cmd.param = 0;
                    cmd.requires_ack = true;
                    xQueueSend(app_cmd_queue_, &cmd, 0);
                    ESP_LOGI(TAG, "Queued REBOOT for node %d", static_cast<int>(active_node_));
                }
                current_screen_ = get_screen_for_node(active_node_);
                break;
            case SubmenuItem::BACK:
                current_screen_ = get_screen_for_node(active_node_);
                break;
            default:
                ESP_LOGI(TAG, "Executed submenu item %d for node %d", submenu_index_, static_cast<int>(active_node_));
                break;
            }
        }
        else if (
            current_screen_ == ScreenMode::NODE_STATS_SCREEN || current_screen_ == ScreenMode::NODE_INFO_SCREEN ||
            current_screen_ == ScreenMode::WATER_TANK_LAST_REPORT_SCREEN ||
            current_screen_ == ScreenMode::SOLAR_SENSOR_LAST_REPORT_SCREEN ||
            current_screen_ == ScreenMode::PUMP_LAST_REPORT_SCREEN) {
            current_screen_ = ScreenMode::NODE_SUBMENU;
        }
        else if (current_screen_ == ScreenMode::SETTINGS_SCREEN) {
            if (settings_index_ == 0) {
                Language new_lang = (I18n::get_language() == Language::EN_US) ? Language::PT_BR : Language::EN_US;
                I18n::set_language(new_lang);
                ESP_LOGI(TAG, "Toggled language -> %s", (new_lang == Language::PT_BR) ? "PT_BR" : "EN_US");
                if (app_cmd_queue_ != nullptr) {
                    AppCommand cmd;
                    cmd.espnow_cmd = static_cast<espnow::CommandType>(0xFE);
                    cmd.target_node = farm::NodeId::HUB;
                    cmd.param = static_cast<uint32_t>(new_lang);
                    xQueueSend(app_cmd_queue_, &cmd, 0);
                }
            }
            else if (settings_index_ == 1) {
                int64_t now_ms = esp_timer_get_time() / 1000;
                bool is_active = pairing_active_ && ((now_ms - pairing_start_ts_ms_) < 30000);
                if (!is_active) {
                    if (espnow_ != nullptr) {
                        espnow_->start_pairing(30000);
                    }
                    pairing_active_ = true;
                    pairing_start_ts_ms_ = now_ms;
                    ESP_LOGI(TAG, "Started ESP-NOW pairing (30s)");
                }
            }
        }
        break;

    case UiEventType::BACK:
        ESP_LOGI(TAG, "BACK pressed");
        if (current_screen_ == ScreenMode::NODE_SUBMENU) {
            current_screen_ = get_screen_for_node(active_node_);
        }
        else if (
            current_screen_ == ScreenMode::NODE_STATS_SCREEN || current_screen_ == ScreenMode::NODE_INFO_SCREEN ||
            current_screen_ == ScreenMode::WATER_TANK_LAST_REPORT_SCREEN ||
            current_screen_ == ScreenMode::SOLAR_SENSOR_LAST_REPORT_SCREEN ||
            current_screen_ == ScreenMode::PUMP_LAST_REPORT_SCREEN) {
            current_screen_ = ScreenMode::NODE_SUBMENU;
        }
        else {
            current_screen_ = ScreenMode::MAIN_SCREEN;
        }
        break;

    case UiEventType::BOOT_CLICK:
        ESP_LOGI(TAG, "BOOT_CLICK pressed");
        if (app_cmd_queue_ != nullptr) {
            AppCommand cmd{espnow::CommandType::REBOOT, farm::NodeId::HUB, 0};
            xQueueSend(app_cmd_queue_, &cmd, 0);
            ESP_LOGI(TAG, "Queued REBOOT for HUB");
        }
        break;
    }
}

// ===============================================================
// Render Methods
// ===============================================================

void UIController::render_current_screen(const UiSnapshotData& data)
{
    switch (current_screen_) {
    case ScreenMode::MAIN_SCREEN:
        render_main_screen(data);
        break;
    case ScreenMode::WATER_TANK_SCREEN:
        render_water_tank_screen(data);
        break;
    case ScreenMode::PUMP_SCREEN:
        render_pump_screen(data);
        break;
    case ScreenMode::NODE_SUBMENU:
        render_node_submenu(data);
        break;
    case ScreenMode::NODE_STATS_SCREEN:
        render_node_stats_screen(data);
        break;
    case ScreenMode::NODE_INFO_SCREEN:
        render_node_info_screen(data);
        break;
    case ScreenMode::WATER_TANK_LAST_REPORT_SCREEN:
        render_water_tank_last_report_screen(data);
        break;
    case ScreenMode::SOLAR_SENSOR_LAST_REPORT_SCREEN:
        render_solar_sensor_last_report_screen(data);
        break;
    case ScreenMode::PUMP_LAST_REPORT_SCREEN:
        render_pump_last_report_screen(data);
        break;
    case ScreenMode::SOLAR_SCREEN:
        render_solar_screen(data);
        break;
    case ScreenMode::LOADS_SCREEN:
        render_loads_screen(data);
        break;
    case ScreenMode::STATS_SCREEN:
        render_stats_screen(data);
        break;
    case ScreenMode::SETTINGS_SCREEN:
        render_settings_screen();
        break;
    case ScreenMode::BOOT_SCREEN:
        render_boot_screen();
        break;
    }
}

void UIController::render_main_screen(const UiSnapshotData& data)
{
    char buf[64];

    // --- Header ---
    gfx_.draw_string(0, 0, data.wifi_connected ? "[W]" : "[_]", 1);
    gfx_.draw_string_centered(0, I18n::get(StrId::HEADER_FARM_HUB), 1, 24, 90);
    gfx_.draw_string(90, 0, "[B]", 1);
    gfx_.draw_string(114, 0, "[_]", 1);

    // Separator
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    // --- Row 1: Level + Timestamp + Distance ---

    int64_t now_ms = esp_timer_get_time() / 1000;
    uint32_t elapsed_s = 0;
    if (data.last_water_update_ts > 0 && now_ms >= data.last_water_update_ts) {
        elapsed_s = static_cast<uint32_t>((now_ms - data.last_water_update_ts) / 1000);
    }
    uint32_t mm = elapsed_s / 60;
    uint32_t ss = elapsed_s % 60;

    float level_percent = data.water_level_permille / 10.0f;
    snprintf(
        buf,
        sizeof(buf),
        "  %.1f  %lu:%02lu   %.1f",
        level_percent,
        static_cast<unsigned long>(mm),
        static_cast<unsigned long>(ss),
        data.water_distance_cm);
    gfx_.draw_string(0, 12, buf, 1);

    // --- Row 2: Visual bar ---
    gfx_.draw_rect(0, 22, 128, 8, 1);
    int fill_width = (data.water_level_permille * 126) / 1000;
    for (int i = 0; i < fill_width; ++i) {
        gfx_.draw_vline(1 + i, 23, 6, 1);
    }

    // --- Row 3: WiFi Status ---
    snprintf(buf, sizeof(buf), "WiFi: %-3s    R: %d", data.wifi_connected ? "ON" : "OFF", data.wifi_rssi);
    gfx_.draw_string(0, 34, buf, 1);

    // --- Row 4: Network Status ---
    gfx_.draw_string(0, 44, "E-NOW Node Detail", 1);

    // --- Row 5: Stats Placeholder ---
    gfx_.draw_string(0, 54, "Stats: via node screens", 1);
}

void UIController::render_water_tank_screen(const UiSnapshotData& data)
{
    char buf[64];
    uint8_t x_pos = 0;
    uint8_t y_pos = 0;
    uint8_t str_width = 0;

    // --- Header (y=0..7) ---
    draw_wifi_signal_icon(x_pos, y_pos, data.wifi_connected, data.wifi_rssi);
    gfx_.draw_string_centered(x_pos, I18n::get(StrId::HEADER_WATER_TANK), 1);
    draw_battery_icon(112, y_pos, data.water_battery_percent);

    y_pos = 8;
    gfx_.draw_hline(0, y_pos, gfx_.get_width(), 1);

    if (data.water_backup_mode) {
        // --- Backup Mode Warning Display (y=12..35) ---
        gfx_.draw_string_centered(12, I18n::get(StrId::LABEL_SENSOR_FAILED), 1, 0, -1, &font8x12);

        const char* float_state_str =
            data.water_float_switch_full ? I18n::get(StrId::LABEL_FLOAT_FULL) : I18n::get(StrId::LABEL_FLOAT_EMPTY);
        snprintf(buf, sizeof(buf), "%s: %s", I18n::get(StrId::LABEL_FLOAT), float_state_str);
        gfx_.draw_string_centered(25, buf, 1, 0, -1, &font8x12);

        // --- Visual Level Bar Frame (Empty in backup mode) ---
        y_pos = 37;
        uint8_t bar_height = 6;
        uint8_t bar_width = 124;
        uint8_t bar_offset_x = (gfx_.get_width() - bar_width) / 2;

        gfx_.draw_rect(bar_offset_x, y_pos, bar_width, bar_height, 1);
        uint8_t quarter_width = bar_width / 4;
        gfx_.draw_vline(bar_offset_x + quarter_width, y_pos, bar_height, 1);
        gfx_.draw_vline(bar_offset_x + 2 * quarter_width, y_pos, bar_height, 1);
        gfx_.draw_vline(bar_offset_x + 3 * quarter_width, y_pos, bar_height, 1);
    }
    else {
        // --- Normal Primary Level Display (y=12..33, font14x22_num) ---
        y_pos = 12;
        float level_percent = data.water_level_permille / 10.0f;
        snprintf(buf, sizeof(buf), "%.1f%%", level_percent);
        gfx_.draw_string_centered(y_pos, buf, 1, 0, -1, &font14x22_num);

        // --- Visual Level Bar (y=37, 124x6px) ---
        y_pos = 37;
        uint8_t bar_height = 6;
        uint8_t bar_width = 124;
        uint8_t bar_offset_x = (gfx_.get_width() - bar_width) / 2;

        gfx_.draw_rect(bar_offset_x, y_pos, bar_width, bar_height, 1);
        int fill_width = (data.water_level_permille * (bar_width - 2)) / 1000;
        if (fill_width > 0) {
            if (fill_width > bar_width - 2)
                fill_width = bar_width - 2;
            gfx_.fill_rect(bar_offset_x + 1, y_pos + 1, fill_width, bar_height - 2, 1);
        }
        uint8_t quarter_width = bar_width / 4;
        gfx_.draw_vline(bar_offset_x + quarter_width, y_pos, bar_height, 1);
        gfx_.draw_vline(bar_offset_x + 2 * quarter_width, y_pos, bar_height, 1);
        gfx_.draw_vline(bar_offset_x + 3 * quarter_width, y_pos, bar_height, 1);
    }

    // --- Footer Line 1 (y=47): Reading Status (Left) + Elapsed Time (Right) ---
    x_pos = 0;
    y_pos = 47;

    const char* status_str = sensor_status_to_string(data.water_sensor_status);
    snprintf(buf, sizeof(buf), "%s: %s", I18n::get(StrId::LABEL_READING), status_str);
    gfx_.draw_string(x_pos, y_pos, buf, 1);

    char time_buf[16];
    format_elapsed_time(time_buf, sizeof(time_buf), data.last_water_update_ts);
    int time_w = gfx_.get_string_width(time_buf);
    gfx_.draw_string(gfx_.get_width() - time_w, y_pos, time_buf, 1);

    // --- Footer Line 2 (y=57): Float & Sensor Indicators ---
    x_pos = 0;
    y_pos = 57;

    snprintf(buf, sizeof(buf), "%s:", I18n::get(StrId::LABEL_FLOAT));
    str_width = gfx_.get_string_width(buf);

    gfx_.draw_string(x_pos, y_pos, buf, 1);

    bool float_demanding = !data.water_float_switch_full;
    if (float_demanding) {
        gfx_.fill_rect(x_pos + str_width + 2, y_pos, 7, 7, 1);
    }
    else {
        gfx_.draw_rect(x_pos + str_width + 2, y_pos, 7, 7, 1);
    }

    snprintf(buf, sizeof(buf), "%s:", I18n::get(StrId::LABEL_SENSOR));
    str_width = gfx_.get_string_width(buf);

    int square_x = gfx_.get_width() - 7;
    int text_x = square_x - 2 - str_width;

    gfx_.draw_string(text_x, y_pos, buf, 1);
    bool sensor_ok = !data.water_backup_mode;
    if (sensor_ok) {
        gfx_.fill_rect(square_x, y_pos, 7, 7, 1);
    }
    else {
        gfx_.draw_rect(square_x, y_pos, 7, 7, 1);
    }
}

void UIController::render_pump_screen(const UiSnapshotData& data)
{
    char buf[64];
    uint8_t y_pos = 0;

    // --- Header (y=0..7) ---
    // Wifi hub status
    draw_wifi_signal_icon(0, y_pos, data.wifi_connected, data.wifi_rssi);
    // Pump header text
    gfx_.draw_string_centered(y_pos, I18n::get(StrId::HEADER_PUMP), 1, 24, 128 - 24);
    // ESP-NOW node pump status
    bool is_online = false;
    int8_t rssi = 0;
    if (espnow_ != nullptr) {
        is_online = espnow_->is_peer_online(static_cast<espnow::NodeId>(farm::NodeId::PUMP_CONTROL));
        espnow::PeerStatistics stats{};
        if (espnow_->get_peer_stats(static_cast<espnow::NodeId>(farm::NodeId::PUMP_CONTROL), stats) == ESP_OK) {
            rssi = stats.rssi_last;
        }
    }
    draw_wifi_signal_icon(gfx_.get_width() - 12, 0, is_online, rssi);
    // Divider
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    const auto& pump = data.load(LoadIndex::PUMP);

    // --- Main Status (y=16) ---
    const char* status_str = I18n::get(StrId::STATUS_IDLE);
    if (pump.load_state == farm::LoadState::RUNNING) {
        status_str = I18n::get(StrId::STATUS_RUNNING);
    }
    else if (pump.load_state == farm::LoadState::ERROR_TIMEOUT) {
        status_str = I18n::get(StrId::STATUS_TIMEOUT);
    }
    else if (
        pump.load_state == farm::LoadState::ERROR_NO_SOURCE ||
        pump.load_state == farm::LoadState::ERROR_CONTACTOR_STUCK) {
        status_str = I18n::get(StrId::STATUS_FAULT);
    }

    if (pump.load_state == farm::LoadState::RUNNING && pump.power_w > 0) {
        snprintf(buf, sizeof(buf), "%s (%uW)", status_str, pump.power_w);
    }
    else {
        snprintf(buf, sizeof(buf), "%s", status_str);
    }
    gfx_.draw_string_centered(16, buf, 1);

    // --- Line 1 (y=32): Mode Indicators (Auto:[ ]  Lock:[ ]  Man:[ ]) ---
    y_pos = 32;
    bool is_auto = (pump.control_mode == farm::ControlMode::AUTO);
    bool is_lock = (pump.control_mode == farm::ControlMode::SOURCE_LOCKED);
    bool is_man =
        (pump.control_mode == farm::ControlMode::STOP_OVERRIDE || pump.control_mode == farm::ControlMode::FULL_MANUAL);

    // Auto (Left at x=0)
    snprintf(buf, sizeof(buf), "%s", I18n::get(StrId::LABEL_AUTO));
    int auto_w = gfx_.get_string_width(buf);
    gfx_.draw_string(0, y_pos, buf, 1);
    if (is_auto) {
        gfx_.fill_rect(auto_w + 2, y_pos, 7, 7, 1);
    }
    else {
        gfx_.draw_rect(auto_w + 2, y_pos, 7, 7, 1);
    }

    // Lock / Trav (Center at x=50)
    snprintf(buf, sizeof(buf), "%s", I18n::get(StrId::LABEL_LOCK));
    int lock_w = gfx_.get_string_width(buf);
    int lock_x = 50;
    gfx_.draw_string(lock_x, y_pos, buf, 1);
    if (is_lock) {
        gfx_.fill_rect(lock_x + lock_w + 2, y_pos, 7, 7, 1);
    }
    else {
        gfx_.draw_rect(lock_x + lock_w + 2, y_pos, 7, 7, 1);
    }

    // Man (Right aligned)
    snprintf(buf, sizeof(buf), "%s", I18n::get(StrId::LABEL_MAN));
    int man_w = gfx_.get_string_width(buf);
    int square_x = gfx_.get_width() - 7;
    int text_x = square_x - 2 - man_w;
    gfx_.draw_string(text_x, y_pos, buf, 1);
    if (is_man) {
        gfx_.fill_rect(square_x, y_pos, 7, 7, 1);
    }
    else {
        gfx_.draw_rect(square_x, y_pos, 7, 7, 1);
    }

    // --- Line 2 (y=43): Source Indicators (Solar:[ ]  rid:[ ]) ---
    y_pos = 43;
    bool is_solar = (pump.active_source == farm::PowerSource::SOLAR);
    bool is_grid = (pump.active_source == farm::PowerSource::GRID);

    // Solar (Left at x=0)
    snprintf(buf, sizeof(buf), "%s", I18n::get(StrId::LABEL_SOLAR));
    int sol_w = gfx_.get_string_width(buf);
    gfx_.draw_string(0, y_pos, buf, 1);
    if (is_solar) {
        gfx_.fill_rect(sol_w + 2, y_pos, 7, 7, 1);
    }
    else {
        gfx_.draw_rect(sol_w + 2, y_pos, 7, 7, 1);
    }

    // Grid  (Right aligned)
    snprintf(buf, sizeof(buf), "%s", I18n::get(StrId::LABEL_GRID));
    int grid_w = gfx_.get_string_width(buf);
    square_x = gfx_.get_width() - 7;
    text_x = square_x - 2 - grid_w;
    gfx_.draw_string(text_x, y_pos, buf, 1);
    if (is_grid) {
        gfx_.fill_rect(square_x, y_pos, 7, 7, 1);
    }
    else {
        gfx_.draw_rect(square_x, y_pos, 7, 7, 1);
    }

    // --- Line 3 (y=54): Runtime & Age ---
    y_pos = 54;
    uint32_t runtime_s = pump.runtime_s;
    if (runtime_s > 0) {
        uint32_t hours = runtime_s / 3600;
        uint32_t mins = (runtime_s % 3600) / 60;
        uint32_t secs = runtime_s % 60;
        snprintf(
            buf,
            sizeof(buf),
            "%s: %02lu:%02lu:%02lu",
            I18n::get(StrId::LABEL_RUNTIME),
            static_cast<unsigned long>(hours),
            static_cast<unsigned long>(mins),
            static_cast<unsigned long>(secs));
    }
    else {
        snprintf(buf, sizeof(buf), "%s: --:--:--", I18n::get(StrId::LABEL_RUNTIME));
    }
    gfx_.draw_string(0, y_pos, buf, 1);

    char time_buf[16];
    format_elapsed_time(time_buf, sizeof(time_buf), pump.last_update_ts);
    int time_w = gfx_.get_string_width(time_buf);
    gfx_.draw_string(gfx_.get_width() - time_w, y_pos, time_buf, 1);
}

void UIController::render_node_submenu(const UiSnapshotData& data)
{
    (void)data;
    const char* node_name = get_node_name(active_node_);
    char title_buf[32];
    snprintf(title_buf, sizeof(title_buf), "%s MENU", node_name);
    gfx_.draw_string_centered(0, title_buf, 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    const char* menu_items[SUBMENU_TOTAL_ITEMS] = {
        I18n::get(StrId::MENU_LAST_REPORT),
        I18n::get(StrId::MENU_ESPNOW_STATS),
        I18n::get(StrId::MENU_REQUEST_REPORT),
        I18n::get(StrId::MENU_CONFIG),
        I18n::get(StrId::MENU_CLEAR_STATS),
        I18n::get(StrId::MENU_REBOOT_NODE),
        I18n::get(StrId::MENU_START_OTA),
        I18n::get(StrId::MENU_BACK)};

    static constexpr int VISIBLE_ITEMS = 4;
    int top_index = submenu_index_ - (VISIBLE_ITEMS - 1);
    if (top_index < 0)
        top_index = 0;

    int y = 14;
    for (int i = 0; i < VISIBLE_ITEMS && (top_index + i) < SUBMENU_TOTAL_ITEMS; ++i) {
        int item_idx = top_index + i;
        bool is_selected = (item_idx == submenu_index_);
        gfx_.draw_string(4, y, menu_items[item_idx], 1, nullptr, is_selected);
        y += 12;
    }

    if (top_index > 0) {
        gfx_.draw_char(gfx_.get_width() - 8, 14, '^', 1);
    }
    if ((top_index + VISIBLE_ITEMS) < SUBMENU_TOTAL_ITEMS) {
        gfx_.draw_char(gfx_.get_width() - 8, 50, 'v', 1);
    }
}

void UIController::render_node_stats_screen(const UiSnapshotData& data)
{
    (void)data;
    const char* node_name = get_node_name(active_node_);

    char left_buf[32];
    char right_buf[32];
    char num_buf[16];

    // --- Header ---
    snprintf(left_buf, sizeof(left_buf), "%s STATS", node_name);
    gfx_.draw_string_centered(0, left_buf, 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    espnow::PeerStatistics stats{};
    if (espnow_ != nullptr) {
        espnow_->get_peer_stats(static_cast<espnow::NodeId>(active_node_), stats);
    }

    uint8_t collumn_a_x_pos = 0;
    uint8_t collumn_b_x_pos = 64;

    // --- Line 1 (y=12): RSSI ---
    uint8_t y_pos = 12;
    snprintf(left_buf, sizeof(left_buf), "RSSI: %d", stats.rssi_last);
    snprintf(right_buf, sizeof(right_buf), "Avg: %d", stats.rssi_avg);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 2 (y=22): Packets Rx & Tx ---
    y_pos = 22;
    format_compact_num(num_buf, sizeof(num_buf), stats.packets_rx);
    snprintf(left_buf, sizeof(left_buf), "Rx: %s", num_buf);
    format_compact_num(num_buf, sizeof(num_buf), stats.packets_sent);
    snprintf(right_buf, sizeof(right_buf), "Tx: %s", num_buf);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 3 (y=32): Lost & Retry ---
    y_pos = 32;
    format_compact_num(num_buf, sizeof(num_buf), stats.packets_lost);
    snprintf(left_buf, sizeof(left_buf), "Lost: %s", num_buf);
    format_compact_num(num_buf, sizeof(num_buf), stats.retries);
    snprintf(right_buf, sizeof(right_buf), "Retry: %s", num_buf);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 4 (y=42): RTT & Avg RTT ---
    y_pos = 42;
    uint32_t rtt_last_ms = stats.rtt_last_us / 1000;
    uint32_t rtt_avg_ms = stats.rtt_avg_us / 1000;
    format_compact_num(num_buf, sizeof(num_buf), rtt_last_ms);
    snprintf(left_buf, sizeof(left_buf), "RTT: %sms", num_buf);
    format_compact_num(num_buf, sizeof(num_buf), rtt_avg_ms);
    snprintf(right_buf, sizeof(right_buf), "Avg: %sms", num_buf);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 5 (y=52): Driver Fail & Errors ---
    y_pos = 52;
    format_compact_num(num_buf, sizeof(num_buf), stats.delivery_failures);
    snprintf(left_buf, sizeof(left_buf), "Fail: %s", num_buf);
    format_compact_num(num_buf, sizeof(num_buf), stats.driver_errors);
    snprintf(right_buf, sizeof(right_buf), "Err: %s", num_buf);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);
}

void UIController::render_node_info_screen(const UiSnapshotData& data)
{
    (void)data;
    const char* node_name = get_node_name(active_node_);
    char title_buf[32];
    char buf[32];

    // --- Header ---
    snprintf(title_buf, sizeof(title_buf), "%s INFO", node_name);
    gfx_.draw_string_centered(0, title_buf, 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    // --- Lookup NodeMetadata from NodeRegistry ---
    farm::NodeMetadata meta{};
    bool meta_found = false;
    if (node_registry_ != nullptr) {
        meta_found = node_registry_->get_node_info(active_node_, meta);
    }

    // --- Lookup PeerInfo from EspNow ---
    // TODO: Implement get_peer(NodeId, PeerInfo&) in IEspNowManager to avoid copying full peers vector
    uint8_t peer_mac[6] = {};
    bool peer_found = false;
    if (espnow_ != nullptr) {
        auto peers = espnow_->get_peers();
        for (const auto& p : peers) {
            if (p.node_id == static_cast<espnow::NodeId>(active_node_)) {
                memcpy(peer_mac, p.mac, 6);
                peer_found = true;
                break;
            }
        }
    }

    // --- Line 1 (y=13): Node ID ---
    uint8_t y_pos = 15;
    snprintf(buf, sizeof(buf), "Node ID: 0x%02X", static_cast<uint8_t>(active_node_));
    gfx_.draw_string(0, y_pos, buf, 1);

    // --- Line 2 (y=25): FW Version ---
    y_pos += 13;
    if (meta_found) {
        snprintf(buf, sizeof(buf), "FW: v%u.%u.%u", meta.fw_major, meta.fw_minor, meta.fw_patch);
    }
    else {
        snprintf(buf, sizeof(buf), "FW: --");
    }
    gfx_.draw_string(0, y_pos, buf, 1);

    // --- Line 3 (y=37): Power Profile ---
    y_pos += 13;
    const char* power_str = "--";
    if (meta_found) {
        switch (meta.power_profile) {
        case farm::PowerProfile::ALWAYS_ON:
            power_str = "ALWAYS_ON";
            break;
        case farm::PowerProfile::LOW_POWER:
            power_str = "LOW_POWER";
            break;
        case farm::PowerProfile::DEEP_SLEEP:
            power_str = "DEEP_SLEEP";
            break;
        }
    }
    snprintf(buf, sizeof(buf), "Power: %s", power_str);
    gfx_.draw_string(0, y_pos, buf, 1);

    // --- Line 4 (y=49): MAC Address ---
    y_pos += 13;
    if (peer_found) {
        snprintf(
            buf,
            sizeof(buf),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            peer_mac[0],
            peer_mac[1],
            peer_mac[2],
            peer_mac[3],
            peer_mac[4],
            peer_mac[5]);
    }
    else {
        snprintf(buf, sizeof(buf), "MAC: --");
    }
    gfx_.draw_string(0, y_pos, buf, 1);
}

void UIController::render_water_tank_last_report_screen(const UiSnapshotData& data)
{
    char left_buf[32];
    char right_buf[32];

    // --- Header (y=0..7) ---
    gfx_.draw_string_centered(0, "[WT] LAST REPORT", 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    uint8_t collumn_a_x_pos = 0;
    uint8_t collumn_b_x_pos = 64;

    uint8_t inter_line_space = 11;
    // --- Line 1: Permille & Distance ---
    uint8_t y_pos = 12;
    snprintf(left_buf, sizeof(left_buf), "Lv: %u", data.water_level_permille);
    snprintf(right_buf, sizeof(right_buf), "D: %.1fcm", data.water_distance_cm);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 2: Raw Battery Voltage (mV) & Percentage ---
    y_pos += inter_line_space;
    snprintf(left_buf, sizeof(left_buf), "B: %umV", data.water_battery_mv);
    snprintf(right_buf, sizeof(right_buf), "(%u%%)", data.water_battery_percent);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 3: Sensor Status & Operating Mode ---
    y_pos += inter_line_space;
    snprintf(left_buf, sizeof(left_buf), "Sens: %s", sensor_status_to_string(data.water_sensor_status));
    const char* mode_str = data.water_backup_mode ? "BACKUP" : "NORM";
    snprintf(right_buf, sizeof(right_buf), "Mode: %s", mode_str);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 4: Float switch (Left) + Elapsed Age MM:SS (Right-aligned) ---
    y_pos += inter_line_space;
    const char* float_str =
        data.water_float_switch_full ? I18n::get(StrId::LABEL_FLOAT_FULL) : I18n::get(StrId::LABEL_FLOAT_EMPTY);
    snprintf(left_buf, sizeof(left_buf), "%s: %s", I18n::get(StrId::LABEL_FLOAT), float_str);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);

    format_elapsed_time(right_buf, sizeof(right_buf), data.last_water_update_ts);
    int age_w = gfx_.get_string_width(right_buf);
    gfx_.draw_string(gfx_.get_width() - age_w, y_pos, right_buf, 1);

    // --- Line 5: Raw Unix Timestamp ---
    y_pos += inter_line_space;
    snprintf(left_buf, sizeof(left_buf), "T: %llu", static_cast<unsigned long long>(data.water_node_unix_time / 1000));
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
}

void UIController::render_solar_sensor_last_report_screen(const UiSnapshotData& data)
{
    gfx_.draw_string_centered(0, I18n::get(StrId::HEADER_SOLAR_REPORT), 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    char left_buf[32];
    char right_buf[32];
    char num_buf[16];
    uint8_t collumn_a_x_pos = 0;
    uint8_t collumn_b_x_pos = 64;

    uint8_t inter_line_space = 11;
    // --- Line 1: Irradiance (Left) + Isc (Right) ---
    uint8_t y_pos = 12;
    snprintf(left_buf, sizeof(left_buf), "Irr: %u", data.solar_irradiance_wm2);
    snprintf(right_buf, sizeof(right_buf), "Isc: %u", data.solar_isc_current_ma);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 2: Estimated Power (Left) + Battery mV (Right) ---
    y_pos += inter_line_space;
    snprintf(left_buf, sizeof(left_buf), "P: %uW", data.solar_power_w_instant);
    snprintf(right_buf, sizeof(right_buf), "Max: %u", data.solar_max_current_ma);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 3: Raw Battery Voltage (mV) & Percentage ---
    y_pos += inter_line_space;
    snprintf(left_buf, sizeof(left_buf), "B: %umV", data.solar_battery_mv);
    snprintf(right_buf, sizeof(right_buf), "(%u%%)", data.solar_battery_percent);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);

    // --- Line 4: Panel Temp (Left) + Elapsed Age MM:SS (Right) ---
    y_pos += inter_line_space;
    if (data.solar_panel_temp_c != INT16_MIN) {
        snprintf(left_buf, sizeof(left_buf), "T: %.1fC", data.solar_panel_temp_c / 10.0f);
    }
    else {
        snprintf(left_buf, sizeof(left_buf), "T: --");
    }
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);

    format_elapsed_time(right_buf, sizeof(right_buf), data.last_solar_update_ts);
    int age_w = gfx_.get_string_width(right_buf);
    gfx_.draw_string(gfx_.get_width() - age_w, y_pos, right_buf, 1);

    // --- Line 5: Hub Wh Yield (Left) + Sensor status (Right) ---
    y_pos += inter_line_space;
    format_compact_num(num_buf, sizeof(num_buf), data.solar_daily_yield_wh_hub);
    snprintf(left_buf, sizeof(left_buf), "Hub: %sWh", num_buf);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);

    snprintf(right_buf, sizeof(right_buf), "Sens: %s", sensor_status_to_string(data.solar_sensor_status));
    gfx_.draw_string(collumn_b_x_pos, y_pos, right_buf, 1);
}

void UIController::render_pump_last_report_screen(const UiSnapshotData& data)
{
    gfx_.draw_string_centered(0, I18n::get(StrId::HEADER_PUMP_REPORT), 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    char left_buf[32];
    char right_buf[32];
    uint8_t collumn_a_x_pos = 0;
    uint8_t collumn_b_x_pos = 64;

    uint8_t inter_line_space = 11;
    uint8_t y_pos = 12;

    const auto& pump = data.load(LoadIndex::PUMP);

    // --- Line 1: Status (Left) + Power W (Right) ---
    const char* status_str = I18n::get(StrId::STATUS_IDLE);
    if (pump.load_state == farm::LoadState::RUNNING) {
        status_str = I18n::get(StrId::STATUS_RUNNING);
    }
    else if (pump.load_state == farm::LoadState::ERROR_TIMEOUT) {
        status_str = I18n::get(StrId::STATUS_TIMEOUT);
    }
    else if (
        pump.load_state == farm::LoadState::ERROR_NO_SOURCE ||
        pump.load_state == farm::LoadState::ERROR_CONTACTOR_STUCK) {
        status_str = I18n::get(StrId::STATUS_FAULT);
    }
    snprintf(left_buf, sizeof(left_buf), "%s", status_str);
    snprintf(right_buf, sizeof(right_buf), "%uW", pump.power_w);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);

    uint8_t str_w = gfx_.get_string_width(right_buf);
    gfx_.draw_string(gfx_.get_width() - str_w, y_pos, right_buf, 1);

    // --- Line 2: Mode (Left) + Source (Right) ---
    y_pos += inter_line_space;
    const char* mode_str = "UNK";
    switch (pump.control_mode) {
    case farm::ControlMode::AUTO:
        mode_str = I18n::get(StrId::LABEL_AUTO);
        break;
    case farm::ControlMode::SOURCE_LOCKED:
    case farm::ControlMode::STOP_OVERRIDE:
        mode_str = I18n::get(StrId::LABEL_LOCK);
        break;
        break;
    case farm::ControlMode::FULL_MANUAL:
        mode_str = I18n::get(StrId::LABEL_MAN);
        break;
    default:
        break;
    }
    const char* src_str = (pump.active_source == farm::PowerSource::SOLAR)  ? I18n::get(StrId::LABEL_SOLAR)
                          : (pump.active_source == farm::PowerSource::GRID) ? I18n::get(StrId::LABEL_GRID)
                                                                            : " ";
    snprintf(left_buf, sizeof(left_buf), "Mode:%s", mode_str);
    snprintf(right_buf, sizeof(right_buf), "%s", src_str);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);

    str_w = gfx_.get_string_width(right_buf);
    gfx_.draw_string(gfx_.get_width() - str_w, y_pos, right_buf, 1);

    // --- Line 3: Runtime (Left) + Circuit ID (Right) ---
    y_pos += inter_line_space;
    uint32_t runtime_s = pump.runtime_s;
    if (runtime_s > 0) {
        uint32_t hours = runtime_s / 3600;
        uint32_t mins = (runtime_s % 3600) / 60;
        uint32_t secs = runtime_s % 60;
        snprintf(
            left_buf,
            sizeof(left_buf),
            "%02lu:%02lu:%02lu",
            static_cast<unsigned long>(hours),
            static_cast<unsigned long>(mins),
            static_cast<unsigned long>(secs));
    }
    else {
        snprintf(left_buf, sizeof(left_buf), "--:--:--");
    }
    snprintf(right_buf, sizeof(right_buf), "Ckt: %u", pump.circuit_id);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);

    str_w = gfx_.get_string_width(right_buf);
    gfx_.draw_string(gfx_.get_width() - str_w, y_pos, right_buf, 1);

    // --- Line 4: Uptime (Left) + Age MM:SS (Right-aligned) ---
    y_pos += inter_line_space;
    char up_buf[16];
    format_dynamic_uptime(up_buf, sizeof(up_buf), pump.uptime_s);
    snprintf(left_buf, sizeof(left_buf), "Up: %s", up_buf);
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);

    format_elapsed_time(right_buf, sizeof(right_buf), pump.last_update_ts);
    int age_w = gfx_.get_string_width(right_buf);
    gfx_.draw_string(gfx_.get_width() - age_w, y_pos, right_buf, 1);

    // --- Line 5: Raw Unix Timestamp ---
    y_pos += inter_line_space;
    snprintf(left_buf, sizeof(left_buf), "T: %llu", static_cast<unsigned long long>(pump.unix_time / 1000));
    gfx_.draw_string(collumn_a_x_pos, y_pos, left_buf, 1);
}

void UIController::render_solar_screen(const UiSnapshotData& data)
{
    char buf[64];
    char num_buf[16];
    uint8_t x_pos = 0;
    uint8_t y_pos = 0;

    // --- Header (y=0..7) ---
    draw_wifi_signal_icon(x_pos, y_pos, data.wifi_connected, data.wifi_rssi);
    gfx_.draw_string_centered(x_pos, I18n::get(StrId::HEADER_SOLAR), 1);
    draw_battery_icon(112, y_pos, data.solar_battery_percent);

    y_pos = 8;
    gfx_.draw_hline(0, y_pos, gfx_.get_width(), 1);

    if (data.is_solar_night()) {
        // --- Night Mode Display (y=18..36) ---
        gfx_.draw_string_centered(18, I18n::get(StrId::SOLAR_LABEL_NIGHT), 1, 0, -1, &font8x12);
        snprintf(buf, sizeof(buf), "Bat: %u mV (%u%%)", data.solar_battery_mv, data.solar_battery_percent);
        gfx_.draw_string_centered(32, buf, 1);
    }
    else {
        // --- Normal Primary Power Display (y=12..33, font14x22_num) ---
        y_pos = 12;
        snprintf(buf, sizeof(buf), "%u", data.solar_power_w_instant);
        gfx_.draw_string_centered(y_pos, buf, 1, 0, -1, &font14x22_num);

        // --- Metrics Line (y=36): Irradiance (Left) + Temperature (Right) ---
        y_pos = 36;
        snprintf(buf, sizeof(buf), "%u W/m2", data.solar_irradiance_wm2);
        gfx_.draw_string(0, y_pos, buf, 1);

        if (data.solar_panel_temp_c != INT16_MIN) {
            snprintf(buf, sizeof(buf), "%.1f C", data.solar_panel_temp_c / 10.0f);
            int temp_w = gfx_.get_string_width(buf);
            gfx_.draw_string(gfx_.get_width() - temp_w, y_pos, buf, 1);
        }
    }

    // --- Footer Line 1 (y=47): Reading Status (Left) + Elapsed Age (Right) ---
    x_pos = 0;
    y_pos = 47;

    const char* status_str = sensor_status_to_string(data.solar_sensor_status);
    snprintf(buf, sizeof(buf), "%s: %s", I18n::get(StrId::LABEL_READING), status_str);
    gfx_.draw_string(x_pos, y_pos, buf, 1);

    char time_buf[16];
    format_elapsed_time(time_buf, sizeof(time_buf), data.last_solar_update_ts);
    int time_w = gfx_.get_string_width(time_buf);
    gfx_.draw_string(gfx_.get_width() - time_w, y_pos, time_buf, 1);

    // --- Footer Line 2 (y=57): Isc Current (Left) + Hub Daily Yield (Right) ---
    x_pos = 0;
    y_pos = 57;
    snprintf(buf, sizeof(buf), "Isc: %u mA", data.solar_isc_current_ma);
    gfx_.draw_string(x_pos, y_pos, buf, 1);

    format_compact_num(num_buf, sizeof(num_buf), data.solar_daily_yield_wh_hub);
    snprintf(buf, sizeof(buf), "%sWh", num_buf);
    int yield_w = gfx_.get_string_width(buf);
    gfx_.draw_string(gfx_.get_width() - yield_w, y_pos, buf, 1);
}

void UIController::render_loads_screen(const UiSnapshotData& data)
{
    char buf[64];
    gfx_.draw_string(0, 0, "[W]", 1);
    gfx_.draw_string_centered(0, I18n::get(StrId::HEADER_LOADS), 1, 24, 128);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    uint16_t total_w = data.total_solar_consumption_w();
    int16_t margin = data.power_margin_w();

    snprintf(buf, sizeof(buf), "Solar Load: %u W", total_w);
    gfx_.draw_string(0, 14, buf, 1);

    snprintf(buf, sizeof(buf), "Margin:     %d W", margin);
    gfx_.draw_string(0, 26, buf, 1);

    const auto& pump = data.load(LoadIndex::PUMP);
    const char* mode_str = "UNK";
    switch (pump.control_mode) {
    case farm::ControlMode::AUTO:
        mode_str = "AUTO";
        break;
    case farm::ControlMode::SOURCE_LOCKED:
        mode_str = "LOCK";
        break;
    case farm::ControlMode::STOP_OVERRIDE:
        mode_str = "STOP";
        break;
    case farm::ControlMode::FULL_MANUAL:
        mode_str = "MAN";
        break;
    default:
        break;
    }
    const char* src_str = (pump.active_source == farm::PowerSource::SOLAR)  ? "SOLAR"
                          : (pump.active_source == farm::PowerSource::GRID) ? "GRID"
                                                                            : "UNK";

    snprintf(buf, sizeof(buf), "Pump: %s/%s %uW", mode_str, src_str, pump.power_w);
    gfx_.draw_string(0, 42, buf, 1);
}

void UIController::render_stats_screen(const UiSnapshotData& data)
{
    (void)data;
    gfx_.draw_string_centered(0, I18n::get(StrId::HEADER_STATS), 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);
}

void UIController::render_settings_screen()
{
    gfx_.draw_string_centered(0, I18n::get(StrId::HEADER_SETTINGS), 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    int64_t now_ms = esp_timer_get_time() / 1000;
    int remaining_s = 0;
    if (pairing_active_) {
        int64_t elapsed_ms = now_ms - pairing_start_ts_ms_;
        if (elapsed_ms < 30000) {
            remaining_s = static_cast<int>((30000 - elapsed_ms + 999) / 1000);
        }
        else {
            pairing_active_ = false;
        }
    }

    char lang_buf[64];
    bool is_en = (I18n::get_language() == Language::EN_US);
    const char* lang_str = is_en ? I18n::get(StrId::SETTINGS_LANG_EN) : I18n::get(StrId::SETTINGS_LANG_PT);
    snprintf(lang_buf, sizeof(lang_buf), "1. %s: %s", I18n::get(StrId::SETTINGS_LANGUAGE), lang_str);
    gfx_.draw_string(4, 18, lang_buf, 1, nullptr, (settings_index_ == 0));

    char pairing_buf[64];
    if (remaining_s > 0) {
        snprintf(
            pairing_buf, sizeof(pairing_buf), "2. %s: %ds", I18n::get(StrId::SETTINGS_PAIRING_ACTIVE), remaining_s);
    }
    else {
        snprintf(pairing_buf, sizeof(pairing_buf), "2. %s", I18n::get(StrId::SETTINGS_PAIRING));
    }
    gfx_.draw_string(4, 34, pairing_buf, 1, nullptr, (settings_index_ == 1));
}

void UIController::render_boot_screen()
{
    gfx_.draw_string_centered(26, I18n::get(StrId::BOOT_STARTING), 1, 0, -1, &font8x12);
}

// ===============================================================
// Private Helpers
// ===============================================================

static void format_compact_num(char* buf, size_t buf_size, uint32_t val)
{
    if (val >= 1000000) {
        snprintf(buf, buf_size, "%.2fM", val / 1000000.0f);
    }
    else if (val >= 10000) {
        snprintf(buf, buf_size, "%.1fk", val / 1000.0f);
    }
    else {
        snprintf(buf, buf_size, "%lu", static_cast<unsigned long>(val));
    }
}

static void format_elapsed_time(char* buf, size_t buf_size, int64_t last_update_ts)
{
    if (!buf || buf_size == 0)
        return;

    int64_t now_ms = esp_timer_get_time() / 1000;
    uint32_t elapsed_s = 0;
    if (last_update_ts > 0 && now_ms >= last_update_ts) {
        elapsed_s = static_cast<uint32_t>((now_ms - last_update_ts) / 1000);
    }
    uint32_t mm = elapsed_s / 60;
    uint32_t ss = elapsed_s % 60;
    snprintf(buf, buf_size, "%02lu:%02lu", static_cast<unsigned long>(mm), static_cast<unsigned long>(ss));
}

static void format_dynamic_uptime(char* buf, size_t buf_size, uint32_t uptime_s)
{
    if (!buf || buf_size == 0)
        return;

    if (uptime_s < 60) {
        snprintf(buf, buf_size, "%lus", static_cast<unsigned long>(uptime_s));
    }
    else if (uptime_s < 3600) {
        uint32_t mm = uptime_s / 60;
        uint32_t ss = uptime_s % 60;
        snprintf(buf, buf_size, "%lum %lus", static_cast<unsigned long>(mm), static_cast<unsigned long>(ss));
    }
    else if (uptime_s < 86400) {
        uint32_t hh = uptime_s / 3600;
        uint32_t mm = (uptime_s % 3600) / 60;
        snprintf(buf, buf_size, "%luh %lum", static_cast<unsigned long>(hh), static_cast<unsigned long>(mm));
    }
    else if (uptime_s < 2592000) { // < 30 days
        uint32_t dd = uptime_s / 86400;
        uint32_t hh = (uptime_s % 86400) / 3600;
        snprintf(buf, buf_size, "%lud %luh", static_cast<unsigned long>(dd), static_cast<unsigned long>(hh));
    }
    else { // >= 30 days
        uint32_t months = uptime_s / 2592000;
        uint32_t dd = (uptime_s % 2592000) / 86400;
        snprintf(buf, buf_size, "%lum %lud", static_cast<unsigned long>(months), static_cast<unsigned long>(dd));
    }
}

static const char* get_node_name(farm::NodeId node)
{
    switch (node) {
    case farm::NodeId::WATER_TANK:
        return I18n::get(StrId::HEADER_WATER_TANK);
    case farm::NodeId::SOLAR_SENSOR:
        return I18n::get(StrId::HEADER_SOLAR);
    case farm::NodeId::PUMP_CONTROL:
        return I18n::get(StrId::HEADER_PUMP);
    default:
        return "NODE";
    }
}

static ScreenMode get_screen_for_node(farm::NodeId node)
{
    switch (node) {
    case farm::NodeId::WATER_TANK:
        return ScreenMode::WATER_TANK_SCREEN;
    case farm::NodeId::PUMP_CONTROL:
        return ScreenMode::PUMP_SCREEN;
    case farm::NodeId::SOLAR_SENSOR:
        return ScreenMode::SOLAR_SCREEN;
    default:
        return ScreenMode::WATER_TANK_SCREEN;
    }
}

[[maybe_unused]] static const char* battery_state_to_string(farm::BatteryState state)
{
    switch (state) {
    case farm::BatteryState::CRITICAL:
        return "CRIT";
    case farm::BatteryState::LOW:
        return "LOW";
    case farm::BatteryState::NORMAL:
        return "OK";
    case farm::BatteryState::FULL:
        return "FULL";
    default:
        return "?";
    }
}

static const char* sensor_status_to_string(farm::SensorStatus status)
{
    switch (status) {
    case farm::SensorStatus::OK:
        return "OK";
    case farm::SensorStatus::WARNING_LOW_SIGNAL:
        return "LS";
    case farm::SensorStatus::ERROR_TIMEOUT:
        return "TO";
    case farm::SensorStatus::ERROR_OUT_OF_RANGE:
        return "OOR";
    case farm::SensorStatus::ERROR_UNSTABLE:
        return "UNS";
    case farm::SensorStatus::ERROR_HARDWARE:
        return "HDW";
    default:
        return "UNK";
    }
}

void UIController::draw_wifi_signal_icon(int x, int y, bool connected, int8_t rssi)
{
    if (!connected) {
        gfx_.draw_hline(x, y + 6, 11, 1);
        gfx_.draw_char(x + 3, y, 'x', 1);
        return;
    }

    uint8_t active_bars = 1;
    if (rssi > -60) {
        active_bars = 4;
    }
    else if (rssi > -70) {
        active_bars = 3;
    }
    else if (rssi > -80) {
        active_bars = 2;
    }
    else {
        active_bars = 1;
    }

    if (active_bars >= 1)
        gfx_.fill_rect(x + 0, y + 5, 2, 2, 1);
    else
        gfx_.draw_hline(x + 0, y + 6, 2, 1);

    if (active_bars >= 2)
        gfx_.fill_rect(x + 3, y + 3, 2, 4, 1);
    else
        gfx_.draw_hline(x + 3, y + 6, 2, 1);

    if (active_bars >= 3)
        gfx_.fill_rect(x + 6, y + 1, 2, 6, 1);
    else
        gfx_.draw_hline(x + 6, y + 6, 2, 1);

    if (active_bars >= 4)
        gfx_.fill_rect(x + 9, y + 0, 2, 7, 1);
    else
        gfx_.draw_hline(x + 9, y + 6, 2, 1);
}

void UIController::draw_battery_icon(int x, int y, uint8_t percent)
{
    gfx_.draw_rect(x, y, 14, 7, 1);
    gfx_.fill_rect(x + 14, y + 2, 2, 3, 1);

    int fill_w = (static_cast<int>(percent) * 10) / 100;
    if (percent > 0 && fill_w == 0)
        fill_w = 1;
    if (fill_w > 10)
        fill_w = 10;

    if (fill_w > 0) {
        gfx_.fill_rect(x + 2, y + 2, fill_w, 3, 1);
    }
}