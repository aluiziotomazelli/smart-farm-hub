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
    "Reading",           // LABEL_READING
    "Float",             // LABEL_FLOAT
    "Bat",               // LABEL_BATTERY
    "FULL",              // LABEL_FLOAT_FULL
    "EMPTY",             // LABEL_FLOAT_EMPTY
    "SENSOR FAIL",       // LABEL_SENSOR_FAILED
    "ON",                // LABEL_BACKUP_ON
    "OFF",               // LABEL_BACKUP_OFF
    "1. Last Report",    // MENU_LAST_REPORT
    "2. ESP-NOW Stats",  // MENU_ESPNOW_STATS
    "3. Request Report", // MENU_REQUEST_REPORT
    "4. Info",           // MENU_CONFIG
    "5. Clear Stats",    // MENU_CLEAR_STATS
    "6. Reboot Node",    // MENU_REBOOT_NODE
    "7. Start OTA",      // MENU_START_OTA
    "< Back",            // MENU_BACK
    "STARTING...",       // BOOT_STARTING
    "Language",          // SETTINGS_LANGUAGE
    "English",           // SETTINGS_LANG_EN
    "Portuguese",        // SETTINGS_LANG_PT
    "Pair ESP-NOW",      // SETTINGS_PAIRING
    "Pairing",           // SETTINGS_PAIRING_ACTIVE
    "[SOL] LAST REPORT", // HEADER_SOLAR_REPORT
    "Irradiance",        // SOLAR_LABEL_IRRADIANCE
    "Isc",               // SOLAR_LABEL_ISC
    "Panel Temp",        // SOLAR_LABEL_TEMP
    "Yield",             // SOLAR_LABEL_YIELD
    "NIGHT",             // SOLAR_LABEL_NIGHT
    "WATER PUMP",        // HEADER_PUMP
    "[PUMP] LAST REPORT",// HEADER_PUMP_REPORT
    "Auto",              // LABEL_AUTO
    "Lock",              // LABEL_LOCK
    "Man",               // LABEL_MAN
    "Solar",             // LABEL_SOLAR
    "Grid",              // LABEL_GRID
    "Time",              // LABEL_RUNTIME
    "RUNNING",           // STATUS_RUNNING
    "IDLE",              // STATUS_IDLE
    "TIMEOUT",           // STATUS_TIMEOUT
    "FAULT",             // STATUS_FAULT
};
