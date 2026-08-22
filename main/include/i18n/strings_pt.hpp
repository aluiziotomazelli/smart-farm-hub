#pragma once
#include "language.hpp"

inline constexpr const char* STRINGS_PT[static_cast<size_t>(StrId::COUNT)] = {
    "CENTRAL",              // HEADER_FARM_HUB
    "CAIXA D'AGUA",         // HEADER_WATER_TANK
    "GERACAO SOLAR",        // HEADER_SOLAR
    "CARGAS",               // HEADER_LOADS
    "ESTATISTICAS",         // HEADER_STATS
    "CONFIG",               // HEADER_SETTINGS
    "Sensor",               // LABEL_SENSOR
    "Leitura",              // LABEL_READING
    "Boia",                 // LABEL_FLOAT
    "Bat",                  // LABEL_BATTERY
    "CHEIA",                // LABEL_FLOAT_FULL
    "VAZIA",                // LABEL_FLOAT_EMPTY
    "FALHA SENSOR",         // LABEL_SENSOR_FAILED
    "ON",                   // LABEL_BACKUP_ON
    "OFF",                  // LABEL_BACKUP_OFF
    "1. Ultima Telemetria", // MENU_LAST_REPORT
    "2. Stats ESP-NOW",     // MENU_ESPNOW_STATS
    "3. Solicitar Dados",   // MENU_REQUEST_REPORT
    "4. Info",              // MENU_CONFIG
    "5. Limpar Stats",      // MENU_CLEAR_STATS
    "6. Reiniciar No",      // MENU_REBOOT_NODE
    "7. Iniciar OTA",       // MENU_START_OTA
    "< Voltar",             // MENU_BACK
    "INICIANDO...",         // BOOT_STARTING
    "Idioma",               // SETTINGS_LANGUAGE
    "Ingles",               // SETTINGS_LANG_EN
    "Portugues",            // SETTINGS_LANG_PT
    "Parear ESP-NOW",       // SETTINGS_PAIRING
    "Pareando",             // SETTINGS_PAIRING_ACTIVE
    "[SOL] ULT. REPORT",    // HEADER_SOLAR_REPORT
    "Irradiancia",          // SOLAR_LABEL_IRRADIANCE
    "Isc",                  // SOLAR_LABEL_ISC
    "Temp Painel",          // SOLAR_LABEL_TEMP
    "Producao",             // SOLAR_LABEL_YIELD
    "NOITE",                // SOLAR_LABEL_NIGHT
    "BOMBA D'AGUA",         // HEADER_PUMP
    "Auto",                 // LABEL_AUTO
    "Trav",                 // LABEL_LOCK
    "Man",                  // LABEL_MAN
    "Solar",                // LABEL_SOLAR
    "Rede",                 // LABEL_GRID
    "Tempo",                // LABEL_RUNTIME
    "LIGADO",               // STATUS_RUNNING
    "DESLIGADO",            // STATUS_IDLE
    "TIMEOUT",              // STATUS_TIMEOUT
    "FAULT",                // STATUS_FAULT
};
