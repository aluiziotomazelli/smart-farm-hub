#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>

#include "farm_protocol_types.hpp"
#include "load_types.hpp"

struct SystemState {
    // ─── Water Tank Node ─────────────────────────────────────────────
    int64_t last_water_update_ts = 0; // ms since boot (esp_timer_get_time()/1000), 0=never
    uint16_t water_level_permille = 0;
    float water_distance_cm = 0.0f;
    uint16_t water_battery_mv = 0;
    uint8_t water_battery_percent = 0;
    farm::BatteryState water_battery_state = farm::BatteryState::UNKNOWN;
    farm::SensorStatus water_sensor_status = farm::SensorStatus::UNKNOWN;
    bool water_float_switch_full = false;
    bool water_backup_mode = false;
    uint64_t water_node_unix_time = 0;

    // ─── Solar Generation ────────────────────────────────────────────
    int64_t last_solar_update_ts = 0;
    uint16_t solar_voltage_mv = 0;
    uint16_t solar_current_ma = 0;
    uint16_t solar_power_w_instant = 0; ///< Interpolated solar array AC power in Watts
    uint16_t solar_power_w_avg = 0;     ///< Moving average of solar array AC power in Watts

    // ─── Electrical Loads ────────────────────────────────────────────
    LoadState loads[static_cast<uint8_t>(LoadIndex::MAX)] = {};

    LoadState& load(LoadIndex idx) { return loads[static_cast<uint8_t>(idx)]; }
    const LoadState& load(LoadIndex idx) const { return loads[static_cast<uint8_t>(idx)]; }

    // ─── Wi-Fi / Network ─────────────────────────────────────────────
    bool wifi_connected = false;
    int8_t wifi_rssi = 0;

    // ─── System ──────────────────────────────────────────────────────
    bool time_synced = false;
    int64_t last_ui_activity_ts = 0; // ms since boot

    // ─── Computed Helpers ─────────────────────────────────────────────

    /**
     * @brief Sum of power_w for all loads with active_source == SOLAR and control_mode != OFF.
     */
    uint16_t total_solar_consumption_w() const
    {
        uint16_t total = 0;
        for (const auto& l : loads) {
            if (l.control_mode != farm::ControlMode::OFF &&
                l.active_source == farm::PowerSource::SOLAR) {
                total += l.power_w;
            }
        }
        return total;
    }

    /**
     * @brief Available solar power margin in Watts. Can be negative (overloaded).
     */
    int16_t power_margin_w() const
    {
        return static_cast<int16_t>(solar_power_w_avg) -
               static_cast<int16_t>(total_solar_consumption_w());
    }

    bool is_water_data_fresh(int64_t now_ms, uint32_t max_age_ms) const
    {
        return last_water_update_ts > 0 &&
               (now_ms - last_water_update_ts) < static_cast<int64_t>(max_age_ms);
    }

    bool is_solar_data_fresh(int64_t now_ms, uint32_t max_age_ms) const
    {
        return last_solar_update_ts > 0 &&
               (now_ms - last_solar_update_ts) < static_cast<int64_t>(max_age_ms);
    }
};

extern SystemState g_system_state;
extern SemaphoreHandle_t g_state_mutex;
