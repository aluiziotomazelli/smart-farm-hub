// main/include/ui_controller.hpp
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "i_graphics_context.hpp"
#include "system_state.hpp"
#include "ui_events.hpp"
#include "app_commands.hpp"

enum class ScreenMode
{
    MAIN_SCREEN,
    WATER_TANK_SCREEN,
    WATER_TANK_SUBMENU,
    SOLAR_SCREEN,
    LOADS_SCREEN,
    STATS_SCREEN,
    SETTINGS_SCREEN,
    BOOT_SCREEN
};

class UIController
{
public:
    explicit UIController(IGraphicsContext& gfx, QueueHandle_t app_cmd_queue = nullptr);
    void render_main_screen(const SystemState& state);
    void render_stats_screen(const SystemState& state);
    void render_settings_screen();
    void render_boot_screen();
    void render_water_tank_screen(const SystemState& state);
    void render_water_tank_submenu(const SystemState& state);
    void render_solar_screen(const SystemState& state);
    void render_loads_screen(const SystemState& state);
    void render_current_screen(const SystemState& state);

    void handle_event(const UiEvent& event);
    ScreenMode get_screen_mode() const { return current_screen_; }
    void set_screen_mode(ScreenMode mode) { current_screen_ = mode; }

private:
    IGraphicsContext& gfx_;
    QueueHandle_t app_cmd_queue_;
    ScreenMode current_screen_{ScreenMode::WATER_TANK_SCREEN};
    int submenu_index_{0};
    static constexpr int SUBMENU_TOTAL_ITEMS = 4;

    const char* battery_state_to_string(farm::BatteryState state);
    const char* sensor_status_to_string(farm::SensorStatus status);

    void draw_wifi_signal_icon(int x, int y, bool connected, int8_t rssi);
    void draw_battery_icon(int x, int y, uint8_t percent);
};

