// main/src/handlers/load_control_handler.cpp
#include <cstdint>
#include <cstring>

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "handlers/load_control_handler.hpp"

static const char* TAG = "LoadControlHandler";

namespace hub {

LoadControlHandler::LoadControlHandler(
    INodeRegistry& node_registry,
    ILoadControlTask& load_control_task,
    ICommandManager& command_mgr,
    idf_hals::ITimerHAL& timer)
    : node_registry_(node_registry)
    , load_control_task_(load_control_task)
    , command_mgr_(command_mgr)
    , timer_(timer)
{
}

espnow::AckStatus LoadControlHandler::handle_payload(const espnow::AppMessage& msg)
{
    if (msg.payload_len < sizeof(farm::LoadControlStatus)) {
        ESP_LOGE(TAG, "Invalid payload length %zu received from node 0x%02X", msg.payload_len, msg.sender_id);
        return espnow::AckStatus::ERROR_INVALID_DATA;
    }

    farm::LoadControlStatus report{};
    memcpy(&report, msg.payload, sizeof(farm::LoadControlStatus));

    auto sender_node = static_cast<farm::NodeId>(msg.sender_id);
    const int64_t now_ms = static_cast<int64_t>(timer_.get_time_us() / 1000);

    // 1. Update NodeRegistry power profile
    node_registry_.set_power_profile(sender_node, report.power_profile);

    // 2. Map node & circuit to logical LoadIndex
    LoadIndex load_idx = LoadIndex::PUMP;
    if (sender_node == farm::NodeId::PUMP_CONTROL && report.circuit_id == 0) {
        load_idx = LoadIndex::PUMP;
    } else {
        // Fallback for future circuits / secondary loads
        load_idx = static_cast<LoadIndex>(report.circuit_id % static_cast<uint8_t>(LoadIndex::MAX));
    }

    // 3. Populate LoadStatusUpdate
    LoadStatusUpdate status_update{
        .load_index = load_idx,
        .node_id = sender_node,
        .circuit_id = report.circuit_id,
        .control_mode = report.control_mode,
        .active_source = report.active_power_source,
        .load_state = report.load_state,
        .power_w = report.power_w,
        .runtime_s = report.runtime_s,
        .timestamp_ms = now_ms,
    };

    // 4. Forward status report to LoadControlTask
    load_control_task_.post_load_status(status_update);

    ESP_LOGI(
        TAG,
        "[LOAD CONTROL] Node: 0x%02X | Circuit: %u | State: %u | Mode: %u | Source: %u | Power: %u W | Runtime: %lu s | Uptime: %lu s | RSSI: %d dBm",
        msg.sender_id,
        report.circuit_id,
        static_cast<uint8_t>(report.load_state),
        static_cast<uint8_t>(report.control_mode),
        static_cast<uint8_t>(report.active_power_source),
        report.power_w,
        static_cast<unsigned long>(report.runtime_s),
        static_cast<unsigned long>(report.uptime_s),
        msg.rssi);

    return espnow::AckStatus::OK;
}

void LoadControlHandler::post_handle_payload(const espnow::AppMessage& msg)
{
    if (msg.payload_len < sizeof(farm::LoadControlStatus)) {
        return;
    }

    const auto* report = reinterpret_cast<const farm::LoadControlStatus*>(msg.payload);
    auto node_id = static_cast<farm::NodeId>(msg.sender_id);

    command_mgr_.process_node_wake(node_id, report->unix_time);
}

} // namespace hub
