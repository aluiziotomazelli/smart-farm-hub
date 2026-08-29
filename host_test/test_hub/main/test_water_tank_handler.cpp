// host_test/test_hub/main/test_water_tank_handler.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "command_manager.hpp"
#include "farm_protocol_types.hpp"
#include "handlers/water_tank_handler.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_timer.hpp"
#include "mock_load_control_task.hpp"
#include "mock_time_manager.hpp"
#include "node_registry.hpp"
#include "sun_schedule.hpp"
#include "tank_controller.hpp"
#include "ui_snapshot.hpp"

using namespace hub;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class WaterTankHandlerTest : public ::testing::Test {
protected:
    static constexpr time_t NOON_EPOCH = 1700060400; // 2023-11-15 12:00:00 local
    UiSnapshot ui_snapshot_;
    NodeRegistry node_registry_;
    NiceMock<time_manager::MockTimeManager> mock_time_mgr_;
    SunSchedule sun_schedule_{ -23.5505f, -3.0f }; // SP coords
    TankController tank_controller_{ mock_time_mgr_, sun_schedule_ };
    NiceMock<MockLoadControlTask> mock_lct_;
    NiceMock<espnow::MockEspNowManager> mock_espnow_;
    CommandManager command_mgr_{ mock_espnow_, node_registry_, mock_time_mgr_ };
    NiceMock<idf_hals::MockTimerHAL> mock_timer_;

    void SetUp() override
    {
        ON_CALL(mock_timer_, get_time_us()).WillByDefault(Return(1000000ULL)); // 1000ms
        ON_CALL(mock_time_mgr_, get_timestamp_ms()).WillByDefault(Return(static_cast<uint64_t>(NOON_EPOCH) * 1000ULL));
        ON_CALL(mock_time_mgr_, get_timestamp_sec()).WillByDefault(Return(NOON_EPOCH));
    }
};

TEST_F(WaterTankHandlerTest, HandleShortPayload_ReturnsInvalidData)
{
    WaterTankHandler handler(ui_snapshot_, node_registry_, tank_controller_, mock_lct_, command_mgr_, mock_timer_);

    espnow::AppMessage msg{};
    msg.payload_len = sizeof(farm::WaterLevelReport) - 1;
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::WATER_TANK);

    EXPECT_EQ(handler.handle_payload(msg), espnow::AckStatus::ERROR_INVALID_DATA);
}

TEST_F(WaterTankHandlerTest, HandleValidWaterReport_UpdatesAllSubsystemsAndEmitsIntent)
{
    WaterTankHandler handler(ui_snapshot_, node_registry_, tank_controller_, mock_lct_, command_mgr_, mock_timer_);

    farm::WaterLevelReport report{};
    report.power_profile = farm::PowerProfile::DEEP_SLEEP;
    report.level_permille = 450; // Below normal_min_permille (500) -> Should trigger ON
    report.distance_cm = 55.0f;
    report.battery_mv = 3300;
    report.battery_percent = 85;
    report.battery_state = farm::BatteryState::NORMAL;
    report.status = farm::SensorStatus::OK;
    report.float_switch_is_full = false;
    report.backup_mode_active = false;
    report.unix_time = 1700000000000ULL;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::WATER_TANK);
    msg.payload_len = sizeof(farm::WaterLevelReport);
    msg.rssi = -65;
    memcpy(msg.payload, &report, sizeof(farm::WaterLevelReport));

    // Verify handle_payload validates and updates NodeRegistry and UiSnapshot
    EXPECT_EQ(handler.handle_payload(msg), espnow::AckStatus::OK);

    // Verify NodeRegistry was updated
    farm::NodeMetadata node_meta{};
    EXPECT_TRUE(node_registry_.get_node_info(farm::NodeId::WATER_TANK, node_meta));
    EXPECT_EQ(node_meta.power_profile, farm::PowerProfile::DEEP_SLEEP);

    // Verify UiSnapshot was updated
    UiSnapshotData snapshot = ui_snapshot_.get();
    EXPECT_EQ(snapshot.water_level_permille, 450);
    EXPECT_FLOAT_EQ(snapshot.water_distance_cm, 55.0f);
    EXPECT_EQ(snapshot.water_battery_mv, 3300);
    EXPECT_EQ(snapshot.water_battery_percent, 85);
    EXPECT_FALSE(snapshot.water_float_switch_full);
    EXPECT_FALSE(snapshot.water_backup_mode);

    // Expect LoadControlTask to receive LoadIntent with desired_state = ON during post_handle_payload (after ACK)
    EXPECT_CALL(mock_lct_, post_load_intent(::testing::Field(&LoadIntent::desired_state, LoadDesiredState::ON)))
        .WillOnce(Return(ESP_OK));

    handler.post_handle_payload(msg);
}

TEST_F(WaterTankHandlerTest, PostHandlePayload_DispatchesNodeWakeToCommandManager)
{
    WaterTankHandler handler(ui_snapshot_, node_registry_, tank_controller_, mock_lct_, command_mgr_, mock_timer_);

    farm::WaterLevelReport report{};
    report.unix_time = 1700000000000ULL;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::WATER_TANK);
    msg.payload_type = static_cast<uint8_t>(farm::PayloadType::WATER_LEVEL_REPORT);
    msg.payload_len = sizeof(farm::WaterLevelReport);
    memcpy(msg.payload, &report, sizeof(farm::WaterLevelReport));

    handler.post_handle_payload(msg);

    EXPECT_EQ(command_mgr_.get_messages_received(), 1);
}

TEST_F(WaterTankHandlerTest, FillRequest_ShortPayload_ReturnsInvalidData)
{
    WaterTankHandler handler(ui_snapshot_, node_registry_, tank_controller_, mock_lct_, command_mgr_, mock_timer_);

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL);
    msg.payload_type = static_cast<uint8_t>(farm::PayloadType::FILL_REQUEST);
    msg.payload_len = sizeof(farm::FillRequest) - 1;

    EXPECT_EQ(handler.handle_payload(msg), espnow::AckStatus::ERROR_INVALID_DATA);
}

TEST_F(WaterTankHandlerTest, FillRequest_ValidPayload_TriggersManualFillInTankControllerAndPostsIntent)
{
    WaterTankHandler handler(ui_snapshot_, node_registry_, tank_controller_, mock_lct_, command_mgr_, mock_timer_);

    // Preset tank satisfied at 1000‰ (IDLE)
    tank_controller_.on_tank_report(1000, false, false, NOON_EPOCH);
    EXPECT_EQ(tank_controller_.get_state(), TankState::IDLE);

    // Drain slightly to 950‰ so manual fill request will become FILL_REQUESTED
    tank_controller_.on_tank_report(950, false, false, NOON_EPOCH);

    farm::FillRequest req{ .circuit_id = 0 };

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL);
    msg.payload_type = static_cast<uint8_t>(farm::PayloadType::FILL_REQUEST);
    msg.payload_len = sizeof(farm::FillRequest);
    memcpy(msg.payload, &req, sizeof(farm::FillRequest));

    EXPECT_EQ(handler.handle_payload(msg), espnow::AckStatus::OK);

    // Expect LoadControlTask to receive LoadIntent with desired_state = ON
    EXPECT_CALL(mock_lct_, post_load_intent(::testing::Field(&LoadIntent::desired_state, LoadDesiredState::ON)))
        .WillOnce(Return(ESP_OK));

    handler.post_handle_payload(msg);

    EXPECT_EQ(tank_controller_.get_state(), TankState::FILL_REQUESTED);
    EXPECT_EQ(command_mgr_.get_messages_received(), 1);
}
