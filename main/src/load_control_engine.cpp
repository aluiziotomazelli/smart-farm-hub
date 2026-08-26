// main/src/load_control_engine.cpp
#include <algorithm>
#include <cmath>

#include "load_control_engine.hpp"

LoadControlEngine::LoadControlEngine(
    const PriorityConfig& priority_cfg)
    : priority_cfg_(priority_cfg)
{
    // Initialize default load entries
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        loads_[i].load_index = static_cast<LoadIndex>(i);
        loads_[i].profile = priority_cfg_.profiles[i];
    }
}

void LoadControlEngine::register_load(LoadIndex index, const LoadProfile& profile)
{
    size_t idx = static_cast<size_t>(index);
    if (idx < static_cast<size_t>(LoadIndex::MAX)) {
        loads_[idx].profile = profile;
        priority_cfg_.profiles[idx] = profile;
    }
}

void LoadControlEngine::update_priority_config(const PriorityConfig& priority_cfg)
{
    priority_cfg_ = priority_cfg;
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        loads_[i].profile = priority_cfg_.profiles[i];
    }
}

void LoadControlEngine::on_solar_update(const SolarPowerUpdate& solar)
{
    solar_power_w_ = solar.power_w;
    current_time_ms_ = solar.timestamp_ms;
}

void LoadControlEngine::on_load_intent(const LoadIntent& intent)
{
    size_t idx = static_cast<size_t>(intent.load_index);
    if (idx < static_cast<size_t>(LoadIndex::MAX)) {
        loads_[idx].intent = intent;
    }
}

void LoadControlEngine::on_load_status(const LoadStatusUpdate& status)
{
    size_t idx = static_cast<size_t>(status.load_index);
    if (idx < static_cast<size_t>(LoadIndex::MAX)) {
        loads_[idx].last_status = status;
    }
    current_time_ms_ = status.timestamp_ms;
}

void LoadControlEngine::on_energy_availability(bool solar_available, bool grid_available)
{
    solar_available_ = solar_available;
    grid_available_ = grid_available;
}

void LoadControlEngine::on_periodic_tick(int64_t now_ms)
{
    current_time_ms_ = now_ms;
    evaluate_episodic_windows(now_ms);
}

const EngineLoadEntry& LoadControlEngine::get_load(LoadIndex index) const
{
    return loads_[static_cast<size_t>(index)];
}

uint16_t LoadControlEngine::get_effective_load_watts(const EngineLoadEntry& entry) const
{
    // If actuator is already actively running and reporting power, use live telemetry.
    if (entry.last_status.load_state == farm::LoadState::RUNNING && entry.last_status.power_w > 0) {
        return entry.last_status.power_w;
    }
    // Otherwise fallback to expected running watts for pre-planning
    return entry.profile.expected_watts_running;
}

uint16_t LoadControlEngine::get_allocated_solar_w() const
{
    uint16_t sum = 0;
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        if (loads_[i].assigned_on && loads_[i].assigned_source == farm::PowerSource::SOLAR) {
            sum += get_effective_load_watts(loads_[i]);
        }
    }
    return sum;
}

int32_t LoadControlEngine::get_solar_headroom_w() const
{
    if (!solar_available_) {
        return 0;
    }
    return static_cast<int32_t>(solar_power_w_) - static_cast<int32_t>(get_allocated_solar_w());
}

void LoadControlEngine::evaluate_episodic_windows(int64_t now_ms)
{
    // Calculate minimum max_shed_duration_s among all active continuous shedding loads
    uint32_t min_safe_hold_duration_s = 30 * 60; // 30 min default fallback
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        if (loads_[i].profile.is_continuous && loads_[i].profile.can_shed) {
            if (loads_[i].profile.max_shed_duration_s > 0 && loads_[i].profile.max_shed_duration_s < min_safe_hold_duration_s) {
                min_safe_hold_duration_s = loads_[i].profile.max_shed_duration_s;
            }
        }
    }

    // Evaluate solar window FSM for every episodic (non-continuous) load
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        auto& load = loads_[i];
        if (load.profile.is_continuous) {
            continue;
        }

        bool load_wants_on = (load.intent.desired_state == LoadDesiredState::ON);
        if (!load_wants_on) {
            load.window_state = EpisodicWindowState::IDLE;
            continue;
        }

        uint16_t load_watts = get_effective_load_watts(load);

        // Calculate available live headroom against current active continuous load consumption
        uint16_t continuous_load_w = 0;
        for (size_t c = 0; c < static_cast<size_t>(LoadIndex::MAX); ++c) {
            if (loads_[c].profile.is_continuous &&
                loads_[c].intent.desired_state == LoadDesiredState::ON) {
                continuous_load_w += get_effective_load_watts(loads_[c]);
            }
        }
        int32_t live_headroom_w = static_cast<int32_t>(solar_power_w_) - static_cast<int32_t>(continuous_load_w);

        switch (load.window_state) {
        case EpisodicWindowState::IDLE: {
            if (live_headroom_w >= load_watts) {
                load.window_state = EpisodicWindowState::RUNNING_DIRECT;
            } else if (load.intent.urgency == LoadUrgency::OPPORTUNISTIC || load.intent.urgency == LoadUrgency::NORMAL) {
                load.window_state = EpisodicWindowState::WAITING_FOR_WINDOW;
                load.window_wait_start_ts = now_ms;
            } else {
                load.window_state = EpisodicWindowState::RUNNING_GRID;
            }
            break;
        }

        case EpisodicWindowState::WAITING_FOR_WINDOW: {
            uint32_t max_wait_s = load.profile.max_wait_window_s > 0 ? load.profile.max_wait_window_s : 600;
            if (live_headroom_w >= load_watts) {
                // Thermal off-cycle detected on continuous loads! Window captured
                load.window_state = EpisodicWindowState::RUNNING_WINDOW;
                load.window_run_start_ts = now_ms;
            } else if (now_ms - load.window_wait_start_ts >= (static_cast<int64_t>(max_wait_s) * 1000)) {
                // Window wait timed out -> fallback by urgency
                if (load.intent.urgency == LoadUrgency::OPPORTUNISTIC) {
                    load.window_state = EpisodicWindowState::IDLE; // Defer
                } else {
                    load.window_state = EpisodicWindowState::RUNNING_GRID;
                }
            }
            break;
        }

        case EpisodicWindowState::RUNNING_WINDOW: {
            // If max safe hold duration of any continuous load is exceeded, migrate to grid
            if (now_ms - load.window_run_start_ts >= (static_cast<int64_t>(min_safe_hold_duration_s) * 1000)) {
                load.window_state = EpisodicWindowState::RUNNING_GRID;
            }
            break;
        }

        default:
            break;
        }
    }
}

etl::vector<LoadControlDecision, static_cast<size_t>(LoadIndex::MAX)> LoadControlEngine::evaluate_arbitration()
{
    evaluate_episodic_windows(current_time_ms_);

    etl::vector<LoadControlDecision, static_cast<size_t>(LoadIndex::MAX)> decisions;

    // Reset planned assignments
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        loads_[i].assigned_on = false;
        loads_[i].assigned_source = farm::PowerSource::UNKNOWN;
    }

    // 1. Gather all loads desiring ON state
    struct Candidate {
        size_t index;
        LoadIndex load_index;
        LoadUrgency urgency;
        bool is_continuous;
        uint32_t max_hold_s;
        uint16_t watts;
        uint8_t priority_rank;
        SourcePreference source_pref;
        uint32_t watchdog_s;
    };

    etl::vector<Candidate, static_cast<size_t>(LoadIndex::MAX)> active_candidates;
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        if (loads_[i].intent.desired_state == LoadDesiredState::ON) {
            active_candidates.push_back({
                i,
                loads_[i].load_index,
                loads_[i].intent.urgency,
                loads_[i].profile.is_continuous,
                loads_[i].intent.max_hold_duration_s,
                get_effective_load_watts(loads_[i]),
                loads_[i].profile.priority_rank,
                loads_[i].intent.source_preference,
                loads_[i].intent.estimated_on_duration_s
            });
        }
    }

    // 2. Sort candidates for Solar Allocation:
    // Higher urgency first (CRITICAL > NORMAL > OPPORTUNISTIC > SHEDDABLE)
    // For equal urgency, prioritize heavier continuous loads over lighter ones to maximize solar utilisation (Knapsack)
    // Lower priority_rank as final tiebreaker
    std::sort(active_candidates.begin(), active_candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.urgency != b.urgency) {
            return static_cast<uint8_t>(a.urgency) > static_cast<uint8_t>(b.urgency);
        }
        if (a.watts != b.watts) {
            return a.watts > b.watts; // Knapsack greedy: larger watts first to maximize solar absorption
        }
        return a.priority_rank < b.priority_rank;
    });

    // 3. Solar Packing & Knapsack Greedy Allocation
    int32_t remaining_solar_w = solar_available_ ? static_cast<int32_t>(solar_power_w_) : 0;

    for (auto& cand : active_candidates) {
        bool can_use_solar = solar_available_ && (remaining_solar_w >= cand.watts);

        // For opportunistic episodic loads waiting for thermal off-cycle window
        if (!cand.is_continuous && cand.urgency == LoadUrgency::OPPORTUNISTIC) {
            auto win_state = loads_[cand.index].window_state;
            if (win_state == EpisodicWindowState::WAITING_FOR_WINDOW) {
                continue; // Hold in waiting state
            }
        }

        if (can_use_solar) {
            loads_[cand.index].assigned_on = true;
            loads_[cand.index].assigned_source = farm::PowerSource::SOLAR;
            loads_[cand.index].current_watchdog_s = cand.watchdog_s;
            remaining_solar_w -= cand.watts;
        } else if (grid_available_ && cand.source_pref != SourcePreference::SOLAR_ONLY) {
            // Assign to Grid if allowed
            loads_[cand.index].assigned_on = true;
            loads_[cand.index].assigned_source = farm::PowerSource::GRID;
            loads_[cand.index].current_watchdog_s = cand.watchdog_s;
        } else {
            // Cannot be satisfied
            loads_[cand.index].assigned_on = false;
            loads_[cand.index].assigned_source = farm::PowerSource::UNKNOWN;
        }
    }

    // 4. Fine Packing / Small Load Opportunistic Recovery (Tiebreaker 2: Wattage vs Deficit)
    // If there is residual solar power left, see if any smaller load currently assigned to GRID
    // can fit in the leftover solar watts without displacing larger loads.
    for (auto& cand : active_candidates) {
        if (loads_[cand.index].assigned_on &&
            loads_[cand.index].assigned_source == farm::PowerSource::GRID &&
            remaining_solar_w >= cand.watts) {
            loads_[cand.index].assigned_source = farm::PowerSource::SOLAR;
            remaining_solar_w -= cand.watts;
        }
    }

    // 5. Generate output decisions
    for (size_t i = 0; i < static_cast<size_t>(LoadIndex::MAX); ++i) {
        auto& load = loads_[i];
        bool current_on = (load.last_status.load_state == farm::LoadState::RUNNING);
        farm::PowerSource current_src = load.last_status.active_source;

        bool state_differs = (current_on != load.assigned_on) || (current_src != load.assigned_source);

        LoadControlDecision dec;
        dec.load_index = load.load_index;
        dec.node_id = load.last_status.node_id;
        dec.circuit_id = load.last_status.circuit_id;
        dec.should_be_on = load.assigned_on;
        dec.target_source = load.assigned_source;
        dec.watchdog_s = load.current_watchdog_s;
        dec.action_required = state_differs;

        decisions.push_back(dec);
    }

    return decisions;
}
