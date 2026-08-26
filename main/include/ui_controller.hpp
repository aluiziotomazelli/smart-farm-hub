// main/include/ui_controller.hpp
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "i_graphics_context.hpp"
#include "interfaces/i_espnow_manager.hpp"
#include "interfaces/i_node_registry.hpp"
#include "ui_snapshot.hpp"
#include "ui_events.hpp"
#include "app_commands.hpp"

enum class ScreenMode
{
    MAIN_SCREEN,
    WATER_TANK_SCREEN,
    PUMP_SCREEN,
    SOLAR_SCREEN,
    LOADS_SCREEN,
    STATS_SCREEN,
    SETTINGS_SCREEN,
    BOOT_SCREEN,
    NODE_SUBMENU,
    NODE_STATS_SCREEN,
    NODE_INFO_SCREEN,
    WATER_TANK_LAST_REPORT_SCREEN,
    SOLAR_SENSOR_LAST_REPORT_SCREEN,
    PUMP_LAST_REPORT_SCREEN
};

enum class SubmenuItem : uint8_t
{
    LAST_REPORT = 0,
    ESPNOW_STATS = 1,
    REQUEST_REPORT = 2,
    CONFIG = 3,
    CLEAR_STATS = 4,
    REBOOT_NODE = 5,
    START_OTA = 6,
    BACK = 7,
    COUNT = 8
};

class UIController
{
public:
    UIController(
        IGraphicsContext& gfx,
        hub::INodeRegistry* node_registry = nullptr,
        QueueHandle_t app_cmd_queue = nullptr,
        espnow::IEspNowManager* espnow = nullptr);
    void render_main_screen(const UiSnapshotData& data);
    void render_stats_screen(const UiSnapshotData& data);
    void render_settings_screen();
    void render_boot_screen();
    void render_water_tank_screen(const UiSnapshotData& data);
    void render_pump_screen(const UiSnapshotData& data);
    void render_water_tank_last_report_screen(const UiSnapshotData& data);
    void render_solar_sensor_last_report_screen(const UiSnapshotData& data);
    void render_pump_last_report_screen(const UiSnapshotData& data);
    void render_node_submenu(const UiSnapshotData& data);
    void render_node_stats_screen(const UiSnapshotData& data);
    void render_node_info_screen(const UiSnapshotData& data);
    void render_solar_screen(const UiSnapshotData& data);
    void render_loads_screen(const UiSnapshotData& data);
    void render_current_screen(const UiSnapshotData& data);

    void handle_event(const UiEvent& event);
    ScreenMode get_screen_mode() const { return current_screen_; }
    void set_screen_mode(ScreenMode mode) { current_screen_ = mode; }
    farm::NodeId get_active_node() const { return active_node_; }
    void set_active_node(farm::NodeId node) { active_node_ = node; }

private:
    IGraphicsContext& gfx_;
    hub::INodeRegistry* node_registry_{nullptr};
    QueueHandle_t app_cmd_queue_;
    espnow::IEspNowManager* espnow_{nullptr};
    ScreenMode current_screen_{ScreenMode::WATER_TANK_SCREEN};
    farm::NodeId active_node_{farm::NodeId::WATER_TANK};
    int submenu_index_{0};
    static constexpr int SUBMENU_TOTAL_ITEMS = static_cast<int>(SubmenuItem::COUNT);
    int settings_index_{0};
    static constexpr int SETTINGS_TOTAL_ITEMS = 2;
    int64_t pairing_start_ts_ms_{0};
    bool pairing_active_{false};

    void draw_wifi_signal_icon(int x, int y, bool connected, int8_t rssi);
    void draw_battery_icon(int x, int y, uint8_t percent);
};
