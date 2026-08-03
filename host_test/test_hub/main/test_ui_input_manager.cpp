// host_test/test_hub/main/test_ui_input_manager.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_hal_freertos.hpp"
#include "mock_i_button.hpp"
#include "mock_i_rotary_encoder.hpp"
#include "ui_events.hpp"
#include "ui_input_manager.hpp"

using ::testing::_;
using ::testing::Return;

class UiInputManagerTest : public ::testing::Test
{
protected:
    ui_inputs::MockIRotaryEncoder mock_encoder_;
    ui_inputs::MockIButton mock_encoder_push_;
    ui_inputs::MockIButton mock_boot_button_;
    idf_hals::MockHalFreertos mock_freertos_;
    QueueHandle_t dummy_queue_ = reinterpret_cast<QueueHandle_t>(0x4321);

    std::unique_ptr<UiInputManager> create_sut(const UiInputManagerConfig& config = {})
    {
        return std::make_unique<UiInputManager>(
            mock_encoder_, mock_encoder_push_, mock_boot_button_, dummy_queue_, mock_freertos_, config);
    }
};

TEST_F(UiInputManagerTest, Init_AllHardwareInitsSucceed_ReturnsOk)
{
    EXPECT_CALL(mock_encoder_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_encoder_push_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_boot_button_, init()).WillOnce(Return(ESP_OK));

    auto sut = create_sut();
    EXPECT_EQ(sut->init(), ESP_OK);
}

TEST_F(UiInputManagerTest, Init_EncoderFails_ReturnsError)
{
    EXPECT_CALL(mock_encoder_, init()).WillOnce(Return(ESP_ERR_INVALID_STATE));

    auto sut = create_sut();
    EXPECT_EQ(sut->init(), ESP_ERR_INVALID_STATE);
}

TEST_F(UiInputManagerTest, Init_EncoderPushFails_ReturnsError)
{
    EXPECT_CALL(mock_encoder_, init()).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_encoder_push_, init()).WillOnce(Return(ESP_FAIL));

    auto sut = create_sut();
    EXPECT_EQ(sut->init(), ESP_FAIL);
}

TEST_F(UiInputManagerTest, StartTask_Succeeds)
{
    TaskHandle_t dummy_task = reinterpret_cast<TaskHandle_t>(0x7777);
    EXPECT_CALL(mock_freertos_, task_create(_, _, _, _, _, _))
        .WillOnce(::testing::DoAll(::testing::SetArgPointee<5>(dummy_task), Return(pdPASS)));

    auto sut = create_sut();
    EXPECT_EQ(sut->start(), ESP_OK);
    EXPECT_EQ(sut->get_task_handle(), dummy_task);
}

TEST_F(UiInputManagerTest, PollLoop_EncoderRotationNext_PostsNavNextEvent)
{
    auto sut = create_sut();

    EXPECT_CALL(mock_encoder_, update()).Times(1);
    EXPECT_CALL(mock_encoder_, get_steps()).WillOnce(Return(2));

    EXPECT_CALL(mock_encoder_push_, update()).Times(1);
    EXPECT_CALL(mock_encoder_push_, get_last_click()).WillOnce(Return(ui_inputs::ButtonClickType::NONE_CLICK));

    EXPECT_CALL(mock_boot_button_, update()).Times(1);
    EXPECT_CALL(mock_boot_button_, get_last_click()).WillOnce(Return(ui_inputs::ButtonClickType::NONE_CLICK));

    EXPECT_CALL(mock_freertos_, queue_send(dummy_queue_, _, 0))
        .WillOnce(::testing::Invoke([](QueueHandle_t, const void* data, TickType_t) {
            const auto* ev = reinterpret_cast<const UiEvent*>(data);
            EXPECT_EQ(ev->type, UiEventType::NAV_NEXT);
            EXPECT_EQ(ev->value, 2);
            return pdTRUE;
        }));

    EXPECT_CALL(mock_freertos_, task_delay(_))
        .WillOnce(::testing::Invoke([&sut](TickType_t) {
            sut->stop();
        }));

    TaskHandle_t dummy_task = reinterpret_cast<TaskHandle_t>(0x7777);
    EXPECT_CALL(mock_freertos_, task_delete(dummy_task));
    EXPECT_CALL(mock_freertos_, task_create(_, _, _, _, _, _))
        .WillOnce(::testing::Invoke([&](TaskFunction_t code, const char*, configSTACK_DEPTH_TYPE, void* param, UBaseType_t, TaskHandle_t* handle) {
            if (handle) *handle = dummy_task;
            code(param);
            return pdPASS;
        }));

    EXPECT_EQ(sut->start(), ESP_OK);
}

TEST_F(UiInputManagerTest, PollLoop_ButtonClick_PostsConfirmEvent)
{
    auto sut = create_sut();

    EXPECT_CALL(mock_encoder_, update()).Times(1);
    EXPECT_CALL(mock_encoder_, get_steps()).WillOnce(Return(0));

    EXPECT_CALL(mock_encoder_push_, update()).Times(1);
    EXPECT_CALL(mock_encoder_push_, get_last_click()).WillOnce(Return(ui_inputs::ButtonClickType::CLICK));

    EXPECT_CALL(mock_boot_button_, update()).Times(1);
    EXPECT_CALL(mock_boot_button_, get_last_click()).WillOnce(Return(ui_inputs::ButtonClickType::NONE_CLICK));

    EXPECT_CALL(mock_freertos_, queue_send(dummy_queue_, _, 0))
        .WillOnce(::testing::Invoke([](QueueHandle_t, const void* data, TickType_t) {
            const auto* ev = reinterpret_cast<const UiEvent*>(data);
            EXPECT_EQ(ev->type, UiEventType::CONFIRM);
            EXPECT_EQ(ev->value, 0);
            return pdTRUE;
        }));

    EXPECT_CALL(mock_freertos_, task_delay(_))
        .WillOnce(::testing::Invoke([&sut](TickType_t) {
            sut->stop();
        }));

    TaskHandle_t dummy_task = reinterpret_cast<TaskHandle_t>(0x7777);
    EXPECT_CALL(mock_freertos_, task_delete(dummy_task));
    EXPECT_CALL(mock_freertos_, task_create(_, _, _, _, _, _))
        .WillOnce(::testing::Invoke([&](TaskFunction_t code, const char*, configSTACK_DEPTH_TYPE, void* param, UBaseType_t, TaskHandle_t* handle) {
            if (handle) *handle = dummy_task;
            code(param);
            return pdPASS;
        }));

    EXPECT_EQ(sut->start(), ESP_OK);
}
