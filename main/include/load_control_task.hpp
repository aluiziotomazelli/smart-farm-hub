// main/include/load_control_task.hpp
#pragma once

#include <array>
#include <cstdint>

#include "interfaces/i_energy_monitor.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_load_actuator_dispatcher.hpp"
#include "interfaces/i_load_control_task.hpp"
#include "interfaces/i_time_manager.hpp"
#include "load_control_engine.hpp"
#include "load_profiles.hpp"
#include "ui_snapshot.hpp"

namespace hub {

/**
 * @struct LoadControlTaskConfig
 * @brief Configuration parameters for LoadControlTask execution.
 */
struct LoadControlTaskConfig
{
    uint32_t stack_size = 4096;
    UBaseType_t priority = 5;
    uint32_t loop_timeout_ms = 100; ///< QueueSet block timeout (controls periodic tick rate)
    size_t command_queue_length = 16;
};

/**
 * @class LoadControlTask
 * @brief Asynchronous FreeRTOS task running the LoadControlEngine and managing energy arbitration.
 */
class LoadControlTask : public ILoadControlTask
{
public:
    LoadControlTask(
        idf_hals::IHalFreertos& rtos,
        time_manager::ITimeManager& time_mgr,
        UiSnapshot& ui_snapshot,
        ILoadActuatorDispatcher& actuator_dispatcher,
        IEnergyMonitor& energy_monitor,
        const PriorityConfig& priority_config = farm_loads::get_default_priority_config(),
        const LoadControlTaskConfig& config = {});

    ~LoadControlTask() override;

    /**
     * @brief Initializes FreeRTOS Queues, Semaphores, and QueueSet.
     * @return ESP_OK on success, ESP_FAIL on resource allocation error.
     */
    esp_err_t init();

    /**
     * @brief Starts the background FreeRTOS execution task.
     * @return ESP_OK on success, ESP_FAIL on task creation error.
     */
    esp_err_t start();

    /**
     * @brief Stops and deletes the background task and associated resources.
     */
    void stop();

    // ─── ILoadControlTask Implementation ──────────────────────────────

    /** @copydoc ILoadControlTask::post_solar_update */
    esp_err_t post_solar_update(const SolarPowerUpdate& update) override;

    /** @copydoc ILoadControlTask::post_load_intent */
    esp_err_t post_load_intent(const LoadIntent& intent) override;

    /** @copydoc ILoadControlTask::post_load_status */
    esp_err_t post_load_status(const LoadStatusUpdate& status) override;

    /** @copydoc ILoadControlTask::notify_energy_availability */
    esp_err_t notify_energy_availability(bool solar_available, bool grid_available) override;

    // ─── Accessors for Diagnostics, Integration and Testing ───────────
    SemaphoreHandle_t get_energy_semaphore() const { return energy_semaphore_; }
    TaskHandle_t get_task_handle() const { return task_handle_; }
    const LoadControlEngine& get_engine() const { return engine_; }

private:
    static void task_entry(void* param);
    void run_loop();

    void drain_command_queue();
    void process_active_queue_member(QueueSetMemberHandle_t member, int64_t now_ms);
    void evaluate_and_dispatch(int64_t now_ms);
    void refresh_ui_snapshot();

    idf_hals::IHalFreertos& rtos_;
    time_manager::ITimeManager& time_mgr_;
    UiSnapshot& ui_snapshot_;
    ILoadActuatorDispatcher& actuator_dispatcher_;
    IEnergyMonitor& energy_monitor_;
    LoadControlTaskConfig config_;

    LoadControlEngine engine_;

    TaskHandle_t task_handle_{nullptr};
    volatile bool running_{false};

    // FreeRTOS Synchronization & IPC Primitives
    QueueHandle_t command_queue_{nullptr}; ///< FIFO for LoadIntents
    QueueHandle_t solar_queue_{nullptr};   ///< Length 1, overwriting solar telemetry
    std::array<QueueHandle_t, static_cast<size_t>(LoadIndex::MAX)> status_queues_{}; ///< Length 1 per load status
    SemaphoreHandle_t energy_semaphore_{nullptr}; ///< Binary semaphore for grid/solar change
    QueueSetHandle_t queue_set_{nullptr};         ///< QueueSet aggregating all event sources

    // Energy availability shadow flags
    volatile bool last_solar_available_{true};
    volatile bool last_grid_available_{true};

    bool snapshot_dirty_{false};
    std::array<int64_t, static_cast<size_t>(LoadIndex::MAX)> last_dispatch_ts_{};
};

} // namespace hub
