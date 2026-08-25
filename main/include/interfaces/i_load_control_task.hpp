// main/include/interfaces/i_load_control_task.hpp
#pragma once

#include "esp_err.h"
#include "load_control_types.hpp"
#include "load_types.hpp"

namespace hub {

/**
 * @interface ILoadControlTask
 * @brief Asynchronous entrypoint for posting events to the Load Control Task.
 */
class ILoadControlTask {
public:
    virtual ~ILoadControlTask() = default;

    /**
     * @brief Posts latest solar generation update to the LCT (overwriting previous).
     * @param update Solar power and irradiance snapshot.
     * @return ESP_OK on success, ESP_FAIL on queue error.
     */
    virtual esp_err_t post_solar_update(const SolarPowerUpdate& update) = 0;

    /**
     * @brief Posts a load operational intent request (queued FIFO).
     * @param intent Intent from a domain controller (e.g. TankController).
     * @return ESP_OK on success, ESP_ERR_NO_MEM / ESP_FAIL if queue full.
     */
    virtual esp_err_t post_load_intent(const LoadIntent& intent) = 0;

    /**
     * @brief Posts reported status from an actuator node (overwriting previous for that load).
     * @param status Reported actuator load state.
     * @return ESP_OK on success, ESP_FAIL on error.
     */
    virtual esp_err_t post_load_status(const LoadStatusUpdate& status) = 0;

    /**
     * @brief Notifies the LCT of energy source availability changes (solar/grid).
     * @param solar_available True if inverter / solar bus is energized.
     * @param grid_available True if AC utility grid is available.
     * @return ESP_OK on success.
     */
    virtual esp_err_t notify_energy_availability(bool solar_available, bool grid_available) = 0;
};

} // namespace hub
