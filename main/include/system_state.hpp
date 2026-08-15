#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <climits>
#include <cstdint>

#include "core_types.hpp"
#include "farm_protocol_types.hpp"
#include "load_types.hpp"

struct SystemState
{
    // ─── Per-Node Metadata (up to farm::MAX_HUB_NODES) ────────────────
    farm::NodeMetadata nodes[farm::MAX_HUB_NODES] = {};

    farm::PowerProfile get_node_power_profile(farm::NodeId node_id) const
    {
        for (const auto& n : nodes) {
            if (n.node_id == node_id) {
                return n.power_profile;
            }
        }
        return farm::PowerProfile::DEEP_SLEEP;
    }

    void set_node_power_profile(farm::NodeId node_id, farm::PowerProfile profile)
    {
        for (auto& n : nodes) {
            if (n.node_id == node_id) {
                n.power_profile = profile;
                return;
            }
        }
        for (auto& n : nodes) {
            if (n.node_id == farm::NodeId::UNKNOWN) {
                n.node_id = node_id;
                n.power_profile = profile;
                return;
            }
        }
    }

    void set_node_fw_version(farm::NodeId node_id, uint8_t major, uint8_t minor, uint8_t patch)
    {
        for (auto& n : nodes) {
            if (n.node_id == node_id) {
                n.fw_major = major;
                n.fw_minor = minor;
                n.fw_patch = patch;
                return;
            }
        }
        for (auto& n : nodes) {
            if (n.node_id == farm::NodeId::UNKNOWN) {
                n.node_id = node_id;
                n.fw_major = major;
                n.fw_minor = minor;
                n.fw_patch = patch;
                return;
            }
        }
    }

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

    // ─── Solar Sensor Node ───────────────────────────────────────────
    int64_t last_solar_update_ts = 0; ///< ms since boot (0 = never)

    // Raw fields from SolarSensorReport (node-filtered, EMA α≈0.8 applied on node)
    uint16_t solar_isc_current_ma = 0;          ///< Ref cell short-circuit current (mA). Spec: 600mA @ 1000 W/m², 25°C
    uint16_t solar_irradiance_wm2 = 0;          ///< Estimated solar irradiance (W/m²)
    int16_t solar_panel_temp_c = INT16_MIN;     ///< Panel temp in 0.1°C. INT16_MIN = sensor absent
    uint16_t solar_battery_mv = 0;              ///< Sensor node battery voltage (mV)
    uint8_t solar_battery_percent = 0;          ///< Sensor node battery level (0–100)
    farm::BatteryState solar_battery_state = farm::BatteryState::UNKNOWN;
    farm::SensorStatus solar_sensor_status = farm::SensorStatus::UNKNOWN;
    uint16_t solar_max_current_ma = 0;          ///< Peak Isc of current day (from node, for display)
    uint32_t solar_daily_yield_mah = 0;         ///< Daily yield integral from node (mAh, display only)
    bool solar_is_night_mode = false;
    uint64_t solar_node_unix_time = 0;          ///< UTC epoch from node (0 = not synced)

    // Hub-computed fields (derived by SolarSensorHandler via SolarPowerEstimator)
    uint16_t solar_power_w_instant = 0;         ///< Estimated AC power of installation (W). Primary for LCT.
    uint16_t solar_power_w_avg = 0;             ///< Alias for solar_power_w_instant (no hub-side smoothing)

    // Hub-accumulated daily energy estimate (volatile, reset on reboot ~= last 24h)
    float solar_daily_yield_wh_hub = 0.0f;      ///< Σ(power_w_instant × Δt_h) per report

    // Hub-tracked daily temperature range (volatile, reset on reboot ~= last 24h)
    int16_t solar_panel_temp_max_c = INT16_MIN; ///< Daily max panel temp (0.1°C)
    int16_t solar_panel_temp_min_c = INT16_MAX; ///< Daily min panel temp (0.1°C)

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
            if (l.control_mode != farm::ControlMode::OFF && l.active_source == farm::PowerSource::SOLAR) {
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
        return static_cast<int16_t>(solar_power_w_avg) - static_cast<int16_t>(total_solar_consumption_w());
    }

    bool is_water_data_fresh(int64_t now_ms, uint32_t max_age_ms) const
    {
        return last_water_update_ts > 0 && (now_ms - last_water_update_ts) < static_cast<int64_t>(max_age_ms);
    }

    bool is_solar_data_fresh(int64_t now_ms, uint32_t max_age_ms) const
    {
        return last_solar_update_ts > 0 && (now_ms - last_solar_update_ts) < static_cast<int64_t>(max_age_ms);
    }

    bool is_solar_night() const
    {
        return solar_is_night_mode;
    }
};

extern SystemState g_system_state;
extern SemaphoreHandle_t g_state_mutex;
