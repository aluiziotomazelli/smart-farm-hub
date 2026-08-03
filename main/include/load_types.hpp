// main/include/load_types.hpp
#pragma once

#include <cstdint>

#include "farm_protocol_types.hpp"

/**
 * @brief Logical index into the SystemState::loads[] array.
 *
 * Provides semantic access to each controlled or monitored load.
 * Add future loads before MAX and update MAX accordingly.
 */
enum class LoadIndex : uint8_t {
    PUMP = 0,     ///< Water pump (motobomba) — primary solar load
    FRIDGE = 1,   ///< Refrigerator
    FREEZER = 2,  ///< Freezer
    ROUTER = 3,   ///< Network router
    LIGHTING = 4, ///< General lighting circuit
    MAX = 8,      ///< Array capacity (leave headroom for expansion)
};

/**
 * @brief Real-time state of a single controllable or monitored load.
 *
 * Populated by actuator status reports received via ESP-NOW.
 * The actuator is the source of truth for power_w — it reports either
 * a measured value (if it has a current sensor) or its own nominal constant.
 * The hub remains agnostic about how the value was obtained.
 */
struct LoadState {
    farm::ControlMode control_mode = farm::ControlMode::OFF;
    farm::PowerSource active_source = farm::PowerSource::UNKNOWN;
    uint16_t power_w = 0;           ///< Current consumption in Watts (0 if OFF)
    int64_t last_update_ts = 0;     ///< ms since boot of last status report (0 = never)
    bool hub_authorized = false;    ///< Hub granted ON authorization (AUTO mode only)
};
