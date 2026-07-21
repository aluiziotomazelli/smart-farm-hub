// main/src/hub_app.cpp
#include "hub_app.hpp"
#include "farm_protocol_types.hpp" // WaterLevelReport, FarmPayloadType, FarmNodeId
#include "espnow_types.hpp"        // AppMessage, CommandType

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char* TAG = "HubApp";

HubApp::HubApp(espnow::IEspNowManager& espnow, HubNvs& nvs, gpio_num_t boot_button_gpio, QueueHandle_t app_rx_queue)
    : espnow_(espnow)
    , nvs_(nvs)
    , boot_button_gpio_(boot_button_gpio)
    , app_rx_queue_(app_rx_queue)
{
}

void HubApp::run()
{
    ESP_LOGI(TAG, "Hub running. Listening for node messages...");

    espnow::AppMessage msg;
    while (true) {
        if (xQueueReceive(app_rx_queue_, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            handle_message(msg);
        }
        handle_boot_button();
    }
}

void HubApp::handle_message(const espnow::AppMessage& msg)
{
    auto node_id = static_cast<farm::NodeId>(msg.sender_id);
    auto payload_type = static_cast<farm::PayloadType>(msg.payload_type);

    nvs_.stats.messages_received++;

    switch (payload_type) {
    case farm::PayloadType::WATER_LEVEL_REPORT:
    {
        const auto* report = reinterpret_cast<const farm::WaterLevelReport*>(msg.payload);

        nvs_.stats.last_wt_level_permille = report->level_permille;
        nvs_.stats.last_wt_distance_cm = report->distance_cm;
        nvs_.stats.last_wt_battery_mv = report->battery_mv;

        ESP_LOGI(
            TAG,
            "[WATER TANK] Level: %u\u2030 | Distance: %.1f cm | Battery: %u mV (%u%%) "
            "| Float: %s | Backup: %s | RSSI: %d dBm",
            report->level_permille,
            report->distance_cm,
            report->battery_mv,
            report->battery_percent,
            report->float_switch_is_full ? "FULL" : "EMPTY",
            report->backup_mode_active ? "ON" : "OFF",
            msg.rssi);

        // Dispatch pending command if armed
        dispatch_pending_command(node_id);

        // ACK the message if required
        if (msg.requires_ack) {
            espnow_.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
        }
        break;
    }
    default:
        ESP_LOGW(TAG, "Unknown payload 0x%02X from node 0x%02X", msg.payload_type, msg.sender_id);
        break;
    }

    nvs_.commit();
}

void HubApp::handle_boot_button()
{
    if (gpio_get_level(boot_button_gpio_) != 0)
        return; // not pressed (active-low)

    vTaskDelay(pdMS_TO_TICKS(50)); // debounce
    if (gpio_get_level(boot_button_gpio_) != 0)
        return;

    // Arm OTA for the water-tank node
    if (set_pending_command(farm::NodeId::WATER_TANK, espnow::CommandType::START_OTA)) {
        ESP_LOGW(
            TAG,
            "OTA command armed for WATER_TANK. "
            "Will be dispatched on its next message.");
    }
    else {
        ESP_LOGI(TAG, "OTA already armed for WATER_TANK.");
    }

    // Wait for button release
    while (gpio_get_level(boot_button_gpio_) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void HubApp::dispatch_pending_command(farm::NodeId node_id)
{
    espnow::CommandType cmd;
    if (!has_pending_command(node_id, cmd))
        return;

    esp_err_t err = espnow_.send_command(static_cast<espnow::NodeId>(node_id), cmd, nullptr, 0, false);

    if (err == ESP_OK) {
        clear_pending_command(node_id);
        nvs_.stats.commands_sent++;
        ESP_LOGW(
            TAG, "Command 0x%02X dispatched to node 0x%02X", static_cast<uint8_t>(cmd), static_cast<uint8_t>(node_id));
    }
    else {
        ESP_LOGE(
            TAG, "Failed to dispatch command to node 0x%02X: %s", static_cast<uint8_t>(node_id), esp_err_to_name(err));
    }
}

bool HubApp::set_pending_command(farm::NodeId node_id, espnow::CommandType cmd)
{
    // Check if already set
    for (auto& entry : nvs_.stats.pending_cmds) {
        if (entry.active && entry.node_id == node_id)
            return false;
    }
    // Find empty slot
    for (auto& entry : nvs_.stats.pending_cmds) {
        if (!entry.active) {
            entry = {true, node_id, cmd};
            nvs_.commit();
            return true;
        }
    }
    ESP_LOGE(TAG, "No free slot for pending command (MAX_HUB_NODES=%d)", MAX_HUB_NODES);
    return false;
}

bool HubApp::has_pending_command(farm::NodeId node_id, espnow::CommandType& out_cmd)
{
    for (const auto& entry : nvs_.stats.pending_cmds) {
        if (entry.active && entry.node_id == node_id) {
            out_cmd = entry.command;
            return true;
        }
    }
    return false;
}

void HubApp::clear_pending_command(farm::NodeId node_id)
{
    for (auto& entry : nvs_.stats.pending_cmds) {
        if (entry.active && entry.node_id == node_id) {
            entry = {};
            nvs_.commit();
            return;
        }
    }
}
