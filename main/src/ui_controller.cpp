// main/src/ui_controller.cpp
#include <stdio.h>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "esp_timer.h"

#include "i_graphics_context.hpp"
#include "system_state.hpp"
#include "ui_events.hpp"
#include "app_commands.hpp"
#include "ui_controller.hpp"

static const char* TAG = "UIController";

UIController::UIController(IGraphicsContext& gfx, QueueHandle_t app_cmd_queue)
    : gfx_(gfx)
    , app_cmd_queue_(app_cmd_queue)
{
}

void UIController::handle_event(const UiEvent& event)
{
    switch (event.type) {
    case UiEventType::NAV_NEXT:
        if (current_screen_ == ScreenMode::MAIN_SCREEN) {
            current_screen_ = ScreenMode::WATER_TANK_SCREEN;
        }
        else if (current_screen_ == ScreenMode::WATER_TANK_SCREEN) {
            current_screen_ = ScreenMode::SOLAR_SCREEN;
        }
        else if (current_screen_ == ScreenMode::SOLAR_SCREEN) {
            current_screen_ = ScreenMode::LOADS_SCREEN;
        }
        else if (current_screen_ == ScreenMode::LOADS_SCREEN) {
            current_screen_ = ScreenMode::STATS_SCREEN;
        }
        else if (current_screen_ == ScreenMode::STATS_SCREEN) {
            current_screen_ = ScreenMode::MAIN_SCREEN;
        }
        ESP_LOGI(TAG, "Navigated next -> screen %d", static_cast<int>(current_screen_));
        break;

    case UiEventType::NAV_PREV:
        if (current_screen_ == ScreenMode::MAIN_SCREEN) {
            current_screen_ = ScreenMode::STATS_SCREEN;
        }
        else if (current_screen_ == ScreenMode::WATER_TANK_SCREEN) {
            current_screen_ = ScreenMode::MAIN_SCREEN;
        }
        else if (current_screen_ == ScreenMode::SOLAR_SCREEN) {
            current_screen_ = ScreenMode::WATER_TANK_SCREEN;
        }
        else if (current_screen_ == ScreenMode::LOADS_SCREEN) {
            current_screen_ = ScreenMode::SOLAR_SCREEN;
        }
        else if (current_screen_ == ScreenMode::STATS_SCREEN) {
            current_screen_ = ScreenMode::LOADS_SCREEN;
        }
        ESP_LOGI(TAG, "Navigated prev -> screen %d", static_cast<int>(current_screen_));
        break;

    case UiEventType::CONFIRM:
        ESP_LOGI(TAG, "CONFIRM pressed on screen %d", static_cast<int>(current_screen_));
        if (app_cmd_queue_ != nullptr) {
            AppCommand cmd;
            if (current_screen_ == ScreenMode::WATER_TANK_SCREEN) {
                cmd.espnow_cmd = espnow::CommandType::START_OTA;
                cmd.target_node = farm::NodeId::WATER_TANK;
                cmd.param = 0;
                xQueueSend(app_cmd_queue_, &cmd, 0);
                ESP_LOGI(TAG, "Queued START_OTA for WATER_TANK");
            }
        }
        break;

    case UiEventType::BACK:
        ESP_LOGI(TAG, "BACK pressed -> returning to MAIN_SCREEN");
        current_screen_ = ScreenMode::MAIN_SCREEN;
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

void UIController::render_current_screen(const SystemState& state)
{
    switch (current_screen_) {
    case ScreenMode::MAIN_SCREEN:
        render_main_screen(state);
        break;
    case ScreenMode::WATER_TANK_SCREEN:
        render_water_tank_screen(state);
        break;
    case ScreenMode::SOLAR_SCREEN:
        render_solar_screen(state);
        break;
    case ScreenMode::LOADS_SCREEN:
        render_loads_screen(state);
        break;
    case ScreenMode::STATS_SCREEN:
        render_stats_screen(state);
        break;
    case ScreenMode::BOOT_SCREEN:
        render_boot_screen();
        break;
    }
}

void UIController::render_main_screen(const SystemState& state)
{
    char buf[64];

    // --- Header ---
    gfx_.draw_string(0, 0, state.wifi_connected ? "[W]" : "[_]", 1);
    gfx_.draw_string(24, 0, "FARM HUB", 1);
    gfx_.draw_string(90, 0, "[B]", 1);
    gfx_.draw_string(114, 0, "[_]", 1);

    // Separator
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    // --- Row 1: Level + Timestamp + Distance ---
    int64_t now_ms = esp_timer_get_time() / 1000;
    uint32_t elapsed_s = 0;
    if (state.last_water_update_ts > 0 && now_ms >= state.last_water_update_ts) {
        elapsed_s = static_cast<uint32_t>((now_ms - state.last_water_update_ts) / 1000);
    }
    uint32_t mm = elapsed_s / 60;
    uint32_t ss = elapsed_s % 60;

    float level_percent = state.water_level_permille / 10.0f;
    snprintf(
        buf,
        sizeof(buf),
        "  %.1f  %lu:%02lu   %.1f",
        level_percent,
        static_cast<unsigned long>(mm),
        static_cast<unsigned long>(ss),
        state.water_distance_cm);
    gfx_.draw_string(0, 12, buf, 1);

    // --- Row 2: Visual bar ---
    gfx_.draw_rect(0, 22, 128, 8, 1);
    int fill_width = (state.water_level_permille * 126) / 1000;
    for (int i = 0; i < fill_width; ++i) {
        gfx_.draw_vline(1 + i, 23, 6, 1);
    }

    // --- Row 3: WiFi Status ---
    snprintf(buf, sizeof(buf), "WiFi: %-3s    R: %d", state.wifi_connected ? "ON" : "OFF", state.wifi_rssi);
    gfx_.draw_string(0, 34, buf, 1);

    // --- Row 4: Network Status ---
    gfx_.draw_string(0, 44, "E-NOW Node Detail", 1);

    // --- Row 5: Stats Placeholder ---
    gfx_.draw_string(0, 54, "Stats: via node screens", 1);
}

void UIController::render_stats_screen(const SystemState& state)
{
    gfx_.draw_string(0, 0, "STATS SCREEN", 1);
    // Placeholder for actual stats
}

void UIController::render_boot_screen()
{
    gfx_.draw_string(30, 28, "STARTING...", 1);
}

void UIController::render_water_tank_screen(const SystemState& state)
{
    char buf[64];

    // --- Header ---
    gfx_.draw_string(0, 0, state.wifi_connected ? "[W]" : "[_]", 1);
    gfx_.draw_string(24, 0, "WATER-TANK", 1);

    // Separator
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    // --- Row 1: Level + Timestamp + Distance ---
    int64_t now_ms = esp_timer_get_time() / 1000;
    uint32_t elapsed_s = 0;
    if (state.last_water_update_ts > 0 && now_ms >= state.last_water_update_ts) {
        elapsed_s = static_cast<uint32_t>((now_ms - state.last_water_update_ts) / 1000);
    }
    uint32_t mm = elapsed_s / 60;
    uint32_t ss = elapsed_s % 60;

    float level_percent = state.water_level_permille / 10.0f;
    snprintf(
        buf,
        sizeof(buf),
        "  %.1f  %lu:%02lu   %.1f",
        level_percent,
        static_cast<unsigned long>(mm),
        static_cast<unsigned long>(ss),
        state.water_distance_cm);
    gfx_.draw_string(0, 12, buf, 1);

    // --- Row 2: Visual bar ---
    gfx_.draw_rect(0, 22, 128, 8, 1);
    int fill_width = (state.water_level_permille * 126) / 1000;
    for (int i = 0; i < fill_width; ++i) {
        gfx_.draw_vline(1 + i, 23, 6, 1);
    }

    // --- Row 3: Water Tank Battery + Sensor Status ---
    const char* status_str = "UNK";
    switch (state.water_sensor_status) {
    case farm::SensorStatus::OK:
        status_str = "OK";
        break;
    case farm::SensorStatus::WARNING_LOW_SIGNAL:
        status_str = "LS";
        break;
    case farm::SensorStatus::ERROR_TIMEOUT:
        status_str = "TO";
        break;
    case farm::SensorStatus::ERROR_OUT_OF_RANGE:
        status_str = "OOR";
        break;
    case farm::SensorStatus::ERROR_UNSTABLE:
        status_str = "UNS";
        break;
    case farm::SensorStatus::ERROR_HARDWARE:
        status_str = "HDW";
        break;
    default:
        status_str = "UNK";
        break;
    }
    snprintf(buf, sizeof(buf), "Sensor: %s", status_str);
    gfx_.draw_string(0, 34, buf, 1);

    // --- Row 4: Battery % and State ---
    auto bat_str = battery_state_to_string(state.water_battery_state);
    snprintf(buf, sizeof(buf), "Bat: %u%%  -  %u mV", state.water_battery_percent, state.water_battery_mv);
    gfx_.draw_string(0, 44, buf, 1);

    // --- Row 5: Float Switch & Backup Mode ---
    snprintf(
        buf,
        sizeof(buf),
        "Float: %-4s BkUp: %s",
        state.water_float_switch_full ? "FULL" : "EMPT",
        state.water_backup_mode ? "ON" : "OFF");
    gfx_.draw_string(0, 54, buf, 1);
}

void UIController::render_solar_screen(const SystemState& state)
{
    char buf[64];
    gfx_.draw_string(0, 0, "[W]", 1);
    gfx_.draw_string(24, 0, "SOLAR GENERATION", 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    snprintf(buf, sizeof(buf), "Instant: %u W", state.solar_power_w_instant);
    gfx_.draw_string(0, 14, buf, 1);

    snprintf(buf, sizeof(buf), "Avg 5m:  %u W", state.solar_power_w_avg);
    gfx_.draw_string(0, 26, buf, 1);

    snprintf(buf, sizeof(buf), "Sensor: %u mV", state.solar_voltage_mv);
    gfx_.draw_string(0, 38, buf, 1);

    snprintf(buf, sizeof(buf), "Current: %u mA", state.solar_current_ma);
    gfx_.draw_string(0, 50, buf, 1);
}

void UIController::render_loads_screen(const SystemState& state)
{
    char buf[64];
    gfx_.draw_string(0, 0, "[W]", 1);
    gfx_.draw_string(24, 0, "LOADS SUMMARY", 1);
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    uint16_t total_w = state.total_solar_consumption_w();
    int16_t margin = state.power_margin_w();

    snprintf(buf, sizeof(buf), "Solar Load: %u W", total_w);
    gfx_.draw_string(0, 14, buf, 1);

    snprintf(buf, sizeof(buf), "Margin:     %d W", margin);
    gfx_.draw_string(0, 26, buf, 1);

    const auto& pump = state.load(LoadIndex::PUMP);
    const char* mode_str = (pump.control_mode == farm::ControlMode::AUTO)     ? "AUTO"
                           : (pump.control_mode == farm::ControlMode::MANUAL) ? "MAN"
                                                                              : "OFF";
    const char* src_str = (pump.active_source == farm::PowerSource::SOLAR)  ? "SOLAR"
                          : (pump.active_source == farm::PowerSource::GRID) ? "GRID"
                                                                            : "UNK";

    snprintf(buf, sizeof(buf), "Pump: %s/%s %uW", mode_str, src_str, pump.power_w);
    gfx_.draw_string(0, 42, buf, 1);
}

const char* UIController::battery_state_to_string(farm::BatteryState state)
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
