// main/include/interfaces/i_energy_monitor.hpp
#pragma once

#include <cstdint>

/**
 * @interface IEnergyMonitor
 * @brief Interface for grid and solar physical voltage presence monitoring.
 */
class IEnergyMonitor
{
public:
    virtual ~IEnergyMonitor() = default;

    /**
     * @brief Checks if solar inverter output voltage is present.
     * @return true if solar AC/DC output is detected and available, false otherwise.
     */
    virtual bool is_solar_available() const = 0;

    /**
     * @brief Checks if public utility grid voltage is present.
     * @return true if grid power is available, false otherwise.
     */
    virtual bool is_grid_available() const = 0;
};
