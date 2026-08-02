// main/include/ui_input_manager.hpp
#pragma once

#include <cstdint>
#include <vector>

#include "esp_err.h"

#include "interfaces/i_button.hpp"
#include "interfaces/i_rotary_encoder.hpp"
#include "interfaces/i_hal_freertos.hpp"

#include "ui_events.hpp"

struct ButtonEventMapping {
    ui_inputs::ButtonClickType click_type;
    UiEventType event_type;
};

struct UiInputManagerConfig {
    std::vector<ButtonEventMapping> encoder_push_mappings{
        {ui_inputs::ButtonClickType::CLICK, UiEventType::CONFIRM},
        {ui_inputs::ButtonClickType::LONG_CLICK, UiEventType::BACK}
    };
    std::vector<ButtonEventMapping> boot_button_mappings{
        {ui_inputs::ButtonClickType::CLICK, UiEventType::BOOT_CLICK}
    };
    uint32_t poll_interval_ms{10};
    uint32_t task_stack_size{3072};
    UBaseType_t task_priority{5};
};

class UiInputManager {
public:
    UiInputManager(
        ui_inputs::IRotaryEncoder& encoder,
        ui_inputs::IButton& encoder_push,
        ui_inputs::IButton& boot_button,
        QueueHandle_t ui_event_queue,
        idf_hals::IHalFreertos& rtos,
        const UiInputManagerConfig& config = {});

    esp_err_t init();
    esp_err_t start();
    TaskHandle_t get_task_handle() const;

private:
    static void task_fn(void* arg);
    void poll_loop();

    ui_inputs::IRotaryEncoder& encoder_;
    ui_inputs::IButton& encoder_push_;
    ui_inputs::IButton& boot_button_;
    QueueHandle_t ui_event_queue_;
    idf_hals::IHalFreertos& rtos_;
    UiInputManagerConfig config_;
    TaskHandle_t task_handle_{nullptr};
};
