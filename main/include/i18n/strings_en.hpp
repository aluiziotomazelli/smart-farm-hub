#pragma once
#include "language.hpp"

inline constexpr const char* STRINGS_EN[static_cast<size_t>(StrId::COUNT)] = {
    "FARM HUB",          // HEADER_FARM_HUB
    "WATER-TANK",        // HEADER_WATER_TANK
    "SOLAR GENERATION",  // HEADER_SOLAR
    "LOADS SUMMARY",     // HEADER_LOADS
    "STATS",             // HEADER_STATS
    "SETTINGS",          // HEADER_SETTINGS
    "Sensor",            // LABEL_SENSOR
    "Bat",               // LABEL_BATTERY
    "FULL",              // LABEL_FLOAT_FULL
    "EMPT",              // LABEL_FLOAT_EMPTY
    "ON",                // LABEL_BACKUP_ON
    "OFF",               // LABEL_BACKUP_OFF
    "1. Start OTA",      // MENU_START_OTA
    "2. Pump Mode",      // MENU_PUMP_MODE
    "3. Clear Stats",    // MENU_CLEAR_STATS
    "< Back",            // MENU_BACK
    "STARTING...",       // BOOT_STARTING
    "Language",          // SETTINGS_LANGUAGE
    "English",           // SETTINGS_LANG_EN
    "Portuguese",        // SETTINGS_LANG_PT
};
