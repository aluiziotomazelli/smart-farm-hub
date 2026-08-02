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

void UIController::handle_event(const UiEvent& event) {
    switch (event.type) {
    case UiEventType::NAV_NEXT:
        if (current_screen_ == ScreenMode::MAIN_SCREEN) {
            current_screen_ = ScreenMode::WATER_TANK_SCREEN;
        } else if (current_screen_ == ScreenMode::WATER_TANK_SCREEN) {
            current_screen_ = ScreenMode::STATS_SCREEN;
        } else if (current_screen_ == ScreenMode::STATS_SCREEN) {
            current_screen_ = ScreenMode::MAIN_SCREEN;
        }
        ESP_LOGI(TAG, "Navigated next -> screen %d", static_cast<int>(current_screen_));
        break;

    case UiEventType::NAV_PREV:
        if (current_screen_ == ScreenMode::MAIN_SCREEN) {
            current_screen_ = ScreenMode::STATS_SCREEN;
        } else if (current_screen_ == ScreenMode::WATER_TANK_SCREEN) {
            current_screen_ = ScreenMode::MAIN_SCREEN;
        } else if (current_screen_ == ScreenMode::STATS_SCREEN) {
            current_screen_ = ScreenMode::WATER_TANK_SCREEN;
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

void UIController::render_current_screen(const SystemState& state) {
    switch (current_screen_) {
    case ScreenMode::MAIN_SCREEN:
        render_main_screen(state);
        break;
    case ScreenMode::WATER_TANK_SCREEN:
        render_water_tank_screen(state);
        break;
    case ScreenMode::STATS_SCREEN:
        render_stats_screen(state);
        break;
    case ScreenMode::BOOT_SCREEN:
        render_boot_screen();
        break;
    }
}

void UIController::render_main_screen(const SystemState& state) {
    char buf[64];

    // --- Header ---
    gfx_.draw_string(0, 0, state.wifi_connected ? "[W]" : "[_]", 1);
    gfx_.draw_string(24, 0, "FARM HUB", 1);
    gfx_.draw_string(90, 0, "[B]", 1);
    gfx_.draw_string(114, 0, state.ota_in_progress ? "[O]" : "[_]", 1);
    
    // Separator
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    // --- Row 1: Level + Timestamp + Distance ---
    uint32_t now = esp_timer_get_time();
    uint32_t elapsed_s = 0;
    if (state.last_water_update_ts > 0 && now >= state.last_water_update_ts) {
        elapsed_s = (now - state.last_water_update_ts) / 1000000;
    }
    uint32_t mm = elapsed_s / 60;
    uint32_t ss = elapsed_s % 60;

    float level_percent = state.water_level_permille / 10.0f;
    snprintf(buf, sizeof(buf), "  %.1f  %lu:%02lu   %.1f", level_percent, static_cast<unsigned long>(mm), static_cast<unsigned long>(ss), state.water_distance_cm);
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

    // --- Row 4: ESP-NOW Status ---
    snprintf(buf, sizeof(buf), "E-NOW  Pr: %d  R: %d", state.espnow_peers, state.espnow_avg_rssi);
    gfx_.draw_string(0, 44, buf, 1);

    // --- Row 5: Stats ---
    snprintf(buf, sizeof(buf), "S: %-4lu  L: %-2lu  T:%-2lu", static_cast<unsigned long>(state.messages_sent), static_cast<unsigned long>(state.messages_lost), static_cast<unsigned long>(state.last_rtt_ms));
    gfx_.draw_string(0, 54, buf, 1);
}

void UIController::render_stats_screen(const SystemState& state) {
    gfx_.draw_string(0, 0, "STATS SCREEN", 1);
    // Placeholder for actual stats
}

void UIController::render_boot_screen() {
    gfx_.draw_string(30, 28, "STARTING...", 1);
}

void UIController::render_water_tank_screen(const SystemState& state) {
    char buf[64];

    // --- Header ---
    gfx_.draw_string(0, 0, state.wifi_connected ? "[W]" : "[_]", 1);
    gfx_.draw_string(24, 0, "WATER-TANK", 1);
    
    // Separator
    gfx_.draw_hline(0, 8, gfx_.get_width(), 1);

    // --- Row 1: Level + Timestamp + Distance ---
    uint32_t now = esp_timer_get_time();
    uint32_t elapsed_s = 0;
    if (state.last_water_update_ts > 0 && now >= state.last_water_update_ts) {
        elapsed_s = (now - state.last_water_update_ts) / 1000000;
    }
    uint32_t mm = elapsed_s / 60;
    uint32_t ss = elapsed_s % 60;

    float level_percent = state.water_level_permille / 10.0f;
    snprintf(buf, sizeof(buf), "  %.1f  %lu:%02lu   %.1f", level_percent, static_cast<unsigned long>(mm), static_cast<unsigned long>(ss), state.water_distance_cm);
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
        case farm::SensorStatus::OK: status_str = "OK"; break;
        case farm::SensorStatus::WARNING_LOW_SIGNAL: status_str = "LS"; break;
        case farm::SensorStatus::ERROR_TIMEOUT: status_str = "TO"; break;
        case farm::SensorStatus::ERROR_OUT_OF_RANGE: status_str = "OOR"; break;
        case farm::SensorStatus::ERROR_UNSTABLE: status_str = "UNS"; break;
        case farm::SensorStatus::ERROR_HARDWARE: status_str = "HDW"; break;
        default: status_str = "UNK"; break;
    }
    snprintf(buf, sizeof(buf), "B: %u mV    S: %s", state.water_battery_mv, status_str);
    gfx_.draw_string(0, 34, buf, 1);

    // --- Row 4: ESP-NOW Status ---
    snprintf(buf, sizeof(buf), "E-NOW  Pr: %d  R: %d", state.espnow_peers, state.espnow_avg_rssi);
    gfx_.draw_string(0, 44, buf, 1);

    // --- Row 5: Fill State ---
    const char* fill_state_str = "UNKNOWN";
    switch (state.water_fill_state) {
        case 1: fill_state_str = "STABLE"; break;
        case 2: fill_state_str = "FILLING"; break;
        case 3: fill_state_str = "DRAINING"; break;
        default: fill_state_str = "UNKNOWN"; break;
    }
    snprintf(buf, sizeof(buf), "State: %s", fill_state_str);
    gfx_.draw_string(0, 54, buf, 1);
}
