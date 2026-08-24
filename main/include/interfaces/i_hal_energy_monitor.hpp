// main/include/interfaces/i_hal_energy_monitor.hpp
#pragma once

#include <cstdint>

namespace idf_hals {

/**
 * @interface IHalEnergyMonitor
 * @brief Hardware Abstraction Layer for grid/solar physical presence sensing.
 */
class IHalEnergyMonitor
{
public:
    virtual ~IHalEnergyMonitor() = default;

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

} // namespace idf_hals
