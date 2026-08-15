// host_test/test_hub/main/test_solar_sensor_handler.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "command_manager.hpp"
#include "handlers/solar_sensor_handler.hpp"
#include "hub_nvs.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_timer.hpp"
#include "mock_persistence_backend.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class MockTimeManager : public time_manager::ITimeManager
{
public:
    MOCK_METHOD(esp_err_t, init, (const time_manager::TimeManagerConfig& config), (override));
    MOCK_METHOD(esp_err_t, start_sntp, (), (override));
    MOCK_METHOD(esp_err_t, stop_sntp, (), (override));
    MOCK_METHOD(esp_err_t, request_sync, (), (override));
    MOCK_METHOD(bool, is_synchronized, (), (const, override));
    MOCK_METHOD(time_t, get_timestamp_sec, (), (const, override));
    MOCK_METHOD(uint64_t, get_timestamp_ms, (), (const, override));
    MOCK_METHOD(bool, get_formatted_time, (char* buf, size_t max_len, const char* format), (const, override));
    MOCK_METHOD(void, set_timezone, (const char* tz), (override));
    MOCK_METHOD(time_manager::TimeSyncPacket, create_time_packet, (), (const, override));
    MOCK_METHOD(esp_err_t, sync_from_time_packet, (const time_manager::TimeSyncPacket& packet), (override));
};

class SolarSensorHandlerTest : public ::testing::Test
{
protected:
    espnow::MockEspNowManager mock_espnow_;
    NiceMock<MockPersistenceBackend> rtc_backend_;
    NiceMock<MockPersistenceBackend> nvs_backend_;
    HubNvs hub_storage_{rtc_backend_, nvs_backend_};
    MockTimeManager mock_time_;
    NiceMock<idf_hals::MockHalFreertos> mock_rtos_;
    NiceMock<idf_hals::MockTimerHAL> mock_timer_;

    HubStats stats_;
    SystemState state_;
    SemaphoreHandle_t dummy_mutex_ = reinterpret_cast<SemaphoreHandle_t>(0x112233);
    EventGroupHandle_t dummy_event_group_ = reinterpret_cast<EventGroupHandle_t>(0x445566);

    void SetUp() override
    {
        rtc_backend_.UseRealStorage();
        nvs_backend_.UseRealStorage();
        stats_.reset();
        ON_CALL(mock_rtos_, semaphore_take(_, _)).WillByDefault(Return(pdTRUE));
        ON_CALL(mock_rtos_, semaphore_give(_)).WillByDefault(Return(pdTRUE));
        ON_CALL(mock_timer_, get_time_us()).WillByDefault(Return(1000000ULL)); // 1000 ms
    }
};

TEST_F(SolarSensorHandlerTest, InvalidPayloadLength_ReturnsError)
{
    hub::CommandManager command_mgr(mock_espnow_, stats_, hub_storage_, mock_time_, state_, dummy_mutex_, mock_rtos_);
    hub::SolarSensorHandler sut(state_, dummy_mutex_, command_mgr, mock_timer_, mock_rtos_);

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::SOLAR_SENSOR);
    msg.payload_len = sizeof(farm::SolarSensorReport) - 1;

    auto status = sut.handle_payload(msg);
    EXPECT_EQ(status, espnow::AckStatus::ERROR_INVALID_DATA);
}

TEST_F(SolarSensorHandlerTest, ValidPayload_UpdatesStateAndComputesPower)
{
    hub::CommandManager command_mgr(mock_espnow_, stats_, hub_storage_, mock_time_, state_, dummy_mutex_, mock_rtos_);
    hub::SolarSensorHandler sut(state_, dummy_mutex_, command_mgr, mock_timer_, mock_rtos_, dummy_event_group_);

    farm::SolarSensorReport report{};
    report.power_profile = farm::PowerProfile::ALWAYS_ON;
    report.isc_current_ma = 480;
    report.irradiance_wm2 = 800;
    report.panel_temp_c = 300; // 30.0 °C
    report.battery_mv = 4150;
    report.battery_percent = 95;
    report.battery_state = farm::BatteryState::NORMAL;
    report.status = farm::SensorStatus::OK;
    report.max_current_ma = 500;
    report.daily_yield_mah = 1200;
    report.is_night_mode = false;
    report.unix_time = 1700000000000ULL;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::SOLAR_SENSOR);
    msg.payload_len = sizeof(report);
    memcpy(msg.payload, &report, sizeof(report));

    EXPECT_CALL(mock_rtos_, event_group_set_bits(dummy_event_group_, 1 << 0)).Times(1);

    auto status = sut.handle_payload(msg);
    EXPECT_EQ(status, espnow::AckStatus::OK);

    // Verify raw fields
    EXPECT_EQ(state_.solar_isc_current_ma, 480);
    EXPECT_EQ(state_.solar_irradiance_wm2, 800);
    EXPECT_EQ(state_.solar_panel_temp_c, 300);
    EXPECT_EQ(state_.solar_battery_mv, 4150);
    EXPECT_EQ(state_.solar_battery_percent, 95);
    EXPECT_EQ(state_.solar_battery_state, farm::BatteryState::NORMAL);
    EXPECT_EQ(state_.solar_sensor_status, farm::SensorStatus::OK);
    EXPECT_EQ(state_.solar_max_current_ma, 500);
    EXPECT_EQ(state_.solar_daily_yield_mah, 1200);
    EXPECT_FALSE(state_.solar_is_night_mode);
    EXPECT_EQ(state_.solar_node_unix_time, 1700000000000ULL);

    // Verify calculated power: 2640 * 0.8 * (1 - 0.004 * 5) * 0.85 = 2640 * 0.8 * 0.98 * 0.85 = 1759 W
    EXPECT_EQ(state_.solar_power_w_instant, 1759);
    EXPECT_EQ(state_.solar_power_w_avg, 1759);

    // Verify min/max temperature tracking
    EXPECT_EQ(state_.solar_panel_temp_max_c, 300);
    EXPECT_EQ(state_.solar_panel_temp_min_c, 300);

    // Verify node metadata update in stats and state
    EXPECT_EQ(stats_.node_info[0].power_profile, farm::PowerProfile::ALWAYS_ON);
}

TEST_F(SolarSensorHandlerTest, PostHandlePayload_ProcessesNodeWake)
{
    hub::CommandManager command_mgr(mock_espnow_, stats_, hub_storage_, mock_time_, state_, dummy_mutex_, mock_rtos_);
    hub::SolarSensorHandler sut(state_, dummy_mutex_, command_mgr, mock_timer_, mock_rtos_);

    farm::SolarSensorReport report{};
    report.unix_time = 1700000000000ULL;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::SOLAR_SENSOR);
    msg.payload_len = sizeof(report);
    memcpy(msg.payload, &report, sizeof(report));

    sut.post_handle_payload(msg);
}
