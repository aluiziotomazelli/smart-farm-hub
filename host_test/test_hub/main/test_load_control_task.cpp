// host_test/test_hub/main/test_load_control_task.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hal_freertos.hpp"
#include "interfaces/i_energy_monitor.hpp"
#include "interfaces/i_load_actuator_dispatcher.hpp"
#include "load_control_task.hpp"
#include "mock_time_manager.hpp"
#include "ui_snapshot.hpp"

using namespace hub;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class MockEnergyMonitor : public IEnergyMonitor {
public:
    MOCK_METHOD(bool, is_solar_available, (), (const, override));
    MOCK_METHOD(bool, is_grid_available, (), (const, override));
};

class MockLoadActuatorDispatcher : public ILoadActuatorDispatcher {
public:
    MOCK_METHOD(bool, dispatch_decision, (const LoadControlDecision& decision), (override));
};

class LoadControlTaskTest : public ::testing::Test {
protected:
    idf_hals::HalFreertos rtos_; // Real FreeRTOS HAL for queues, semaphores and tasks on host
    NiceMock<time_manager::MockTimeManager> time_mgr_;
    UiSnapshot ui_snapshot_;
    NiceMock<MockLoadActuatorDispatcher> actuator_dispatcher_;
    NiceMock<MockEnergyMonitor> energy_monitor_;

    void SetUp() override
    {
        ON_CALL(time_mgr_, get_timestamp_ms()).WillByDefault(Return(1000ULL));
        ON_CALL(energy_monitor_, is_solar_available()).WillByDefault(Return(true));
        ON_CALL(energy_monitor_, is_grid_available()).WillByDefault(Return(true));
    }
};

TEST_F(LoadControlTaskTest, InitializationCreatesQueuesAndQueueSet)
{
    LoadControlTask task(rtos_, time_mgr_, ui_snapshot_, actuator_dispatcher_, energy_monitor_);
    EXPECT_EQ(task.init(), ESP_OK);
    EXPECT_NE(task.get_energy_semaphore(), nullptr);
}

TEST_F(LoadControlTaskTest, PostEventsBeforeInitReturnsError)
{
    LoadControlTask task(rtos_, time_mgr_, ui_snapshot_, actuator_dispatcher_, energy_monitor_);

    SolarPowerUpdate solar{500, 800, false, 1000};
    EXPECT_EQ(task.post_solar_update(solar), ESP_ERR_INVALID_STATE);

    LoadIntent intent{LoadIndex::PUMP, farm::NodeId::PUMP_CONTROL, 0, LoadDesiredState::ON};
    EXPECT_EQ(task.post_load_intent(intent), ESP_ERR_INVALID_STATE);
}

TEST_F(LoadControlTaskTest, PostEventsAfterInitSucceeds)
{
    LoadControlTask task(rtos_, time_mgr_, ui_snapshot_, actuator_dispatcher_, energy_monitor_);
    ASSERT_EQ(task.init(), ESP_OK);

    SolarPowerUpdate solar{1200, 950, false, 1000};
    EXPECT_EQ(task.post_solar_update(solar), ESP_OK);

    LoadIntent intent{LoadIndex::PUMP, farm::NodeId::PUMP_CONTROL, 0, LoadDesiredState::ON, LoadUrgency::NORMAL, 600, 1800, SourcePreference::SOLAR_ONLY};
    EXPECT_EQ(task.post_load_intent(intent), ESP_OK);

    LoadStatusUpdate status{LoadIndex::PUMP, farm::NodeId::PUMP_CONTROL, 0, farm::ControlMode::AUTO, farm::PowerSource::SOLAR, farm::LoadState::RUNNING, 450, 60, 1000};
    EXPECT_EQ(task.post_load_status(status), ESP_OK);

    EXPECT_EQ(task.notify_energy_availability(true, true), ESP_OK);
}

TEST_F(LoadControlTaskTest, RealTaskExecutesArbitrationAndUpdatesUiSnapshot)
{
    LoadControlTask task(rtos_, time_mgr_, ui_snapshot_, actuator_dispatcher_, energy_monitor_);
    ASSERT_EQ(task.init(), ESP_OK);

    // Expect dispatcher to be called when pump turns ON
    EXPECT_CALL(actuator_dispatcher_, dispatch_decision(_))
        .WillRepeatedly(Return(true));

    ASSERT_EQ(task.start(), ESP_OK);

    // 1. Post solar generation (1500W)
    SolarPowerUpdate solar{1500, 950, false, 1000};
    EXPECT_EQ(task.post_solar_update(solar), ESP_OK);

    // 2. Post Intent to turn ON Pump (450W)
    LoadIntent pump_intent{
        LoadIndex::PUMP,
        farm::NodeId::PUMP_CONTROL,
        0,
        LoadDesiredState::ON,
        LoadUrgency::NORMAL,
        600,
        1800,
        SourcePreference::SOLAR_ONLY};
    EXPECT_EQ(task.post_load_intent(pump_intent), ESP_OK);

    // Give real FreeRTOS task ~150ms to select from QueueSet, evaluate Knapsack and refresh UiSnapshot
    rtos_.task_delay(pdMS_TO_TICKS(150));

    // Verify UiSnapshot was updated with solar power and pump allocation
    UiSnapshotData snapshot = ui_snapshot_.get();
    EXPECT_EQ(snapshot.solar_power_w_instant, 1500);
    EXPECT_EQ(snapshot.load(LoadIndex::PUMP).active_source, farm::PowerSource::SOLAR);
    EXPECT_EQ(snapshot.load(LoadIndex::PUMP).load_state, farm::LoadState::RUNNING);

    task.stop();
}
