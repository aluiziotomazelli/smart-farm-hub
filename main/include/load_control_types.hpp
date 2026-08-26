// main/include/load_control_types.hpp
#pragma once

#include <cstdint>
#include "farm_protocol_types.hpp"
#include "load_types.hpp"

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
 * @brief Telemetry snapshot from an actuator node.
 */
struct LoadStatusUpdate {
    LoadIndex load_index = LoadIndex::UNKNOWN;
    farm::NodeId node_id = farm::NodeId::UNKNOWN;
    uint8_t circuit_id = 0;
    farm::ControlMode control_mode = farm::ControlMode::UNKNOWN;
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
