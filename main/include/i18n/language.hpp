#pragma once
#include <cstdint>

/**
 * @file language.hpp
 * @brief Multi-language internationalization (i18n) identifiers and types.
 */

/**
 * @brief Supported display languages for the Hub user interface.
 */
enum class Language : uint8_t {
    EN_US = 0, ///< English (United States) - Default
    PT_BR = 1, ///< Portuguese (Brazil)
    COUNT      ///< Sentinel value representing total number of supported languages
};

/**
 * @brief Unique string identifiers for localized user interface text elements.
 */
enum class StrId : uint8_t {
    // Headers
    HEADER_FARM_HUB,   ///< Main Hub screen header
    HEADER_WATER_TANK, ///< Water tank screen header
    HEADER_SOLAR,      ///< Solar generation screen header
    HEADER_LOADS,      ///< Loads summary screen header
    HEADER_STATS,      ///< Statistics screen header
    HEADER_SETTINGS,   ///< Settings screen header

    // Labels & Data
    LABEL_SENSOR,      ///< Sensor status label
    LABEL_BATTERY,     ///< Battery level label
    LABEL_FLOAT_FULL,  ///< Float switch FULL state text
    LABEL_FLOAT_EMPTY, ///< Float switch EMPTY state text
    LABEL_BACKUP_ON,   ///< Backup mode ON state text
    LABEL_BACKUP_OFF,  ///< Backup mode OFF state text

    // Water Tank Submenu
    MENU_START_OTA,    ///< Start OTA update menu item
    MENU_PUMP_MODE,    ///< Pump mode configuration menu item
    MENU_CLEAR_STATS,  ///< Clear statistics menu item
    MENU_BACK,         ///< Back to previous screen menu item

    // Boot
    BOOT_STARTING,     ///< Boot splash screen status text

    // Settings Screen
    SETTINGS_LANGUAGE, ///< Language setting label
    SETTINGS_LANG_EN,  ///< English language selection text
    SETTINGS_LANG_PT,  ///< Portuguese language selection text

    COUNT              ///< Sentinel value representing total number of localized strings
};

