#pragma once

#include "interfaces/i_persistence_backend.hpp"
#include "interfaces/i_hub_nvs.hpp"
#include "hub_stats.hpp"

#include <type_traits>
#include <cstddef>
#include "esp_rom_crc.h"

/**
 * @class HubNvs
 * @brief Persistent storage handler for the Hub application.
 */
class HubNvs : public IHubNvs
{
public:
    HubNvs(IPersistenceBackend& rtc_stats, IPersistenceBackend& nvs_stats);
    virtual ~HubNvs() override = default;

    esp_err_t load_app_data(HubStats& stats) override;
    esp_err_t save_app_data(const HubStats& stats, bool force_nvs_commit = false) override;

private:
    IPersistenceBackend& rtc_stats_;
    IPersistenceBackend& nvs_stats_;

    esp_err_t load_raw_app_data(HubStats& data_out);
    esp_err_t validate_app_data(const HubStats& data);
    bool is_app_data_dirty(const HubStats& new_data) const;

    /**
     * @brief Calculates the CRC of the given data.
     * @tparam T The type of the data to calculate the CRC of.
     * @param data The data to calculate the CRC of.
     * @return The CRC of the given data.
     */
    template <typename T> uint32_t calculate_crc(const T& data)
    {
        static_assert(std::is_standard_layout_v<T>, "T must be standard_layout for safe offset calculation");
        static_assert(offsetof(T, crc) != 0, "T must have a non-first crc field");

        return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&data), offsetof(T, crc));
    }
};
