// host_test/test_hub/main/test_ui_snapshot.cpp
#include <gtest/gtest.h>
#include "ui_snapshot.hpp"

TEST(UiSnapshotTest, DefaultValuesAreZeroInitialized)
{
    UiSnapshot snapshot;
    UiSnapshotData data = snapshot.get();

    EXPECT_EQ(data.water_level_permille, 0);
    EXPECT_EQ(data.solar_power_w_instant, 0);
    EXPECT_FALSE(data.wifi_connected);
    EXPECT_TRUE(data.solar_available);
    EXPECT_TRUE(data.grid_available);
    EXPECT_EQ(data.total_solar_consumption_w(), 0);
    EXPECT_EQ(data.power_margin_w(), 0);
}

TEST(UiSnapshotTest, UpdateWaterTankTelemetry)
{
    UiSnapshot snapshot;
    snapshot.update_water_tank(
        12345,
        850,
        15.5f,
        3300,
        90,
        farm::BatteryState::NORMAL,
        farm::SensorStatus::OK,
        false,
        false,
        1700000000000ULL);

    UiSnapshotData data = snapshot.get();
    EXPECT_EQ(data.last_water_update_ts, 12345);
    EXPECT_EQ(data.water_level_permille, 850);
    EXPECT_FLOAT_EQ(data.water_distance_cm, 15.5f);
    EXPECT_EQ(data.water_battery_mv, 3300);
    EXPECT_EQ(data.water_battery_percent, 90);
    EXPECT_EQ(data.water_battery_state, farm::BatteryState::NORMAL);
    EXPECT_EQ(data.water_sensor_status, farm::SensorStatus::OK);
    EXPECT_FALSE(data.water_float_switch_full);
    EXPECT_FALSE(data.water_backup_mode);
    EXPECT_EQ(data.water_node_unix_time, 1700000000000ULL);
}

TEST(UiSnapshotTest, UpdateSolarTelemetry)
{
    UiSnapshot snapshot;
    snapshot.update_solar(
        54321,
        550,
        920,
        355,
        4100,
        98,
        farm::BatteryState::FULL,
        farm::SensorStatus::OK,
        620,
        1500,
        false,
        1700000000000ULL,
        600,
        1250.5f);

    UiSnapshotData data = snapshot.get();
    EXPECT_EQ(data.last_solar_update_ts, 54321);
    EXPECT_EQ(data.solar_isc_current_ma, 550);
    EXPECT_EQ(data.solar_irradiance_wm2, 920);
    EXPECT_EQ(data.solar_panel_temp_c, 355);
    EXPECT_EQ(data.solar_power_w_instant, 600);
    EXPECT_FLOAT_EQ(data.solar_daily_yield_wh_hub, 1250.5f);
    EXPECT_FALSE(data.is_solar_night());
}

TEST(UiSnapshotTest, UpdateEnergyAndLoads)
{
    UiSnapshot snapshot;

    std::array<LoadState, static_cast<size_t>(LoadIndex::MAX)> loads{};
    std::array<EpisodicWindowState, static_cast<size_t>(LoadIndex::MAX)> window_states{};

    loads[static_cast<size_t>(LoadIndex::PUMP)].node_id = farm::NodeId::PUMP_CONTROL;
    loads[static_cast<size_t>(LoadIndex::PUMP)].circuit_id = 0;
    loads[static_cast<size_t>(LoadIndex::PUMP)].load_state = farm::LoadState::RUNNING;
    loads[static_cast<size_t>(LoadIndex::PUMP)].active_source = farm::PowerSource::SOLAR;
    loads[static_cast<size_t>(LoadIndex::PUMP)].power_w = 450;
    window_states[static_cast<size_t>(LoadIndex::PUMP)] = EpisodicWindowState::RUNNING_WINDOW;

    loads[static_cast<size_t>(LoadIndex::ROUTER)].node_id = farm::NodeId::HUB;
    loads[static_cast<size_t>(LoadIndex::ROUTER)].circuit_id = 1;
    loads[static_cast<size_t>(LoadIndex::ROUTER)].load_state = farm::LoadState::RUNNING;
    loads[static_cast<size_t>(LoadIndex::ROUTER)].active_source = farm::PowerSource::SOLAR;
    loads[static_cast<size_t>(LoadIndex::ROUTER)].power_w = 75;
    window_states[static_cast<size_t>(LoadIndex::ROUTER)] = EpisodicWindowState::IDLE;

    snapshot.update_energy_and_loads(1000, 525, 475, true, true, loads, window_states);

    UiSnapshotData data = snapshot.get();
    EXPECT_EQ(data.solar_power_w_instant, 1000);
    EXPECT_EQ(data.total_solar_allocated_w, 525);
    EXPECT_EQ(data.solar_headroom_w, 475);
    EXPECT_EQ(data.load(LoadIndex::PUMP).power_w, 450);
    EXPECT_EQ(data.load_window_state(LoadIndex::PUMP), EpisodicWindowState::RUNNING_WINDOW);
    EXPECT_EQ(data.total_solar_consumption_w(), 525);
    EXPECT_EQ(data.power_margin_w(), 475);
}
