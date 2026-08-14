#pragma once
#include <cstdint>

/**
 * @file language.hpp
 * @brief Multi-language internationalization (i18n) identifiers and types.
 */

/**
 * @brief Supported display languages for the Hub user interface.
 */
enum class Language : uint8_t
{
    EN_US = 0, ///< English (United States) - Default
    PT_BR = 1, ///< Portuguese (Brazil)
    COUNT      ///< Sentinel value representing total number of supported languages
};

/**
 * @brief Unique string identifiers for localized user interface text elements.
 */
enum class StrId : uint8_t
{
    // Headers
    HEADER_FARM_HUB,   ///< Main Hub screen header
    HEADER_WATER_TANK, ///< Water tank screen header
    HEADER_SOLAR,      ///< Solar generation screen header
    HEADER_LOADS,      ///< Loads summary screen header
    HEADER_STATS,      ///< Statistics screen header
    HEADER_SETTINGS,   ///< Settings screen header

    // Labels & Data
    LABEL_SENSOR,        ///< Sensor status label
    LABEL_READING,       ///< Reading status label
    LABEL_FLOAT,         ///< Float switch label
    LABEL_BATTERY,       ///< Battery level label
    LABEL_FLOAT_FULL,    ///< Float switch FULL state text
    LABEL_FLOAT_EMPTY,   ///< Float switch EMPTY state text
    LABEL_SENSOR_FAILED, ///< Sensor failed warning text
    LABEL_BACKUP_ON,     ///< Backup mode ON state text
    LABEL_BACKUP_OFF,    ///< Backup mode OFF state text

    // Universal Node Submenu Items
    MENU_LAST_REPORT,    ///< 1. Last Report menu item
    MENU_ESPNOW_STATS,   ///< 2. ESP-NOW Stats menu item
    MENU_REQUEST_REPORT, ///< 3. Request Report menu item
    MENU_CONFIG,         ///< 4. Config menu item
    MENU_CLEAR_STATS,    ///< 5. Clear Stats menu item
    MENU_REBOOT_NODE,    ///< 6. Reboot Node menu item
    MENU_START_OTA,      ///< 7. Start OTA menu item
    MENU_BACK,           ///< 8. Back menu item

    // Boot
    BOOT_STARTING, ///< Boot splash screen status text

    // Settings Screen
    SETTINGS_LANGUAGE,       ///< Language setting label
    SETTINGS_LANG_EN,        ///< English language selection text
    SETTINGS_LANG_PT,        ///< Portuguese language selection text
    SETTINGS_PAIRING,        ///< ESP-NOW pairing setting item
    SETTINGS_PAIRING_ACTIVE, ///< ESP-NOW pairing active prefix

    COUNT ///< Sentinel value representing total number of localized strings
};
