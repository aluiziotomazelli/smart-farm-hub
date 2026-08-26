// main/include/hub_app.hpp
#pragma once

#include "driver/gpio.h"

#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_sleep.hpp"
#include "interfaces/i_hal_system.hpp"
#include "interfaces/i_hal_timer.hpp"
#include "interfaces/i_hub_nvs.hpp"
#include "interfaces/i_node_registry.hpp"
#include "interfaces/i_nvs_core.hpp"
#include "interfaces/i_ota_manager.hpp"
#include "interfaces/i_time_manager.hpp"
#include "interfaces/i_wifi_manager.hpp"
#include "i_espnow_manager.hpp"

#include "app_commands.hpp"
#include "command_manager.hpp"
#include "core_types.hpp"
#include "farm_protocol_types.hpp"
#include "hub_stats.hpp"
#include "ui_events.hpp"

struct HubAppConfig
{
    // Placeholder for future configuration options
};

class HubApp
{
public:
    HubApp(
        INvsCore& core_storage,
        IHubNvs& hub_storage,
        hub::INodeRegistry& node_registry,
        espnow::IEspNowManager& espnow,
        wifi_manager::IWiFiManager& wifi,
        IOtaManager& ota_manager,
        time_manager::ITimeManager& time_manager,
        idf_hals::IHalFreertos& rtos,
        idf_hals::ISystemHAL& hal_system,
        idf_hals::ISleepHAL& hal_sleep,
        idf_hals::ITimerHAL& hal_timer);

    esp_err_t
    init(const HubAppConfig& config = {}, QueueHandle_t app_cmd_queue = nullptr, QueueHandle_t rx_queue = nullptr);
    void run();

    void set_command_manager(hub::CommandManager& cmd_mgr) { cmd_mgr_ = &cmd_mgr; }
    void handle_app_command(const AppCommand& cmd);
    void on_node_version_received(uint8_t node_id, uint8_t major, uint8_t minor, uint8_t patch);
    HubStats& get_stats() { return stats_; }

protected:
    CoreData core_;
    HubStats stats_;

    bool session_healthy_ = true;
    bool pending_firmware_verify_ = false;
    bool pending_core_commit_ = false;
    bool pending_hub_commit_ = false;

private:
    INvsCore& core_storage_;
    IHubNvs& hub_storage_;
    hub::INodeRegistry& node_registry_;
    espnow::IEspNowManager& espnow_;
    wifi_manager::IWiFiManager& wifi_;
    IOtaManager& ota_manager_;
    time_manager::ITimeManager& time_manager_;
    hub::CommandManager* cmd_mgr_{nullptr};
    idf_hals::IHalFreertos& hal_rtos_;
    idf_hals::ISystemHAL& hal_system_;
    idf_hals::ISleepHAL& hal_sleep_;
    idf_hals::ITimerHAL& hal_timer_;

    QueueHandle_t app_cmd_queue_ = nullptr;
    HubAppConfig config_;

    esp_err_t init_hub_storage();
    esp_err_t init_core_storage();
    esp_err_t init_wifi();
    esp_err_t init_espnow(QueueHandle_t rx_queue);
    esp_err_t init_ota_manager();

    void log_boot_summary();
    void check_firmware();
    void update_wifi_status();
    void save_persistent_state();

    int64_t last_wifi_poll_ts_{0};
    int64_t last_nvs_commit_ts_{0};
};
