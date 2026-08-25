// main/include/load_control_engine.hpp
#pragma once

#include <cstdint>
#include "etl/vector.h"

#include "interfaces/i_energy_monitor.hpp"
#include "load_control_types.hpp"
#include "load_types.hpp"

/**
 * @enum class WindowFillState
 * @brief FSM state for solar window optimization (Scenario C - pump & compressor).
 */
enum class WindowFillState : uint8_t {
    IDLE = 0,               ///< No fill requested
    WAITING_FOR_WINDOW = 1, ///< Pump waiting for compressor off-cycle window
    FILLING_WINDOW = 2,     ///< Pump running on solar in captured window
    FILLING_DIRECT = 3,     ///< Pump running directly on solar (ample headroom)
    FILLING_GRID = 4,       ///< Pump running on grid
};

class LoadControlEngine
{
public:
    explicit LoadControlEngine(
        const PriorityConfig& priority_cfg = {});

    ~LoadControlEngine() = default;

    // ─── Registration & Profile Setup ────────────────────────────────

    void register_load(LoadIndex index, const LoadProfile& profile);
    void update_priority_config(const PriorityConfig& priority_cfg);

    // ─── Input Handlers (Events & Telemetry) ──────────────────────────

    /**
     * @brief Ingests new solar power generation telemetry.
     */
    void on_solar_update(const SolarPowerUpdate& solar);

    /**
     * @brief Ingests updated intent from a load domain controller (e.g. TankController, Fridge).
     */
    void on_load_intent(const LoadIntent& intent);

    /**
     * @brief Ingests telemetry report from an actuator node.
     */
    void on_load_status(const LoadStatusUpdate& status);

    /**
     * @brief Ingests physical grid/solar presence changes (from EnergyMonitor).
     */
    void on_energy_availability(bool solar_available, bool grid_available);

    /**
     * @brief Periodic housekeeping tick (call every second or 100ms).
     * @param now_ms Current system timestamp in milliseconds.
     */
    void on_periodic_tick(int64_t now_ms);

    // ─── Evaluation & Decisions ───────────────────────────────────────

    /**
     * @brief Evaluates energy balance and returns decisions for loads that require state change.
     * @return List of decisions to be dispatched to actuator nodes (Zero heap allocation).
     */
    etl::vector<LoadControlDecision, static_cast<size_t>(LoadIndex::MAX)> evaluate_arbitration();

    // ─── State Getters ────────────────────────────────────────────────

    const EngineLoadEntry& get_load(LoadIndex index) const;
    uint16_t get_solar_power_w() const { return solar_power_w_; }
    uint16_t get_allocated_solar_w() const;
    int32_t get_solar_headroom_w() const;
    bool is_solar_available() const { return solar_available_; }
    bool is_grid_available() const { return grid_available_; }
    EpisodicWindowState get_load_window_state(LoadIndex index) const { return loads_[static_cast<size_t>(index)].window_state; }

private:
    void evaluate_episodic_windows(int64_t now_ms);
    uint16_t get_effective_load_watts(const EngineLoadEntry& entry) const;

    PriorityConfig priority_cfg_;

    EngineLoadEntry loads_[static_cast<size_t>(LoadIndex::MAX)];
    uint16_t solar_power_w_ = 0;
    bool solar_available_ = true;
    bool grid_available_ = true;
    int64_t current_time_ms_ = 0;
};
