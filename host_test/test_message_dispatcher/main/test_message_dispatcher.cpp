// host_test/test_message_dispatcher/main/test_message_dispatcher.cpp
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "farm_protocol_types.hpp"
#include "interfaces/i_payload_handler.hpp"
#include "message_dispatcher.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::Return;

class MockPayloadHandler : public hub::IPayloadHandler
{
public:
    MOCK_METHOD(void, handle_payload, (const espnow::AppMessage& msg), (override));
};

class MessageDispatcherTest : public ::testing::Test
{
protected:
    idf_hals::MockHalFreertos mock_freertos_;
    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x1234);
};

TEST_F(MessageDispatcherTest, RegisterNullHandler_ReturnsInvalidArg)
{
    hub::MessageDispatcher dispatcher(dummy_queue_, mock_freertos_);
    EXPECT_EQ(dispatcher.register_handler(farm::PayloadType::WATER_LEVEL_REPORT, nullptr), ESP_ERR_INVALID_ARG);
}

TEST_F(MessageDispatcherTest, RegisterValidHandler_ReturnsOk)
{
    MockPayloadHandler mock_handler;
    hub::MessageDispatcher dispatcher(dummy_queue_, mock_freertos_);
    EXPECT_EQ(dispatcher.register_handler(farm::PayloadType::WATER_LEVEL_REPORT, &mock_handler), ESP_OK);
}

TEST_F(MessageDispatcherTest, StartWithoutQueue_ReturnsInvalidState)
{
    hub::MessageDispatcher dispatcher(nullptr, mock_freertos_);
    EXPECT_EQ(dispatcher.start(), ESP_ERR_INVALID_STATE);
}

TEST_F(MessageDispatcherTest, StartAndStopTask_Succeeds)
{
    hub::MessageDispatcher dispatcher(dummy_queue_, mock_freertos_);
    TaskHandle_t dummy_task = reinterpret_cast<TaskHandle_t>(0x5678);

    EXPECT_CALL(mock_freertos_, task_create(_, _, _, _, _, _))
        .WillOnce(::testing::DoAll(::testing::SetArgPointee<5>(dummy_task), Return(pdPASS)));

    EXPECT_EQ(dispatcher.start(), ESP_OK);
    EXPECT_EQ(dispatcher.get_task_handle(), dummy_task);

    EXPECT_CALL(mock_freertos_, task_delete(dummy_task));
    EXPECT_EQ(dispatcher.stop(), ESP_OK);
    EXPECT_EQ(dispatcher.get_task_handle(), nullptr);
}

TEST_F(MessageDispatcherTest, DispatchLoop_RoutesMessageToRegisteredHandler)
{
    hub::MessageDispatcher dispatcher(dummy_queue_, mock_freertos_);
    MockPayloadHandler mock_handler;
    dispatcher.register_handler(farm::PayloadType::WATER_LEVEL_REPORT, &mock_handler);

    espnow::AppMessage test_msg{};
    test_msg.sender_id = static_cast<uint8_t>(farm::NodeId::WATER_TANK);
    test_msg.payload_type = static_cast<uint8_t>(farm::PayloadType::WATER_LEVEL_REPORT);

    // Mock queue_receive to populate test_msg on first call, then stop dispatcher and return pdFALSE to exit loop cleanly
    EXPECT_CALL(mock_freertos_, queue_receive(dummy_queue_, _, _))
        .WillOnce(::testing::Invoke([test_msg](QueueHandle_t, void* data, TickType_t) {
            if (data) {
                *reinterpret_cast<espnow::AppMessage*>(data) = test_msg;
            }
            return pdTRUE;
        }))
        .WillRepeatedly(::testing::Invoke([&dispatcher](QueueHandle_t, void*, TickType_t) {
            dispatcher.stop();
            return pdFALSE;
        }));

    EXPECT_CALL(mock_handler, handle_payload(_))
        .WillOnce(::testing::Invoke([&](const espnow::AppMessage& msg) {
            EXPECT_EQ(msg.sender_id, static_cast<uint8_t>(farm::NodeId::WATER_TANK));
            EXPECT_EQ(msg.payload_type, static_cast<uint8_t>(farm::PayloadType::WATER_LEVEL_REPORT));
        }));

    TaskHandle_t dummy_task = reinterpret_cast<TaskHandle_t>(0x5678);
    EXPECT_CALL(mock_freertos_, task_delete(dummy_task));
    EXPECT_CALL(mock_freertos_, task_create(_, _, _, _, _, _))
        .WillOnce(::testing::Invoke([&](TaskFunction_t code, const char*, configSTACK_DEPTH_TYPE, void* param, UBaseType_t, TaskHandle_t* handle) {
            if (handle) *handle = dummy_task;
            // Run task function directly in test thread
            code(param);
            return pdPASS;
        }));

    EXPECT_EQ(dispatcher.start(), ESP_OK);
}
