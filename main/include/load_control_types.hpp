// main/include/load_control_types.hpp
#pragma once

#include <cstdint>
#include "farm_protocol_types.hpp"

/**
 * @brief Logical index into the UiSnapshotData::loads[] array and LoadControlEngine.
 *
 * Provides semantic access to each controlled or monitored load.
 * Add future loads before MAX and update MAX accordingly.
 */
enum class LoadIndex : uint8_t {
    PUMP = 0,       ///< Water pump (motobomba) — primary solar load
    FRIDGE = 1,     ///< Refrigerator
    FREEZER = 2,    ///< Freezer
    ROUTER = 3,     ///< Network router
    LIGHTING = 4,   ///< General lighting circuit
    MAX = 8,        ///< Array capacity (contiguous indices 0..7)
    UNKNOWN = 0xFF, ///< Unassigned, invalid, or uninitialized load
};

/**
 * @struct LoadUiSnapshot
 * @brief Consolidated presentation and telemetry snapshot of a load for UI and display rendering.
 *
 * Populated by LoadControlTask periodically into the UiSnapshot.
 * The UIController and OLED rendering screens consume this struct directly.
 */
struct LoadUiSnapshot {
    farm::NodeId node_id = farm::NodeId::UNKNOWN;
    uint8_t circuit_id = 0;
    farm::ControlMode control_mode = farm::ControlMode::UNKNOWN;
    farm::PowerSource selected_source = farm::PowerSource::UNKNOWN;
    farm::PowerSource active_source = farm::PowerSource::UNKNOWN;
    farm::LoadState load_state = farm::LoadState::IDLE;
    uint16_t power_w = 0;           ///< Current consumption in Watts (0 if IDLE)
    uint32_t runtime_s = 0;         ///< Current active cycle runtime in seconds
    uint32_t uptime_s = 0;          ///< Node lifetime uptime in seconds
    uint64_t unix_time = 0;         ///< Node unix epoch time in ms
    int64_t last_update_ts = 0;     ///< ms since boot of last status report (0 = never)
    bool hub_authorized = false;    ///< Hub granted ON authorization (AUTO mode only)
};

/**
 * @brief Desired operational state emitted by a load domain controller.
 */
enum class LoadDesiredState : uint8_t {
    OFF = 0, ///< Controller wants the load off / disconnected
    ON = 1,  ///< Controller wants the load on / connected
};

/**
 * @brief Power source preference for a load request.
 */
enum class SourcePreference : uint8_t {
    SOLAR_ONLY = 0,      ///< Only run if solar surplus is available
    SOLAR_PREFERRED = 1, ///< Prefer solar, fallback to grid if needed
    ANY = 2,             ///< Indifferent (run on whichever is available)
};

/**
 * @brief Urgency tier for load scheduling and energy arbitration.
 */
enum class LoadUrgency : uint8_t {
    SHEDDABLE = 0,     ///< Lowest priority; first to be cut or deferred
    OPPORTUNISTIC = 1, ///< Run only if free surplus exists; no cost impact
    NORMAL = 2,        ///< Standard operation; can wait or use grid if necessary
    CRITICAL = 3,      ///< Top priority; highest urgency, cannot be shed
};

/**
 * @brief Static configuration / thermal baseline for a load.
 */
struct LoadProfile {
    uint16_t expected_watts_running = 0; ///< Typical active consumption (W)
    uint16_t expected_watts_idle = 0;    ///< Idle / stand-by consumption (W)
    bool can_shed = false;               ///< Whether load can be shed in emergency
    uint32_t max_shed_duration_s = 0;    ///< Max safe unpowered duration (seconds)
    uint32_t min_switch_interval_s = 5;  ///< Min seconds between source changes (contactor protection)
    uint32_t max_wait_window_s = 600;    ///< For episodic loads: max wait for solar window before fallback (seconds)
    bool is_continuous = true;           ///< True for always-on loads, false for episodic (pump)
    uint8_t priority_rank = 10;          ///< Tiebreaker rank (lower = higher priority)
};

/**
 * @brief Intent emitted by any domain controller to the Load Control Task (LCT).
 */
struct LoadIntent {
    LoadIndex load_index = LoadIndex::UNKNOWN;
    LoadDesiredState desired_state = LoadDesiredState::OFF;
    SourcePreference source_preference = SourcePreference::SOLAR_PREFERRED;
    LoadUrgency urgency = LoadUrgency::NORMAL;
    uint32_t max_hold_duration_s = 0;     ///< Max safe off-time (0 = cannot hold)
    uint32_t estimated_on_duration_s = 0; ///< For episodic loads (e.g., pump fill time in seconds)
};

/**
 * @brief Priority configuration table per load for LCT arbitration.
 */
struct PriorityConfig {
    LoadProfile profiles[static_cast<size_t>(LoadIndex::MAX)];
};

/**
 * @struct SolarPowerUpdate
 * @brief Telemetry snapshot from solar sensor node for energy arbitration.
 */
struct SolarPowerUpdate {
    uint16_t power_w = 0;          ///< AC solar estimated generation in Watts
    uint16_t irradiance_wm2 = 0;   ///< Solar irradiance in W/m²
    bool is_night_mode = false;    ///< True if solar sensor indicates night mode
    int64_t timestamp_ms = 0;      ///< Local time of telemetry in ms
};

/**
 * @struct LoadStatusUpdate
 * @brief Telemetry snapshot from an actuator node (received via ESP-NOW queue).
 */
struct LoadStatusUpdate {
    LoadIndex load_index = LoadIndex::UNKNOWN;
    farm::NodeId node_id = farm::NodeId::UNKNOWN;
    uint8_t circuit_id = 0;
    farm::ControlMode control_mode = farm::ControlMode::UNKNOWN;
    farm::PowerSource selected_source = farm::PowerSource::UNKNOWN;
    farm::PowerSource active_source = farm::PowerSource::UNKNOWN;
    farm::LoadState load_state = farm::LoadState::IDLE;
    uint16_t power_w = 0;           ///< Reported consumption in Watts
    uint32_t runtime_s = 0;         ///< Current cycle runtime in seconds
    int64_t timestamp_ms = 0;       ///< Local timestamp of update
};

/**
 * @struct LoadControlDecision
 * @brief Output action decided by the LoadControlEngine for a specific load.
 */
struct LoadControlDecision {
    LoadIndex load_index = LoadIndex::UNKNOWN;
    farm::NodeId node_id = farm::NodeId::UNKNOWN;
    uint8_t circuit_id = 0;
    bool should_be_on = false;
    farm::PowerSource target_source = farm::PowerSource::UNKNOWN;
    uint32_t watchdog_s = 0;        ///< Watchdog timeout for actuator (0 = infinite / default)
    bool action_required = false;   ///< True if this differs from actuator's current reported state
};

/**
 * @enum class EpisodicWindowState
 * @brief FSM state for episodic load solar window optimization (e.g. pumps, irrigation).
 */
enum class EpisodicWindowState : uint8_t {
    IDLE = 0,               ///< No run requested
    WAITING_FOR_WINDOW = 1, ///< Waiting for thermal/compressor off-cycle window
    RUNNING_WINDOW = 2,     ///< Running on solar within captured window
    RUNNING_DIRECT = 3,     ///< Running directly on solar (ample continuous headroom)
    RUNNING_GRID = 4,       ///< Running on grid fallback
};

/**
 * @struct EngineLoadEntry
 * @brief Full tracking state of a load within the LoadControlEngine.
 */
struct EngineLoadEntry {
    LoadIndex load_index = LoadIndex::PUMP;
    LoadProfile profile;
    LoadIntent intent;
    LoadStatusUpdate last_status;
    farm::PowerSource assigned_source = farm::PowerSource::UNKNOWN;
    bool assigned_on = false;
    uint32_t current_watchdog_s = 0;
    int64_t last_source_switch_ts = 0;

    // Episodic load solar window FSM state
    EpisodicWindowState window_state = EpisodicWindowState::IDLE;
    int64_t window_wait_start_ts = 0;
    int64_t window_run_start_ts = 0;
};
