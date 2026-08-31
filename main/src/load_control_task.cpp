// main/src/load_control_task.cpp
#include "load_control_task.hpp"

#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "LoadControlTask";

namespace hub {

LoadControlTask::LoadControlTask(
    idf_hals::IHalFreertos& rtos,
    time_manager::ITimeManager& time_mgr,
    UiSnapshot& ui_snapshot,
    ILoadActuatorDispatcher& actuator_dispatcher,
    IEnergyMonitor& energy_monitor,
    const PriorityConfig& priority_config,
    const LoadControlTaskConfig& config)
    : rtos_(rtos)
    , time_mgr_(time_mgr)
    , ui_snapshot_(ui_snapshot)
    , actuator_dispatcher_(actuator_dispatcher)
    , energy_monitor_(energy_monitor)
    , config_(config)
    , engine_(priority_config)
{
    status_queues_.fill(nullptr);
}

LoadControlTask::~LoadControlTask()
{
    stop();
}

esp_err_t LoadControlTask::init()
{
    // 1. Create FIFO command queue for intents
    command_queue_ = rtos_.queue_create(config_.command_queue_length, sizeof(LoadIntent));
    if (command_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create command_queue");
        return ESP_FAIL;
    }

    // 2. Create Overwrite queue for solar telemetry (length = 1)
    solar_queue_ = rtos_.queue_create(1, sizeof(SolarPowerUpdate));
    if (solar_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create solar_queue");
        return ESP_FAIL;
    }

    // 3. Create Overwrite queues for per-load status reports (length = 1 each)
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        status_queues_[i] = rtos_.queue_create(1, sizeof(LoadStatusUpdate));
        if (status_queues_[i] == nullptr) {
            ESP_LOGE(TAG, "Failed to create status_queue for load %zu", i);
            return ESP_FAIL;
        }
    }

    // 4. Create Binary Semaphore for energy grid/solar availability changes
    energy_semaphore_ = rtos_.semaphore_create_binary();
    if (energy_semaphore_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create energy_semaphore");
        return ESP_FAIL;
    }

    // 5. Create QueueSet (solar + command_queue + N status queues + 1 energy semaphore)
    // Combined capacity: 1 (solar) + 8 (status) + 1 (semaphore) = 10
    const UBaseType_t queue_set_length = 1 + static_cast<UBaseType_t>(LoadIndex::MAX) + 1;
    queue_set_ = rtos_.queue_create_set(queue_set_length);
    if (queue_set_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create queue_set");
        return ESP_FAIL;
    }

    rtos_.queue_add_to_set(solar_queue_, queue_set_);
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        rtos_.queue_add_to_set(status_queues_[i], queue_set_);
    }
    rtos_.queue_add_to_set(energy_semaphore_, queue_set_);

    ESP_LOGI(TAG, "LoadControlTask initialized successfully with QueueSet (length=%u)", static_cast<unsigned>(queue_set_length));
    return ESP_OK;
}

esp_err_t LoadControlTask::start()
{
    if (running_) {
        return ESP_OK;
    }

    running_ = true;
    BaseType_t ret = rtos_.task_create(
        task_entry,
        "load_ctrl_task",
        config_.stack_size,
        this,
        config_.priority,
        &task_handle_);

    if (ret != pdPASS) {
        running_ = false;
        ESP_LOGE(TAG, "Failed to create FreeRTOS task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LoadControlTask started");
    return ESP_OK;
}

void LoadControlTask::stop()
{
    running_ = false;

    if (task_handle_ != nullptr) {
        rtos_.task_delete(task_handle_);
        task_handle_ = nullptr;
    }

    if (command_queue_ != nullptr) {
        rtos_.queue_delete(command_queue_);
        command_queue_ = nullptr;
    }
    if (solar_queue_ != nullptr) {
        rtos_.queue_delete(solar_queue_);
        solar_queue_ = nullptr;
    }
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        if (status_queues_[i] != nullptr) {
            rtos_.queue_delete(status_queues_[i]);
            status_queues_[i] = nullptr;
        }
    }
    if (energy_semaphore_ != nullptr) {
        rtos_.semaphore_delete(energy_semaphore_);
        energy_semaphore_ = nullptr;
    }
}

// ─── Event Posting (Called by Handlers / Controllers from other contexts) ─

esp_err_t LoadControlTask::post_solar_update(const SolarPowerUpdate& update)
{
    if (solar_queue_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t ret = rtos_.queue_overwrite(solar_queue_, &update);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t LoadControlTask::post_load_intent(const LoadIntent& intent)
{
    if (command_queue_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t ret = rtos_.queue_send(command_queue_, &intent, 0);
    if (ret == pdPASS) {
        // Wake up LCT if it's currently waiting in queue_select_from_set
        if (energy_semaphore_ != nullptr) {
            rtos_.semaphore_give(energy_semaphore_);
        }
        return ESP_OK;
    }
    ESP_LOGW(TAG, "command_queue full for load %u", static_cast<unsigned>(intent.load_index));
    return ESP_ERR_NO_MEM;
}

esp_err_t LoadControlTask::post_load_status(const LoadStatusUpdate& status)
{
    size_t idx = static_cast<size_t>(status.load_index);
    if (idx >= static_cast<size_t>(LoadIndex::MAX) || status_queues_[idx] == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    BaseType_t ret = rtos_.queue_overwrite(status_queues_[idx], &status);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t LoadControlTask::notify_energy_availability(bool solar_available, bool grid_available)
{
    last_solar_available_ = solar_available;
    last_grid_available_ = grid_available;
    if (energy_semaphore_ != nullptr) {
        rtos_.semaphore_give(energy_semaphore_);
    }
    return ESP_OK;
}

// ─── Background Task Loop ────────────────────────────────────────────────

void LoadControlTask::task_entry(void* param)
{
    auto* self = static_cast<LoadControlTask*>(param);
    self->run_loop();
}

void LoadControlTask::drain_command_queue()
{
    LoadIntent intent;
    while (command_queue_ != nullptr && rtos_.queue_receive(command_queue_, &intent, 0) == pdTRUE) {
        engine_.on_load_intent(intent);
        snapshot_dirty_ = true;
    }
}

void LoadControlTask::process_active_queue_member(QueueSetMemberHandle_t member, int64_t now_ms)
{
    if (member == solar_queue_) {
        SolarPowerUpdate solar_update;
        if (rtos_.queue_receive(solar_queue_, &solar_update, 0) == pdTRUE) {
            engine_.on_solar_update(solar_update);
            snapshot_dirty_ = true;
        }
    }
    else if (member == energy_semaphore_) {
        if (rtos_.semaphore_take(energy_semaphore_, 0) == pdTRUE) {
            bool solar = energy_monitor_.is_solar_available();
            bool grid = energy_monitor_.is_grid_available();
            engine_.on_energy_availability(solar, grid);
            snapshot_dirty_ = true;
        }
    }
    else {
        // Check if member is one of the per-load status queues
        for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
            if (member == status_queues_[i]) {
                LoadStatusUpdate status;
                if (rtos_.queue_receive(status_queues_[i], &status, 0) == pdTRUE) {
                    engine_.on_load_status(status);
                    snapshot_dirty_ = true;
                }
                break;
            }
        }
    }
}

void LoadControlTask::evaluate_and_dispatch(int64_t now_ms)
{
    constexpr int64_t COMMAND_RETRY_INTERVAL_MS = 3000;

    auto decisions = engine_.evaluate_arbitration();
    for (const auto& dec : decisions) {
        if (dec.action_required) {
            size_t idx = static_cast<size_t>(dec.load_index);
            if (idx < last_dispatch_ts_.size()) {
                if (now_ms - last_dispatch_ts_[idx] < COMMAND_RETRY_INTERVAL_MS) {
                    continue;
                }
                last_dispatch_ts_[idx] = now_ms;
            }

            ESP_LOGI(
                TAG,
                "Dispatching decision: Load %u -> State: %s | Source: %s | Watchdog: %lus",
                static_cast<unsigned>(dec.load_index),
                dec.should_be_on ? "ON" : "OFF",
                (dec.target_source == farm::PowerSource::SOLAR) ? "SOLAR" : "GRID",
                static_cast<unsigned long>(dec.watchdog_s));

            actuator_dispatcher_.dispatch_decision(dec);
            snapshot_dirty_ = true;
        }
    }
}

void LoadControlTask::refresh_ui_snapshot()
{
    std::array<LoadUiSnapshot, static_cast<size_t>(LoadIndex::MAX)> loads{};
    std::array<EpisodicWindowState, static_cast<size_t>(LoadIndex::MAX)> window_states{};

    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        const auto& entry = engine_.get_load(static_cast<LoadIndex>(i));
        loads[i].node_id = entry.last_status.node_id;
        loads[i].circuit_id = entry.last_status.circuit_id;
        loads[i].control_mode = entry.last_status.control_mode;
        loads[i].selected_source = entry.last_status.selected_source;
        if (entry.assigned_source != farm::PowerSource::UNKNOWN) {
            loads[i].active_source = entry.assigned_source;
        } else {
            loads[i].active_source = entry.last_status.active_source;
        }
        if (entry.assigned_on || entry.last_status.load_state == farm::LoadState::RUNNING) {
            loads[i].load_state = farm::LoadState::RUNNING;
        } else {
            loads[i].load_state = entry.last_status.load_state;
        }
        loads[i].power_w = entry.last_status.power_w;
        loads[i].runtime_s = entry.last_status.runtime_s;
        loads[i].last_update_ts = entry.last_status.timestamp_ms;
        loads[i].hub_authorized = entry.assigned_on;

        window_states[i] = entry.window_state;
    }

    ui_snapshot_.update_energy_and_loads(
        engine_.get_solar_power_w(),
        engine_.get_allocated_solar_w(),
        engine_.get_solar_headroom_w(),
        engine_.is_solar_available(),
        engine_.is_grid_available(),
        loads,
        window_states);
}

void LoadControlTask::run_loop()
{
    ESP_LOGI(TAG, "LoadControlTask execution loop started");

    while (running_) {
        // 1. Wait on QueueSet (reactive on telemetry/events, or periodic timeout)
        TickType_t wait_ticks = pdMS_TO_TICKS(config_.loop_timeout_ms);
        QueueSetMemberHandle_t active_member = rtos_.queue_select_from_set(queue_set_, wait_ticks);

        int64_t now_ms = static_cast<int64_t>(time_mgr_.get_timestamp_ms());

        // 2. Drain any incoming LoadIntents (FIFO queue)
        drain_command_queue();

        // 3. Process active QueueSet member if any
        if (active_member != nullptr) {
            process_active_queue_member(active_member, now_ms);
        } else {
            // Idle timeout -> Periodic tick for window FSM and off-grid checks
            engine_.on_periodic_tick(now_ms);
            snapshot_dirty_ = true;
        }

        // 4. Run energy arbitration and dispatch decisions
        evaluate_and_dispatch(now_ms);

        // 5. Refresh UI Snapshot when dirty
        if (snapshot_dirty_) {
            refresh_ui_snapshot();
            snapshot_dirty_ = false;
        }
    }

    ESP_LOGI(TAG, "LoadControlTask execution loop terminated");
}

} // namespace hub
