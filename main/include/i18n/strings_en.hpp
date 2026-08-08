#pragma once
#include "language.hpp"

inline constexpr const char* STRINGS_EN[static_cast<size_t>(StrId::COUNT)] = {
    "FARM HUB",         // HEADER_FARM_HUB
    "WATER-TANK",       // HEADER_WATER_TANK
    "SOLAR GENERATION", // HEADER_SOLAR
    "LOADS SUMMARY",    // HEADER_LOADS
    "STATS",            // HEADER_STATS
    "SETTINGS",         // HEADER_SETTINGS
    "Sensor",           // LABEL_SENSOR
    "Reading",          // LABEL_READING
    "Float",            // LABEL_FLOAT
    "Bat",              // LABEL_BATTERY
    "FULL",             // LABEL_FLOAT_FULL
    "EMPTY",            // LABEL_FLOAT_EMPTY
    "SENSOR FAIL",      // LABEL_SENSOR_FAILED
    "ON",               // LABEL_BACKUP_ON
    "OFF",              // LABEL_BACKUP_OFF
    "1. Last Report",    // MENU_LAST_REPORT
    "2. ESP-NOW Stats",  // MENU_ESPNOW_STATS
    "3. Request Report", // MENU_REQUEST_REPORT
    "4. Info",         // MENU_CONFIG
    "5. Clear Stats",    // MENU_CLEAR_STATS
    "6. Reboot Node",    // MENU_REBOOT_NODE
    "7. Start OTA",      // MENU_START_OTA
    "< Back",            // MENU_BACK
    "STARTING...",      // BOOT_STARTING
    "Language",         // SETTINGS_LANGUAGE
    "English",          // SETTINGS_LANG_EN
    "Portuguese",       // SETTINGS_LANG_PT
};
