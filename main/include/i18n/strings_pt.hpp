#pragma once
#include "language.hpp"

inline constexpr const char* STRINGS_PT[static_cast<size_t>(StrId::COUNT)] = {
    "CENTRAL",         // HEADER_FARM_HUB
    "CAIXA D'AGUA",    // HEADER_WATER_TANK
    "GERACAO SOLAR",   // HEADER_SOLAR
    "CARGAS",          // HEADER_LOADS
    "ESTATISTICAS",    // HEADER_STATS
    "CONFIG",          // HEADER_SETTINGS
    "Sensor",          // LABEL_SENSOR
    "Leitura",         // LABEL_READING
    "Boia",            // LABEL_FLOAT
    "Bat",             // LABEL_BATTERY
    "CHEIA",           // LABEL_FLOAT_FULL
    "VAZIA",           // LABEL_FLOAT_EMPTY
    "FALHA SENSOR",    // LABEL_SENSOR_FAILED
    "ON",              // LABEL_BACKUP_ON
    "OFF",             // LABEL_BACKUP_OFF
    "1. Iniciar OTA",  // MENU_START_OTA
    "2. Modo Bomba",   // MENU_PUMP_MODE
    "3. Limpar Stats", // MENU_CLEAR_STATS
    "< Voltar",        // MENU_BACK
    "INICIANDO...",    // BOOT_STARTING
    "Idioma",          // SETTINGS_LANGUAGE
    "Ingles",          // SETTINGS_LANG_EN
    "Portugues",       // SETTINGS_LANG_PT
};
