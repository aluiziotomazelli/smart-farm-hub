// host_test/test_hub/main/test_load_control_handler.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "command_manager.hpp"
#include "farm_protocol_types.hpp"
#include "handlers/load_control_handler.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_timer.hpp"
#include "mock_load_control_task.hpp"
#include "mock_time_manager.hpp"
#include "node_registry.hpp"

using namespace hub;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class LoadControlHandlerTest : public ::testing::Test
{
protected:
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

TEST_F(LoadControlHandlerTest, InvalidPayloadLength_ReturnsError)
{
    LoadControlHandler handler(node_registry_, mock_lct_, command_mgr_, mock_timer_);

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL);
    msg.payload_len = sizeof(farm::LoadControlStatus) - 1;

    EXPECT_EQ(handler.handle_payload(msg), espnow::AckStatus::ERROR_INVALID_DATA);
}

TEST_F(LoadControlHandlerTest, ValidPumpControlPayload_UpdatesNodeRegistryAndPostsToLct)
{
    LoadControlHandler handler(node_registry_, mock_lct_, command_mgr_, mock_timer_);

    farm::LoadControlStatus report{};
    report.circuit_id = 0;
    report.power_profile = farm::PowerProfile::ALWAYS_ON;
    report.control_mode = farm::ControlMode::AUTO;
    report.active_power_source = farm::PowerSource::SOLAR;
    report.load_state = farm::LoadState::RUNNING;
    report.power_w = 1500;
    report.runtime_s = 120;
    report.uptime_s = 3600;
    report.unix_time = 1700000000000ULL;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL);
    msg.payload_len = sizeof(report);
    memcpy(msg.payload, &report, sizeof(report));

    // Verify LCT receives LoadStatusUpdate with matching pump fields
    EXPECT_CALL(mock_lct_, post_load_status(::testing::AllOf(
        ::testing::Field(&LoadStatusUpdate::load_index, LoadIndex::PUMP),
        ::testing::Field(&LoadStatusUpdate::power_w, 1500),
        ::testing::Field(&LoadStatusUpdate::runtime_s, 120),
        ::testing::Field(&LoadStatusUpdate::load_state, farm::LoadState::RUNNING),
        ::testing::Field(&LoadStatusUpdate::active_source, farm::PowerSource::SOLAR)
    ))).WillOnce(Return(ESP_OK));

    EXPECT_EQ(handler.handle_payload(msg), espnow::AckStatus::OK);

    // Verify NodeRegistry has node profile registered
    farm::NodeMetadata meta{};
    EXPECT_TRUE(node_registry_.get_node_info(farm::NodeId::PUMP_CONTROL, meta));
    EXPECT_EQ(meta.power_profile, farm::PowerProfile::ALWAYS_ON);
}

TEST_F(LoadControlHandlerTest, PostHandlePayload_DispatchesNodeWakeToCommandManager)
{
    LoadControlHandler handler(node_registry_, mock_lct_, command_mgr_, mock_timer_);

    farm::LoadControlStatus report{};
    report.unix_time = 1700000000000ULL;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL);
    msg.payload_len = sizeof(farm::LoadControlStatus);
    memcpy(msg.payload, &report, sizeof(farm::LoadControlStatus));

    handler.post_handle_payload(msg);

    EXPECT_EQ(command_mgr_.get_messages_received(), 1);
}
