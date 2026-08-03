// main/include/hub_tasks.hpp
#pragma once

#include <cstdint>

namespace hub {
namespace tasks {

/**
 * @brief FreeRTOS Task Priority Hierarchy for Smart Farm Hub.
 * (Higher integer = Higher priority in FreeRTOS)
 *
 * 5: ESP-NOW Driver RX & TX Tasks (configured in EspNowConfig)
 * 4: Real-time Telemetry Dispatcher & UI Inputs (Rotary/Buttons)
 * 3: Solar Load Arbitrator (LoadDecisionEngine - Real-time power balancing)
 * 2: Display Rendering Task (DisplayManager OLED 128x64 updates)
 * 1: Background Application Loop (HubApp: WiFi, SNTP, NVS, OTA orchestrator)
 */

// ESP-NOW Manager Tasks
constexpr uint32_t ESPNOW_RX_TASK_PRIORITY = 5;
constexpr uint32_t ESPNOW_RX_TASK_STACK_SIZE = 4096;

constexpr uint32_t ESPNOW_TX_TASK_PRIORITY = 5;
constexpr uint32_t ESPNOW_TX_TASK_STACK_SIZE = 3584;

// Message Dispatcher Task
constexpr uint32_t DISPATCHER_PRIORITY = 4;
constexpr uint32_t DISPATCHER_STACK_SIZE = 3584; // 3.5 KB

// UI Input Manager Task (Rotary Encoder & Buttons)
constexpr uint32_t UI_INPUT_PRIORITY = 4;
constexpr uint32_t UI_INPUT_STACK_SIZE = 3072; // 3.0 KB

// Solar Load Arbitrator Task (LoadDecisionEngine)
constexpr uint32_t LOAD_DECISION_ENGINE_PRIORITY = 3;
constexpr uint32_t LOAD_DECISION_ENGINE_STACK_SIZE = 3072; // 3.0 KB

// Display Manager Task
constexpr uint32_t DISPLAY_PRIORITY = 2;
constexpr uint32_t DISPLAY_STACK_SIZE = 4096; // 4.0 KB

} // namespace tasks
} // namespace hub
