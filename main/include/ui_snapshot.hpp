// main/include/ui_snapshot.hpp
#pragma once

#include <array>
#include <climits>
#include <cstdint>
#include <mutex>

#include "farm_protocol_types.hpp"
#include "load_control_types.hpp"
#include "load_types.hpp"

/**
 * @struct UiSnapshotData
 * @brief Pure Plain-Old-Data snapshot containing real-time energy, tank, and load states for the UI.
 */
struct UiSnapshotData {
    // ─── Water Tank Telemetry ─────────────────────────────────────────
    int64_t last_water_update_ts{0};       ///< ms since boot (0 = never)
    uint16_t water_level_permille{0};      ///< Tank level 0..1000 ‰
    float water_distance_cm{0.0f};         ///< Ultrasonic measured distance
    uint16_t water_battery_mv{0};          ///< Node battery voltage in mV
    uint8_t water_battery_percent{0};      ///< Node battery 0..100%
    farm::BatteryState water_battery_state{farm::BatteryState::UNKNOWN};
    farm::SensorStatus water_sensor_status{farm::SensorStatus::UNKNOWN};
    bool water_float_switch_full{false};   ///< Float switch indicating tank full
    bool water_backup_mode{false};         ///< True if ultrasonic sensor failed and running on float switch
    uint64_t water_node_unix_time{0};      ///< Unix epoch from tank node in ms

    // ─── Solar Sensor Telemetry & Estimations ────────────────────────
    int64_t last_solar_update_ts{0};       ///< ms since boot (0 = never)
    uint16_t solar_isc_current_ma{0};      ///< Short-circuit current in mA
    uint16_t solar_irradiance_wm2{0};      ///< Irradiance in W/m²
    int16_t solar_panel_temp_c{INT16_MIN}; ///< Panel temperature in 0.1°C (INT16_MIN = absent)
    uint16_t solar_battery_mv{0};          ///< Node battery voltage in mV
    uint8_t solar_battery_percent{0};      ///< Node battery 0..100%
    farm::BatteryState solar_battery_state{farm::BatteryState::UNKNOWN};
    farm::SensorStatus solar_sensor_status{farm::SensorStatus::UNKNOWN};
    uint16_t solar_max_current_ma{0};      ///< Daily peak Isc
    uint32_t solar_daily_yield_mah{0};     ///< Daily yield integral from node (mAh)
    bool solar_is_night_mode{false};       ///< Sensor night mode flag
    uint64_t solar_node_unix_time{0};      ///< Unix epoch from solar node in ms
    uint16_t solar_power_w_instant{0};     ///< Hub-estimated total AC solar power (W)
    float solar_daily_yield_wh_hub{0.0f};  ///< Hub-accumulated daily energy (Wh)

    // ─── Energy Grid & Available Headroom ─────────────────────────────
    bool solar_available{true};
    bool grid_available{true};
    uint16_t total_solar_allocated_w{0};   ///< Total power currently assigned/running on solar
    int32_t solar_headroom_w{0};           ///< Remaining solar headroom in Watts

    // ─── Controlled Electrical Loads ──────────────────────────────────
    std::array<LoadState, static_cast<size_t>(LoadIndex::MAX)> loads{};
    std::array<EpisodicWindowState, static_cast<size_t>(LoadIndex::MAX)> load_window_states{};

    // ─── System / Hub Status ──────────────────────────────────────────
    bool wifi_connected{false};
    int8_t wifi_rssi{0};
    bool time_synced{false};
    int64_t last_ui_activity_ts{0};

    // ─── Helper Queries ───────────────────────────────────────────────
    const LoadState& load(LoadIndex idx) const
    {
        return loads[static_cast<size_t>(idx)];
    }

    LoadState& load(LoadIndex idx)
    {
        return loads[static_cast<size_t>(idx)];
    }

    EpisodicWindowState load_window_state(LoadIndex idx) const
    {
        return load_window_states[static_cast<size_t>(idx)];
    }

    uint16_t total_solar_consumption_w() const
    {
        uint16_t total = 0;
        for (const auto& l : loads) {
            if (l.load_state == farm::LoadState::RUNNING && l.active_source == farm::PowerSource::SOLAR) {
                total += l.power_w;
            }
        }
        return total;
    }

    int16_t power_margin_w() const
    {
        return static_cast<int16_t>(solar_power_w_instant) - static_cast<int16_t>(total_solar_consumption_w());
    }

    bool is_solar_night() const
    {
        return solar_is_night_mode;
    }
};

/**
 * @class UiSnapshot
 * @brief Thread-safe wrapper holding the latest snapshot data for display and external inspection.
 */
class UiSnapshot {
public:
    UiSnapshot() = default;

    /**
     * @brief Updates the entire snapshot atomically.
     * @param data Full snapshot data.
     */
    void update(const UiSnapshotData& data)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_ = data;
    }

    /**
     * @brief Retrieves a consistent copy of the current snapshot data.
     * @return Complete frozen UiSnapshotData copy.
     */
    UiSnapshotData get() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_;
    }

    /**
     * @brief Updates water tank telemetry section.
     */
    void update_water_tank(
        int64_t timestamp_ms,
        uint16_t level_permille,
        float distance_cm,
        uint16_t battery_mv,
        uint8_t battery_percent,
        farm::BatteryState battery_state,
        farm::SensorStatus sensor_status,
        bool float_switch_full,
        bool backup_mode,
        uint64_t unix_time)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.last_water_update_ts = timestamp_ms;
        data_.water_level_permille = level_permille;
        data_.water_distance_cm = distance_cm;
        data_.water_battery_mv = battery_mv;
        data_.water_battery_percent = battery_percent;
        data_.water_battery_state = battery_state;
        data_.water_sensor_status = sensor_status;
        data_.water_float_switch_full = float_switch_full;
        data_.water_backup_mode = backup_mode;
        data_.water_node_unix_time = unix_time;
    }

    /**
     * @brief Updates solar sensor telemetry section.
     */
    void update_solar(
        int64_t timestamp_ms,
        uint16_t isc_ma,
        uint16_t irradiance_wm2,
        int16_t temp_c,
        uint16_t battery_mv,
        uint8_t battery_percent,
        farm::BatteryState battery_state,
        farm::SensorStatus sensor_status,
        uint16_t max_current_ma,
        uint32_t daily_yield_mah,
        bool is_night,
        uint64_t unix_time,
        uint16_t power_w,
        float daily_yield_wh)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.last_solar_update_ts = timestamp_ms;
        data_.solar_isc_current_ma = isc_ma;
        data_.solar_irradiance_wm2 = irradiance_wm2;
        data_.solar_panel_temp_c = temp_c;
        data_.solar_battery_mv = battery_mv;
        data_.solar_battery_percent = battery_percent;
        data_.solar_battery_state = battery_state;
        data_.solar_sensor_status = sensor_status;
        data_.solar_max_current_ma = max_current_ma;
        data_.solar_daily_yield_mah = daily_yield_mah;
        data_.solar_is_night_mode = is_night;
        data_.solar_node_unix_time = unix_time;
        data_.solar_power_w_instant = power_w;
        data_.solar_daily_yield_wh_hub = daily_yield_wh;
    }

    /**
     * @brief Updates loads and energy balance from LoadControlTask.
     */
    void update_energy_and_loads(
        uint16_t solar_power_w,
        uint16_t total_solar_allocated_w,
        int32_t solar_headroom_w,
        bool solar_available,
        bool grid_available,
        const std::array<LoadState, static_cast<size_t>(LoadIndex::MAX)>& loads,
        const std::array<EpisodicWindowState, static_cast<size_t>(LoadIndex::MAX)>& window_states)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.solar_power_w_instant = solar_power_w;
        data_.total_solar_allocated_w = total_solar_allocated_w;
        data_.solar_headroom_w = solar_headroom_w;
        data_.solar_available = solar_available;
        data_.grid_available = grid_available;
        data_.loads = loads;
        data_.load_window_states = window_states;
    }

    /**
     * @brief Updates single load status in the snapshot.
     */
    void update_load(LoadIndex index, const LoadState& state, EpisodicWindowState window_state = EpisodicWindowState::IDLE)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t idx = static_cast<size_t>(index);
        if (idx < static_cast<size_t>(LoadIndex::MAX)) {
            data_.loads[idx] = state;
            data_.load_window_states[idx] = window_state;
        }
    }

    /**
     * @brief Updates Wi-Fi connection info.
     */
    void update_wifi(bool connected, int8_t rssi)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.wifi_connected = connected;
        data_.wifi_rssi = rssi;
    }

private:
    mutable std::mutex mutex_;
    UiSnapshotData data_{};
};
