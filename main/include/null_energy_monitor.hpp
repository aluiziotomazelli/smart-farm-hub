// main/include/null_energy_monitor.hpp
#pragma once

#include "interfaces/i_energy_monitor.hpp"

/**
 * @class NullEnergyMonitor
 * @brief Default fallback implementation of IEnergyMonitor that always reports power available.
 *
 * Useful for test benches, early development, or hardware configurations without physical grid/solar voltage presence sensors.
 */
class NullEnergyMonitor : public IEnergyMonitor
{
public:
    NullEnergyMonitor() = default;
    ~NullEnergyMonitor() override = default;

    /**
     * @brief Always returns true (solar assumed available).
     */
    bool is_solar_available() const override
    {
        return true;
    }

    /**
     * @brief Always returns true (grid assumed available).
     */
    bool is_grid_available() const override
    {
        return true;
    }
};
