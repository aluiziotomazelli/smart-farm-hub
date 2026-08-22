// host_test/test_hub/main/test_load_control_handler.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "command_manager.hpp"
#include "handlers/load_control_handler.hpp"
#include "hub_nvs.hpp"
#include "mocks/mock_espnow_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_timer.hpp"
#include "mock_persistence_backend.hpp"
#include "mock_time_manager.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class LoadControlHandlerTest : public ::testing::Test
{
protected:
    SystemState state_;
    SemaphoreHandle_t dummy_mutex_ = reinterpret_cast<SemaphoreHandle_t>(0x5678);
    HubStats stats_;
    NiceMock<MockPersistenceBackend> rtc_backend_;
    NiceMock<MockPersistenceBackend> nvs_backend_;
    HubNvs hub_storage_{rtc_backend_, nvs_backend_};
    NiceMock<time_manager::MockTimeManager> mock_time_;
    NiceMock<idf_hals::MockHalFreertos> mock_freertos_;
    NiceMock<idf_hals::MockTimerHAL> mock_timer_;
    NiceMock<espnow::MockEspNowManager> mock_espnow_;

    std::unique_ptr<hub::CommandManager> cmd_mgr_;
    std::unique_ptr<hub::LoadControlHandler> handler_;

    void SetUp() override
    {
        ON_CALL(mock_freertos_, semaphore_take(dummy_mutex_, _)).WillByDefault(Return(pdTRUE));
        ON_CALL(mock_freertos_, semaphore_give(dummy_mutex_)).WillByDefault(Return(pdTRUE));
        ON_CALL(mock_timer_, get_time_us()).WillByDefault(Return(1000000));

        cmd_mgr_ = std::make_unique<hub::CommandManager>(
            mock_espnow_, stats_, hub_storage_, mock_time_, state_, dummy_mutex_, mock_freertos_);
        handler_ = std::make_unique<hub::LoadControlHandler>(
            state_, dummy_mutex_, *cmd_mgr_, mock_timer_, mock_freertos_);
    }
};

TEST_F(LoadControlHandlerTest, InvalidPayloadLength_ReturnsError)
{
    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL);
    msg.payload_len = sizeof(farm::LoadControlStatus) - 1;

    EXPECT_EQ(handler_->handle_payload(msg), espnow::AckStatus::ERROR_INVALID_DATA);
}

TEST_F(LoadControlHandlerTest, ValidPumpControlPayload_UpdatesPumpLoadState)
{
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

    EXPECT_EQ(handler_->handle_payload(msg), espnow::AckStatus::OK);

    const auto& pump = state_.load(LoadIndex::PUMP);
    EXPECT_EQ(pump.load_state, farm::LoadState::RUNNING);
    EXPECT_EQ(pump.control_mode, farm::ControlMode::AUTO);
    EXPECT_EQ(pump.active_source, farm::PowerSource::SOLAR);
    EXPECT_EQ(pump.power_w, 1500);
    EXPECT_EQ(pump.runtime_s, 120);
    EXPECT_EQ(pump.uptime_s, 3600);
    EXPECT_EQ(pump.unix_time, 1700000000000ULL);
    EXPECT_EQ(pump.last_update_ts, 1000);

    EXPECT_EQ(state_.get_node_power_profile(farm::NodeId::PUMP_CONTROL), farm::PowerProfile::ALWAYS_ON);
}

TEST_F(LoadControlHandlerTest, GenericActuatorNode_FindsOrAllocatesLoad)
{
    auto generic_node = static_cast<farm::NodeId>(0x0C);

    farm::LoadControlStatus report{};
    report.circuit_id = 1;
    report.power_profile = farm::PowerProfile::ALWAYS_ON;
    report.control_mode = farm::ControlMode::FULL_MANUAL;
    report.active_power_source = farm::PowerSource::GRID;
    report.load_state = farm::LoadState::RUNNING;
    report.power_w = 750;
    report.runtime_s = 60;
    report.uptime_s = 1800;
    report.unix_time = 1700000005000ULL;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(generic_node);
    msg.payload_len = sizeof(report);
    memcpy(msg.payload, &report, sizeof(report));

    EXPECT_EQ(handler_->handle_payload(msg), espnow::AckStatus::OK);

    auto* load = state_.find_or_allocate_load(generic_node, 1);
    ASSERT_NE(load, nullptr);
    EXPECT_EQ(load->load_state, farm::LoadState::RUNNING);
    EXPECT_EQ(load->control_mode, farm::ControlMode::FULL_MANUAL);
    EXPECT_EQ(load->active_source, farm::PowerSource::GRID);
    EXPECT_EQ(load->power_w, 750);
}

TEST_F(LoadControlHandlerTest, PostHandlePayload_ProcessesNodeWake)
{
    farm::LoadControlStatus report{};
    report.circuit_id = 0;
    report.unix_time = 1700000000000ULL;

    espnow::AppMessage msg{};
    msg.sender_id = static_cast<uint8_t>(farm::NodeId::PUMP_CONTROL);
    msg.payload_len = sizeof(report);
    memcpy(msg.payload, &report, sizeof(report));

    EXPECT_CALL(mock_time_, is_synchronized()).WillOnce(Return(false));

    handler_->post_handle_payload(msg);
    EXPECT_EQ(cmd_mgr_->get_stats().messages_received, 1);
}
