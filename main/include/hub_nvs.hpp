// main/include/hub_nvs.hpp
#pragma once
#include "nvs_core.hpp"
#include "hal_nvs.hpp"
#include "hub_stats.hpp"

class HubNvs : public NvsCore {
public:
    explicit HubNvs(idf_hals::INvsHAL &hal);

    HubStats stats;

protected:
    esp_err_t loadAppData()    override;
    esp_err_t saveAppData()    override;
    void      setAppDefaults() override;
};
