// main/include/domain_controllers.hpp
#pragma once

#include "static_load_controller.hpp"

/**
 * @class FridgeController
 * @brief Domain controller for refrigerator circuits.
 */
class FridgeController : public StaticLoadController
{
public:
    explicit FridgeController(
        const LoadProfile& profile =
            {
                .expected_watts_running = 350,
                .expected_watts_idle = 30,
                .can_shed = true,
                .max_shed_duration_s = 20 * 60, // 20 minutes safe off time
                .is_continuous = true,
                .priority_rank = 3,
            })
        : StaticLoadController(LoadIndex::FRIDGE, profile, LoadUrgency::NORMAL, SourcePreference::SOLAR_PREFERRED)
    {
    }
};

/**
 * @class FreezerController
 * @brief Domain controller for freezer circuits.
 */
class FreezerController : public StaticLoadController
{
public:
    explicit FreezerController(
        const LoadProfile& profile =
            {
                .expected_watts_running = 700,
                .expected_watts_idle = 40,
                .can_shed = true,
                .max_shed_duration_s = 30 * 60, // 30 minutes safe off time
                .is_continuous = true,
                .priority_rank = 2,
            })
        : StaticLoadController(LoadIndex::FREEZER, profile, LoadUrgency::NORMAL, SourcePreference::SOLAR_PREFERRED)
    {
    }
};

/**
 * @class RouterController
 * @brief Domain controller for essential communications/networking.
 */
class RouterController : public StaticLoadController
{
public:
    explicit RouterController(
        const LoadProfile& profile =
            {
                .expected_watts_running = 75,
                .expected_watts_idle = 30,
                .can_shed = false,
                .max_shed_duration_s = 0, // Cannot be shed
                .is_continuous = true,
                .priority_rank = 1,
            })
        : StaticLoadController(LoadIndex::ROUTER, profile, LoadUrgency::CRITICAL, SourcePreference::SOLAR_PREFERRED)
    {
    }
};

/**
 * @class LightingController
 * @brief Domain controller for farm lighting circuits.
 */
class LightingController : public StaticLoadController
{
public:
    explicit LightingController(
        const LoadProfile& profile =
            {
                .expected_watts_running = 100,
                .expected_watts_idle = 0,
                .can_shed = true,
                .max_shed_duration_s = 0,
                .is_continuous = false,
                .priority_rank = 5,
            })
        : StaticLoadController(LoadIndex::LIGHTING, profile, LoadUrgency::SHEDDABLE, SourcePreference::SOLAR_ONLY)
    {
    }
};
