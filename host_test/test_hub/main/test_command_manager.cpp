// host_test/test_hub/main/test_command_manager.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "command_manager.hpp"
#include "mock_espnow_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_persistence_backend.hpp"
#include "mock_time_manager.hpp"
#include "hub_nvs.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class CommandManagerTest : public ::testing::Test
{
protected:
    espnow::MockEspNowManager mock_espnow_;
    NiceMock<MockPersistenceBackend> rtc_backend_;
    NiceMock<MockPersistenceBackend> nvs_backend_;
    HubNvs hub_storage_{rtc_backend_, nvs_backend_};
    time_manager::MockTimeManager mock_time_;
    NiceMock<idf_hals::MockHalFreertos> mock_rtos_;

    HubStats stats_;
    SystemState state_;
    SemaphoreHandle_t dummy_mutex_ = reinterpret_cast<SemaphoreHandle_t>(0x112233);

    void SetUp() override
    {
        rtc_backend_.UseRealStorage();
        nvs_backend_.UseRealStorage();
        stats_.reset();
        ON_CALL(mock_rtos_, semaphore_take(_, _)).WillByDefault(Return(pdTRUE));
        ON_CALL(mock_rtos_, semaphore_give(_)).WillByDefault(Return(pdTRUE));
    }
};

TEST_F(CommandManagerTest, DeepSleepNode_EnqueuesInFIFOQueue)
{
    hub::CommandManager sut(mock_espnow_, stats_, hub_storage_, mock_time_, state_, dummy_mutex_, mock_rtos_);

    auto target_node = farm::NodeId::WATER_TANK;
    state_.set_node_power_profile(target_node, farm::PowerProfile::DEEP_SLEEP);

    EXPECT_CALL(mock_espnow_, send_command(_, _, _, _, _)).Times(0);

    bool result = sut.send_command(target_node, espnow::CommandType::START_OTA, true);
    EXPECT_TRUE(result);
    EXPECT_TRUE(stats_.has_pending(target_node));

    PendingNodeCommand peek;
    EXPECT_TRUE(stats_.peek_pending(target_node, peek));
    EXPECT_EQ(peek.command, espnow::CommandType::START_OTA);
}

TEST_F(CommandManagerTest, AlwaysOnNode_SendsImmediatelyOverEspNow)
{
    hub::CommandManager sut(mock_espnow_, stats_, hub_storage_, mock_time_, state_, dummy_mutex_, mock_rtos_);

    auto target_node = farm::NodeId::WATER_TANK;
    state_.set_node_power_profile(target_node, farm::PowerProfile::ALWAYS_ON);

    EXPECT_CALL(
        mock_espnow_, send_command(static_cast<uint8_t>(target_node), espnow::CommandType::REBOOT, nullptr, 0, true))
        .WillOnce(Return(ESP_OK));

    bool result = sut.send_command(target_node, espnow::CommandType::REBOOT, true);
    EXPECT_TRUE(result);
    EXPECT_FALSE(stats_.has_pending(target_node));
    EXPECT_EQ(stats_.commands_sent, 1);
}

TEST_F(CommandManagerTest, ProcessNodeWake_DrainsFIFOAndChecksDrift)
{
    hub::CommandManager sut(mock_espnow_, stats_, hub_storage_, mock_time_, state_, dummy_mutex_, mock_rtos_);

    auto target_node = farm::NodeId::WATER_TANK;
    stats_.push_pending(target_node, espnow::CommandType::START_OTA, true);
    stats_.push_pending(target_node, espnow::CommandType::REBOOT, true);

    EXPECT_CALL(mock_time_, is_synchronized()).WillRepeatedly(Return(false));

    EXPECT_CALL(
        mock_espnow_, send_command(static_cast<uint8_t>(target_node), espnow::CommandType::START_OTA, nullptr, 0, true))
        .WillOnce(Return(ESP_OK));
    EXPECT_CALL(
        mock_espnow_, send_command(static_cast<uint8_t>(target_node), espnow::CommandType::REBOOT, nullptr, 0, true))
        .WillOnce(Return(ESP_OK));

    sut.process_node_wake(target_node, 1000000);

    EXPECT_FALSE(stats_.has_pending(target_node));
    EXPECT_EQ(stats_.messages_received, 1);
    EXPECT_EQ(stats_.commands_sent, 2);
}

TEST_F(CommandManagerTest, AutoTimeSync_ArmsTimeSyncWhenClockDriftExceedsThreshold)
{
    hub::CommandManager sut(mock_espnow_, stats_, hub_storage_, mock_time_, state_, dummy_mutex_, mock_rtos_);

    auto target_node = farm::NodeId::WATER_TANK;

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
            _,
            false))
        .WillOnce(Return(ESP_OK));

    // Node time is 0 (unsynchronized) -> should arm and dispatch SYNC_TIME
    sut.process_node_wake(target_node, 0);

    EXPECT_FALSE(stats_.has_pending(target_node));
    EXPECT_EQ(stats_.commands_sent, 1);
}

TEST_F(CommandManagerTest, BroadcastTankLevel_SendsPayloadToPumpControl)
{
    hub::CommandManager sut(mock_espnow_, stats_, hub_storage_, mock_time_, state_, dummy_mutex_, mock_rtos_);

    EXPECT_CALL(
        mock_espnow_,
        send_data(
            static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL),
            static_cast<uint8_t>(farm::PayloadType::TANK_LEVEL_UPDATE),
            _,
            sizeof(farm::TankLevelUpdate),
            false))
        .WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut.broadcast_tank_level(0, 750, false, false), ESP_OK);
}
