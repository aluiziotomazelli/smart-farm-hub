// host_test/test_hub/main/test_command_manager.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "command_manager.hpp"
#include "farm_protocol_types.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_time_manager.hpp"
#include "node_registry.hpp"

using namespace hub;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class CommandManagerTest : public ::testing::Test
{
protected:
    NiceMock<espnow::MockEspNowManager> mock_espnow_;
    NodeRegistry node_registry_;
    NiceMock<time_manager::MockTimeManager> mock_time_;

    void SetUp() override
    {
        node_registry_.clear();
    }
};

TEST_F(CommandManagerTest, DeepSleepNode_EnqueuesInRAMFIFOQueue)
{
    CommandManager sut(mock_espnow_, node_registry_, mock_time_);

    auto target_node = farm::NodeId::WATER_TANK;
    node_registry_.set_power_profile(target_node, farm::PowerProfile::DEEP_SLEEP);

    EXPECT_CALL(mock_espnow_, send_command(_, _, _, _, _)).Times(0);

    bool result = sut.send_command(target_node, espnow::CommandType::START_OTA, true);
    EXPECT_TRUE(result);
}

TEST_F(CommandManagerTest, AlwaysOnNode_SendsImmediatelyOverEspNow)
{
    CommandManager sut(mock_espnow_, node_registry_, mock_time_);

    auto target_node = farm::NodeId::WATER_TANK;
    node_registry_.set_power_profile(target_node, farm::PowerProfile::ALWAYS_ON);

    EXPECT_CALL(
        mock_espnow_, send_command(static_cast<uint8_t>(target_node), espnow::CommandType::REBOOT, nullptr, 0, true))
        .WillOnce(Return(ESP_OK));

    bool result = sut.send_command(target_node, espnow::CommandType::REBOOT, true);
    EXPECT_TRUE(result);
    EXPECT_EQ(sut.get_commands_sent(), 1);
}

TEST_F(CommandManagerTest, ProcessNodeWake_DrainsFIFOAndChecksDrift)
{
    CommandManager sut(mock_espnow_, node_registry_, mock_time_);

    auto target_node = farm::NodeId::WATER_TANK;
    node_registry_.set_power_profile(target_node, farm::PowerProfile::DEEP_SLEEP);

    // Push 2 commands into RAM FIFO
    EXPECT_TRUE(sut.send_command(target_node, espnow::CommandType::START_OTA, true));
    EXPECT_TRUE(sut.send_command(target_node, espnow::CommandType::REBOOT, true));

    EXPECT_CALL(mock_time_, is_synchronized()).WillRepeatedly(Return(false));

    EXPECT_CALL(
        mock_espnow_, send_command(static_cast<uint8_t>(target_node), espnow::CommandType::START_OTA, nullptr, 0, true))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(
        mock_espnow_, send_command(static_cast<uint8_t>(target_node), espnow::CommandType::REBOOT, nullptr, 0, true))
        .WillOnce(Return(ESP_OK));

    sut.process_node_wake(target_node, 1000000);

    EXPECT_EQ(sut.get_messages_received(), 1);
    EXPECT_EQ(sut.get_commands_sent(), 2);
}

TEST_F(CommandManagerTest, AutoTimeSync_ArmsTimeSyncWhenClockDriftExceedsThreshold)
{
    CommandManager sut(mock_espnow_, node_registry_, mock_time_);

    auto target_node = farm::NodeId::WATER_TANK;
    node_registry_.set_power_profile(target_node, farm::PowerProfile::DEEP_SLEEP);

    EXPECT_CALL(mock_time_, is_synchronized()).WillRepeatedly(Return(true));
    EXPECT_CALL(mock_time_, get_timestamp_ms()).WillRepeatedly(Return(100000));

    time_manager::TimeSyncPacket sync_pkt{};
    sync_pkt.timestamp_ms = 100000;
    EXPECT_CALL(mock_time_, create_time_packet()).WillRepeatedly(Return(sync_pkt));

    EXPECT_CALL(
        mock_espnow_,
        send_command(
            static_cast<uint8_t>(target_node),
            static_cast<espnow::CommandType>(farm::CommandType::SYNC_TIME),
            _,
            sizeof(farm::TimeSyncCommand),
            false))
        .WillOnce(Return(ESP_OK));

    // Wake with clock drift = 50000 ms (exceeds 5000 ms threshold)
    sut.process_node_wake(target_node, 50000);

    EXPECT_EQ(sut.get_messages_received(), 1);
    EXPECT_EQ(sut.get_commands_sent(), 1);
}

TEST_F(CommandManagerTest, DispatchDecision_SendsLoadOnCommand)
{
    CommandManager sut(mock_espnow_, node_registry_, mock_time_);

    LoadControlDecision decision{
        .load_index = LoadIndex::PUMP,
        .node_id = farm::NodeId::PUMP_CONTROL,
        .circuit_id = 0,
        .should_be_on = true,
        .target_source = farm::PowerSource::SOLAR,
        .watchdog_s = 60,
    };

    EXPECT_CALL(
        mock_espnow_,
        send_command(
            static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL),
            static_cast<espnow::CommandType>(farm::CommandType::LOAD_ON),
            _,
            sizeof(farm::LoadOnCommand),
            true))
        .WillOnce(Return(ESP_OK));

    EXPECT_TRUE(sut.dispatch_decision(decision));
    EXPECT_EQ(sut.get_commands_sent(), 1);
}

TEST_F(CommandManagerTest, DispatchDecision_SendsLoadOffCommand)
{
    CommandManager sut(mock_espnow_, node_registry_, mock_time_);

    LoadControlDecision decision{
        .load_index = LoadIndex::PUMP,
        .node_id = farm::NodeId::PUMP_CONTROL,
        .circuit_id = 0,
        .should_be_on = false,
        .target_source = farm::PowerSource::GRID,
        .watchdog_s = 0,
    };

    EXPECT_CALL(
        mock_espnow_,
        send_command(
            static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL),
            static_cast<espnow::CommandType>(farm::CommandType::LOAD_OFF),
            _,
            sizeof(farm::LoadOffCommand),
            true))
        .WillOnce(Return(ESP_OK));

    EXPECT_TRUE(sut.dispatch_decision(decision));
    EXPECT_EQ(sut.get_commands_sent(), 1);
}

TEST_F(CommandManagerTest, BroadcastTankLevel_SendsPacketToPumpControl)
{
    CommandManager sut(mock_espnow_, node_registry_, mock_time_);

    EXPECT_CALL(
        mock_espnow_,
        send_data(
            static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL),
            static_cast<uint8_t>(farm::PayloadType::TANK_LEVEL_UPDATE),
            _,
            sizeof(farm::TankLevelUpdate),
            false))
        .WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut.broadcast_tank_level(1, 850, false, true), ESP_OK);
}
