// main/include/domain_controllers.hpp
#pragma once

#include "load_profiles.hpp"
#include "static_load_controller.hpp"

/**
 * @class FridgeController
 * @brief Domain controller for refrigerator circuits.
 */
class FridgeController : public StaticLoadController
{
public:
    explicit FridgeController(const LoadProfile& profile = farm_loads::FRIDGE)
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
    explicit FreezerController(const LoadProfile& profile = farm_loads::FREEZER)
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
    explicit RouterController(const LoadProfile& profile = farm_loads::ROUTER)
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
    explicit LightingController(const LoadProfile& profile = farm_loads::LIGHTING)
        : StaticLoadController(LoadIndex::LIGHTING, profile, LoadUrgency::SHEDDABLE, SourcePreference::SOLAR_ONLY)
    {
    }
};
