// host_test/test_hub/main/test_solar_sensor_handler.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "command_manager.hpp"
#include "farm_protocol_types.hpp"
#include "handlers/solar_sensor_handler.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_timer.hpp"
#include "mock_load_control_task.hpp"
#include "mock_time_manager.hpp"
#include "node_registry.hpp"
#include "ui_snapshot.hpp"

using namespace hub;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class SolarSensorHandlerTest : public ::testing::Test
{
protected:
    UiSnapshot ui_snapshot_;
    NodeRegistry node_registry_;
    NiceMock<MockLoadControlTask> mock_lct_;
    NiceMock<espnow::MockEspNowManager> mock_espnow_;
    time_manager::MockTimeManager mock_time_;
    CommandManager command_mgr_{ mock_espnow_, node_registry_, mock_time_ };
    NiceMock<idf_hals::MockTimerHAL> mock_timer_;

    void SetUp() override
    {
        ON_CALL(mock_timer_, get_time_us()).WillByDefault(Return(1000000ULL)); // 1000 ms
        ON_CALL(mock_time_, get_timestamp_ms()).WillByDefault(Return(1700000000000ULL));
    }
};

TEST_F(SolarSensorHandlerTest, InvalidPayloadLength_ReturnsError)
{
    SolarSensorHandler handler(ui_snapshot_, node_registry_, mock_lct_, command_mgr_, mock_timer_);

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::SOLAR_SENSOR);
    msg.payload_len = sizeof(farm::SolarSensorReport) - 1;

    EXPECT_EQ(handler.handle_payload(msg), espnow::AckStatus::ERROR_INVALID_DATA);
}

TEST_F(SolarSensorHandlerTest, ValidPayload_UpdatesUiSnapshotNodeRegistryAndPostsToLct)
{
    SolarSensorHandler handler(ui_snapshot_, node_registry_, mock_lct_, command_mgr_, mock_timer_);

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
    msg.rssi = -55;
    memcpy(msg.payload, &report, sizeof(report));

    // Expect LoadControlTask to receive solar update with computed power > 0
    EXPECT_CALL(mock_lct_, post_solar_update(::testing::Field(&SolarPowerUpdate::power_w, ::testing::Gt(1000))))
        .WillOnce(Return(ESP_OK));

    EXPECT_EQ(handler.handle_payload(msg), espnow::AckStatus::OK);

    // Verify NodeRegistry was updated
    farm::NodeMetadata node_meta{};
    EXPECT_TRUE(node_registry_.get_node_info(farm::NodeId::SOLAR_SENSOR, node_meta));
    EXPECT_EQ(node_meta.power_profile, farm::PowerProfile::ALWAYS_ON);

    // Verify UiSnapshot was updated
    UiSnapshotData snapshot = ui_snapshot_.get();
    EXPECT_EQ(snapshot.solar_isc_current_ma, 480);
    EXPECT_EQ(snapshot.solar_irradiance_wm2, 800);
    EXPECT_EQ(snapshot.solar_panel_temp_c, 300);
    EXPECT_EQ(snapshot.solar_battery_mv, 4150);
    EXPECT_EQ(snapshot.solar_battery_percent, 95);
    EXPECT_EQ(snapshot.solar_battery_state, farm::BatteryState::NORMAL);
    EXPECT_EQ(snapshot.solar_sensor_status, farm::SensorStatus::OK);
    EXPECT_EQ(snapshot.solar_max_current_ma, 500);
    EXPECT_EQ(snapshot.solar_daily_yield_mah, 1200);
    EXPECT_FALSE(snapshot.is_solar_night());
    EXPECT_EQ(snapshot.solar_node_unix_time, 1700000000000ULL);
    EXPECT_GT(snapshot.solar_power_w_instant, 1000);
}

TEST_F(SolarSensorHandlerTest, PostHandlePayload_DispatchesNodeWakeToCommandManager)
{
    SolarSensorHandler handler(ui_snapshot_, node_registry_, mock_lct_, command_mgr_, mock_timer_);

    farm::SolarSensorReport report{};
    report.unix_time = 1700000000000ULL;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::SOLAR_SENSOR);
    msg.payload_len = sizeof(farm::SolarSensorReport);
    memcpy(msg.payload, &report, sizeof(farm::SolarSensorReport));

    handler.post_handle_payload(msg);

    EXPECT_EQ(command_mgr_.get_messages_received(), 1);
}
