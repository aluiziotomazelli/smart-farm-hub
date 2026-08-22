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
    SystemState& state,
    SemaphoreHandle_t state_mutex,
    CommandManager& command_mgr,
    idf_hals::ITimerHAL& timer,
    idf_hals::IHalFreertos& rtos,
    EventGroupHandle_t load_events)
    : state_(state)
    , state_mutex_(state_mutex)
    , command_mgr_(command_mgr)
    , timer_(timer)
    , rtos_(rtos)
    , load_events_(load_events)
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

    if (rtos_.semaphore_take(state_mutex_, portMAX_DELAY) == pdTRUE) {
        auto* load = state_.find_or_allocate_load(sender_node, report.circuit_id);
        if (load != nullptr) {
            load->control_mode = report.control_mode;
            load->active_source = report.active_power_source;
            load->load_state = report.load_state;
            load->power_w = report.power_w;
            load->runtime_s = report.runtime_s;
            load->uptime_s = report.uptime_s;
            load->unix_time = report.unix_time;
            load->last_update_ts = timer_.get_time_us() / 1000;
        }

        state_.set_node_power_profile(sender_node, report.power_profile);

        rtos_.semaphore_give(state_mutex_);
    }

    command_mgr_.get_stats().set_node_power_profile(sender_node, report.power_profile);

    ESP_LOGI(
        TAG,
        "[LOAD CONTROL] Node: 0x%02X | Circuit: %u | State: %u | Mode: %u | Source: %u | Power: %u W | Runtime: %lu s | Uptime: %lu s | RSSI: %d dBm | Time: %llu ms",
        msg.sender_id,
        report.circuit_id,
        static_cast<uint8_t>(report.load_state),
        static_cast<uint8_t>(report.control_mode),
        static_cast<uint8_t>(report.active_power_source),
        report.power_w,
        static_cast<unsigned long>(report.runtime_s),
        static_cast<unsigned long>(report.uptime_s),
        msg.rssi,
        static_cast<unsigned long long>(report.unix_time));

    if (load_events_ != nullptr) {
        rtos_.event_group_set_bits(load_events_, 1 << 0);
    }

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
