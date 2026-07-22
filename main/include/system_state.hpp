#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>

struct SystemState {
    // Water Tank
    uint16_t water_level_permille = 0;
    float water_distance_cm = 0.0f;
    uint32_t last_water_update_ts = 0; // esp_timer_get_time() when received

    // WiFi & ESP-NOW
    bool wifi_connected = false;
    int8_t wifi_rssi = 0;
    
    uint8_t espnow_peers = 0;
    int8_t espnow_last_rssi = 0; 
    int8_t espnow_avg_rssi = 0; 
    
    // Stats
    uint32_t messages_sent = 0;
    uint32_t messages_lost = 0;
    uint32_t last_rtt_ms = 0;

    // OTA
    bool ota_in_progress = false;
};

extern SystemState g_system_state;
extern SemaphoreHandle_t g_state_mutex;
