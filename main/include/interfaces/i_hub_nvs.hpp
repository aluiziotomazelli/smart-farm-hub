#pragma once
#include "esp_err.h"
#include "hub_stats.hpp"

/**
 * @class IHubNvs
 * @brief Interface for persisting water tank application state and statistics.
 *
 * Abstracts both NVS and RTC memory storage, allowing the logic to be tested
 * without physical non-volatile storage.
 */
class IHubNvs
{
public:
    virtual ~IHubNvs() = default;

    /**
     * @brief Loads the application statistics and state.
     * @param[out] stats The struct to populate with loaded data.
     * @return ESP_OK on success, or an error code.
     */
    virtual esp_err_t load_app_data(HubStats& stats) = 0;

    /**
     * @brief Persists the application statistics and state.
     * @param[in] stats The struct containing the data to save.
     * @return ESP_OK on success, or an error code.
     */
    virtual esp_err_t save_app_data(const HubStats& stats, bool force_nvs_commit = false) = 0;
};
