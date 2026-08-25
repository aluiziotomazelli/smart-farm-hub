// host_test/test_hub/main/test_load_control_engine.cpp
#include <gtest/gtest.h>

#include "load_control_engine.hpp"

class LoadControlEngineTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Setup default profiles
        PriorityConfig cfg;
        cfg.profiles[static_cast<size_t>(LoadIndex::PUMP)] = {
            .expected_watts_running = 600,
            .expected_watts_idle = 0,
            .can_shed = true,
            .max_shed_duration_s = 0,
            .min_switch_interval_s = 5,
            .max_wait_window_s = 600,
            .is_continuous = false,
            .priority_rank = 4,
        };
        cfg.profiles[static_cast<size_t>(LoadIndex::FREEZER)] = {
            .expected_watts_running = 700,
            .expected_watts_idle = 40,
            .can_shed = true,
            .max_shed_duration_s = 30 * 60, // 30 min hold
            .min_switch_interval_s = 180,
            .max_wait_window_s = 0,
            .is_continuous = true,
            .priority_rank = 2,
        };
        cfg.profiles[static_cast<size_t>(LoadIndex::FRIDGE)] = {
            .expected_watts_running = 350,
            .expected_watts_idle = 30,
            .can_shed = true,
            .max_shed_duration_s = 20 * 60, // 20 min hold
            .min_switch_interval_s = 180,
            .max_wait_window_s = 0,
            .is_continuous = true,
            .priority_rank = 3,
        };
        cfg.profiles[static_cast<size_t>(LoadIndex::ROUTER)] = {
            .expected_watts_running = 75,
            .expected_watts_idle = 30,
            .can_shed = false,
            .max_shed_duration_s = 0,
            .min_switch_interval_s = 600,
            .max_wait_window_s = 0,
            .is_continuous = true,
            .priority_rank = 1,
        };

        engine_ = LoadControlEngine(cfg);
    }

    LoadControlEngine engine_;
};

TEST_F(LoadControlEngineTest, AmpleSolarPutsAllLoadsOnSolar)
{
    // 2000W Solar
    engine_.on_solar_update({.power_w = 2000, .irradiance_wm2 = 900, .is_night_mode = false, .timestamp_ms = 1000});

    // All loads want ON
    engine_.on_load_intent({.load_index = LoadIndex::ROUTER, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::CRITICAL});
    engine_.on_load_intent({.load_index = LoadIndex::FREEZER, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_intent({.load_index = LoadIndex::FRIDGE, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_intent({.load_index = LoadIndex::PUMP, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});

    auto decisions = engine_.evaluate_arbitration();

    // All should be on SOLAR
    for (const auto& dec : decisions) {
        if (dec.load_index == LoadIndex::ROUTER || dec.load_index == LoadIndex::FREEZER ||
            dec.load_index == LoadIndex::FRIDGE || dec.load_index == LoadIndex::PUMP) {
            EXPECT_TRUE(dec.should_be_on);
            EXPECT_EQ(dec.target_source, farm::PowerSource::SOLAR);
        }
    }
}

TEST_F(LoadControlEngineTest, KnapsackOptimizationShedsSmallestLoadToFitRest)
{
    // 1700W Solar: Total demand is 700 + 600 + 350 + 75 = 1725W (25W deficit)
    engine_.on_solar_update({.power_w = 1700, .irradiance_wm2 = 700, .is_night_mode = false, .timestamp_ms = 1000});

    engine_.on_load_intent({.load_index = LoadIndex::ROUTER, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_intent({.load_index = LoadIndex::FREEZER, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_intent({.load_index = LoadIndex::FRIDGE, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_intent({.load_index = LoadIndex::PUMP, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});

    engine_.evaluate_arbitration();

    // Heavy loads (Freezer 700, Pump 600, Fridge 350 = 1650W) fit in 1700W
    EXPECT_EQ(engine_.get_load(LoadIndex::FREEZER).assigned_source, farm::PowerSource::SOLAR);
    EXPECT_EQ(engine_.get_load(LoadIndex::PUMP).assigned_source, farm::PowerSource::SOLAR);
    EXPECT_EQ(engine_.get_load(LoadIndex::FRIDGE).assigned_source, farm::PowerSource::SOLAR);

    // Router (75W) migrated to GRID to prevent dropping a 350W or 700W load!
    EXPECT_EQ(engine_.get_load(LoadIndex::ROUTER).assigned_source, farm::PowerSource::GRID);
}

TEST_F(LoadControlEngineTest, RestoresShedLoadWhenHeadroomReappears)
{
    // 1700W Solar with all loads (Router pushed to Grid)
    engine_.on_solar_update({.power_w = 1700, .irradiance_wm2 = 700, .is_night_mode = false, .timestamp_ms = 1000});
    engine_.on_load_intent({.load_index = LoadIndex::ROUTER, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_intent({.load_index = LoadIndex::FREEZER, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_intent({.load_index = LoadIndex::FRIDGE, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_intent({.load_index = LoadIndex::PUMP, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});

    engine_.evaluate_arbitration();
    EXPECT_EQ(engine_.get_load(LoadIndex::ROUTER).assigned_source, farm::PowerSource::GRID);

    // Pump finishes filling and turns OFF
    engine_.on_load_intent({.load_index = LoadIndex::PUMP, .desired_state = LoadDesiredState::OFF});
    engine_.evaluate_arbitration();

    // Router immediately restored to SOLAR!
    EXPECT_EQ(engine_.get_load(LoadIndex::ROUTER).assigned_source, farm::PowerSource::SOLAR);
}

TEST_F(LoadControlEngineTest, InsufficientSolarMigratesPumpToGridIfAllowed)
{
    // 800W Solar: Freezer (700W) running on solar, Pump (600W) cannot fit
    engine_.on_solar_update({.power_w = 800, .irradiance_wm2 = 400, .is_night_mode = false, .timestamp_ms = 1000});

    engine_.on_load_intent({.load_index = LoadIndex::FREEZER, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_intent({.load_index = LoadIndex::PUMP, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::CRITICAL, .source_preference = SourcePreference::SOLAR_PREFERRED});

    engine_.evaluate_arbitration();

    // Freezer stays on solar (700W)
    EXPECT_EQ(engine_.get_load(LoadIndex::FREEZER).assigned_source, farm::PowerSource::SOLAR);
    // Pump goes to GRID (600W)
    EXPECT_EQ(engine_.get_load(LoadIndex::PUMP).assigned_source, farm::PowerSource::GRID);
}

TEST_F(LoadControlEngineTest, ScenarioC_CapturesCompressorOffCycleForEpisodicLoad)
{
    // 750W Solar: Freezer usually 700W, Pump 600W
    engine_.on_solar_update({.power_w = 750, .irradiance_wm2 = 350, .is_night_mode = false, .timestamp_ms = 1000});

    // Freezer compressor is RUNNING at 700W
    engine_.on_load_intent({.load_index = LoadIndex::FREEZER, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::NORMAL});
    engine_.on_load_status({.load_index = LoadIndex::FREEZER, .active_source = farm::PowerSource::SOLAR, .load_state = farm::LoadState::RUNNING, .power_w = 700, .timestamp_ms = 1000});

    // Pump (episodic load) wants Opportunistic run
    engine_.on_load_intent({.load_index = LoadIndex::PUMP, .desired_state = LoadDesiredState::ON, .urgency = LoadUrgency::OPPORTUNISTIC, .source_preference = SourcePreference::SOLAR_PREFERRED});

    engine_.evaluate_arbitration();
    // Pump enters WAITING_FOR_WINDOW
    EXPECT_EQ(engine_.get_load_window_state(LoadIndex::PUMP), EpisodicWindowState::WAITING_FOR_WINDOW);
    EXPECT_FALSE(engine_.get_load(LoadIndex::PUMP).assigned_on);

    // Freezer compressor cycles OFF (power drops to 30W)
    engine_.on_load_status({.load_index = LoadIndex::FREEZER, .active_source = farm::PowerSource::SOLAR, .load_state = farm::LoadState::RUNNING, .power_w = 30, .timestamp_ms = 2000});

    engine_.evaluate_arbitration();
    // Window captured! Pump assigned to SOLAR
    EXPECT_EQ(engine_.get_load_window_state(LoadIndex::PUMP), EpisodicWindowState::RUNNING_WINDOW);
    EXPECT_TRUE(engine_.get_load(LoadIndex::PUMP).assigned_on);
    EXPECT_EQ(engine_.get_load(LoadIndex::PUMP).assigned_source, farm::PowerSource::SOLAR);
}
