// main/include/tank_controller.hpp
#pragma once

#include <cmath>
#include <cstdint>
#include <ctime>

#include "interfaces/i_load_domain_controller.hpp"
#include "interfaces/i_time_manager.hpp"
#include "load_control_types.hpp"
#include "sun_schedule.hpp"

/**
 * @struct TankPolicyConfig
 * @brief Tunable thresholds and timing parameters for TankController.
 */
struct TankPolicyConfig
{
    uint16_t target_fill_permille = 1000;        ///< 100.0% — Target fill level (top)
    uint16_t surplus_min_permille = 900;         // 90.0% — Above this is hysteresis band (no fill initiated)
    uint16_t opportunistic_min_permille = 800;   ///< 80.0% — 800..900 is pure solar surplus (SOLAR_ONLY)
    uint16_t normal_min_permille = 500;          ///< 50.0% — 500..800 is daylight opportunistic (SOLAR_PREF)
    uint16_t critical_min_permille = 300;        ///< 30.0% — 300..500 is normal refill (NORMAL, SOLAR_PREF); <300 is emergency
    uint8_t fill_rate_permille_per_min_x10 = 48; ///< 4.8 ‰ / min scaled by 10 (48 = 4.8 ‰/min)
    float pre_sunset_window_hours = 2.5f;        ///< Hours before sunset to accelerate fill
    uint32_t backup_fill_duration_s = 16 * 60;   ///< 16 minutes nominal fill duration in backup mode
    uint32_t manual_stop_cooldown_s = 30 * 60;   ///< 30 minutes cooldown after manual operator stop
    uint32_t min_fill_duration_s = 70;           ///< Minimum 70s watchdog to cover 60s sensor cycle
};

/**
 * @enum class TankState
 * @brief Operational state of TankController FSM.
 */
enum class TankState : uint8_t
{
    IDLE = 0,           ///< Tank is satisfied / full
    FILL_REQUESTED = 1, ///< Level is below target; requesting fill
    FILLING = 2,        ///< Pump is actively running
};

/**
 * @enum class TankFillTier
 * @brief Dynamic demand tier driving the tank's fill target and source policy.
 */
enum class TankFillTier : uint8_t
{
    NONE = 0,               ///< Level satisfied (>= 900‰), no fill needed (IDLE)
    CRITICAL_RECOVERY = 1,  ///< < 300‰ -> target is normal_min_permille (500‰) on ANY source
    NORMAL_FILL = 2,        ///< < 500‰ -> target is opportunistic_min_permille (800‰) on SOLAR_PREFERRED
    OPPORTUNISTIC = 3,      ///< < 800‰ -> target is surplus_min_permille (900‰) on SOLAR_PREFERRED
    SOLAR_SURPLUS = 4,      ///< < 900‰ -> target is target_fill_permille (1000‰) on SOLAR_ONLY
    MANUAL_REQUEST = 5,     ///< Operator manual request via button -> target is target_fill_permille (1000‰)
};

/**
 * @class TankController
 * @brief Water tank domain controller managing fill policies, duration calculations, and LoadIntent emission.
 */
class TankController : public ILoadDomainController
{
public:
    /**
     * @brief Constructs TankController with injected TimeManager and SunSchedule.
     */
    TankController(
        time_manager::ITimeManager& time_mgr,
        const SunSchedule& sun_schedule,
        const TankPolicyConfig& config = {});

    ~TankController() override = default;

    // ─── Input Telemetry & Events ─────────────────────────────────────

    /**
     * @brief Processes incoming WaterTankReport data.
     * @param permille Current water level in permille (0 - 1000).
     * @param float_switch_full True if high-level emergency float switch is tripped.
     * @param backup_mode True if ultrasonic sensor is failed and operating only on float switch.
     * @param timestamp Epoch timestamp of the report.
     */
    void on_tank_report(uint16_t permille, bool float_switch_full, bool backup_mode, time_t timestamp);

    /**
     * @brief User manual request to fill the tank to target level.
     */
    void on_manual_fill_request();

    /**
     * @brief Notifies TankController about actual pump actuator operational state.
     */
    void on_pump_status_update(farm::LoadState state, farm::PowerSource source, uint32_t runtime_s);

    // ─── ILoadDomainController Interface ──────────────────────────────

    /** @copydoc ILoadDomainController::get_current_intent */
    LoadIntent get_current_intent() const override;

    /** @copydoc ILoadDomainController::get_load_index */
    LoadIndex get_load_index() const override { return LoadIndex::PUMP; }

    // ─── State Query Getters ──────────────────────────────────────────

    TankState get_state() const { return state_; }

    TankFillTier get_current_tier() const { return current_tier_; }

    uint16_t get_current_level_permille() const { return current_permille_; }

    bool is_float_switch_full() const { return float_switch_full_; }

    bool is_backup_mode() const { return backup_mode_; }

    bool is_manual_stop_in_cooldown() const;

    /**
     * @brief Calculates remaining fill duration in seconds (0 if full or not filling).
     */
    uint32_t calculate_estimated_duration_s() const;

    /**
     * @brief Returns the dynamic target permille for the currently active tier.
     */
    uint16_t get_target_permille_for_tier(TankFillTier tier) const;

private:
    void evaluate_policy();

    time_manager::ITimeManager& time_mgr_;
    SunSchedule sun_schedule_;
    TankPolicyConfig config_;

    TankState state_ = TankState::IDLE;
    TankFillTier current_tier_ = TankFillTier::NONE;
    uint16_t current_permille_ = 1000;
    bool float_switch_full_ = false;
    bool backup_mode_ = false;
    bool manual_fill_requested_ = false;
    bool pump_running_ = false;
    time_t last_report_time_ = 0;
    time_t manual_stop_cooldown_until_ = 0;
};
