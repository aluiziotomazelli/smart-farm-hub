// main/include/load_profiles.hpp
#pragma once

#include "load_control_types.hpp"

namespace farm_loads {

/**
 * @brief Pump (motobomba de recalque): episodic load, 320W running, 5s switch interval, 10min max wait for solar window.
 */
static constexpr LoadProfile PUMP = {
    .expected_watts_running = 320,
    .expected_watts_idle = 0,
    .can_shed = false,
    .max_shed_duration_s = 0,
    .min_switch_interval_s = 5,
    .max_wait_window_s = 600,
    .is_continuous = false, // Carga episódica
    .priority_rank = 4,
};

/**
 * @brief Refrigerator (geladeira): continuous load with thermal cycle, 350W running, 30W idle, 20min max shed.
 */
static constexpr LoadProfile FRIDGE = {
    .expected_watts_running = 350,
    .expected_watts_idle = 30,
    .can_shed = true,
    .max_shed_duration_s = 20 * 60,
    .min_switch_interval_s = 180,
    .max_wait_window_s = 0,
    .is_continuous = true,
    .priority_rank = 3,
};

/**
 * @brief Freezer: continuous load with thermal cycle, 700W running, 40W idle, 30min max shed.
 */
static constexpr LoadProfile FREEZER = {
    .expected_watts_running = 700,
    .expected_watts_idle = 40,
    .can_shed = true,
    .max_shed_duration_s = 30 * 60,
    .min_switch_interval_s = 180,
    .max_wait_window_s = 0,
    .is_continuous = true,
    .priority_rank = 2,
};

/**
 * @brief Router / Comms link: continuous load, unsheddable, 75W running, 30W idle.
 */
static constexpr LoadProfile ROUTER = {
    .expected_watts_running = 75,
    .expected_watts_idle = 30,
    .can_shed = false,
    .max_shed_duration_s = 0,
    .min_switch_interval_s = 600,
    .max_wait_window_s = 0,
    .is_continuous = true,
    .priority_rank = 1,
};

/**
 * @brief General farm lighting: sheddable episodic load, 100W running.
 */
static constexpr LoadProfile LIGHTING = {
    .expected_watts_running = 100,
    .expected_watts_idle = 0,
    .can_shed = true,
    .max_shed_duration_s = 0,
    .min_switch_interval_s = 5,
    .max_wait_window_s = 0,
    .is_continuous = false,
    .priority_rank = 5,
};

/**
 * @brief Returns the pre-configured priority configuration populated with all default farm load profiles.
 */
inline PriorityConfig get_default_priority_config()
{
    PriorityConfig cfg{};
    cfg.profiles[static_cast<size_t>(LoadIndex::PUMP)] = PUMP;
    cfg.profiles[static_cast<size_t>(LoadIndex::FRIDGE)] = FRIDGE;
    cfg.profiles[static_cast<size_t>(LoadIndex::FREEZER)] = FREEZER;
    cfg.profiles[static_cast<size_t>(LoadIndex::ROUTER)] = ROUTER;
    cfg.profiles[static_cast<size_t>(LoadIndex::LIGHTING)] = LIGHTING;
    return cfg;
}

} // namespace farm_loads
