// main/include/hub_nvs.hpp
#pragma once

#include "app_storage.hpp"
#include "interfaces/i_persistence_backend.hpp"
#include "interfaces/i_hub_nvs.hpp"
#include "hub_stats.hpp"

/**
 * @class HubNvs
 * @brief Persistent storage handler for the Hub application.
 */
class HubNvs : public IHubNvs,
               public AppStorage<HubStats, HUB_STATS_MAGIC, HUB_STATS_VERSION>
{
public:
    HubNvs(IPersistenceBackend& rtc_stats, IPersistenceBackend& nvs_stats)
        : AppStorage<HubStats, HUB_STATS_MAGIC, HUB_STATS_VERSION>(rtc_stats, nvs_stats, "HubNvs")
    {
    }

    /** @copydoc IHubNvs::init_app_data */
    esp_err_t init_app_data(HubStats& stats, const HubStats& default_stats) override
    {
        return init_app_data_impl(stats, default_stats);
    }

    /** @copydoc IHubNvs::load_app_data */
    esp_err_t load_app_data(HubStats& stats) override
    {
        return load_app_data_impl(stats);
    }

    /** @copydoc IHubNvs::save_app_data */
    esp_err_t save_app_data(const HubStats& stats, bool force_nvs_commit = false) override
    {
        return save_app_data_impl(stats, force_nvs_commit);
    }
};
