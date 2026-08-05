#pragma once
#include <cstdint>

enum class Language : uint8_t {
    EN_US = 0,
    PT_BR = 1,
    COUNT
};

enum class StrId : uint8_t {
    // Headers
    HEADER_FARM_HUB,
    HEADER_WATER_TANK,
    HEADER_SOLAR,
    HEADER_LOADS,
    HEADER_STATS,
    HEADER_SETTINGS,

    // Labels & Data
    LABEL_SENSOR,
    LABEL_BATTERY,
    LABEL_FLOAT_FULL,
    LABEL_FLOAT_EMPTY,
    LABEL_BACKUP_ON,
    LABEL_BACKUP_OFF,

    // Water Tank Submenu
    MENU_START_OTA,
    MENU_PUMP_MODE,
    MENU_CLEAR_STATS,
    MENU_BACK,

    // Boot
    BOOT_STARTING,

    // Settings Screen
    SETTINGS_LANGUAGE,
    SETTINGS_LANG_EN,
    SETTINGS_LANG_PT,

    COUNT
};
