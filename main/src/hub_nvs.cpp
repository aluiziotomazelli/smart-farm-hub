// main/src/hub_nvs.cpp
#include "hub_nvs.hpp"
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char *TAG      = "HubNvs";
static const char *KEY_STATS = "hub_stats";

HubNvs::HubNvs(idf_hals::INvsHAL &hal) : NvsCore("hub_ns", hal) {}

esp_err_t HubNvs::loadAppData()
{
    esp_err_t err = loadStruct(KEY_STATS, stats);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No saved stats, using defaults");
        setAppDefaults();
        return ESP_OK;
    }
    return err;
}

esp_err_t HubNvs::saveAppData()   { return saveStruct(KEY_STATS, stats); }
void      HubNvs::setAppDefaults() { stats = HubStats{}; }
