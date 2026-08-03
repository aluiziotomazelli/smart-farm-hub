// main/src/ui_input_manager.cpp
#include <vector>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "interfaces/i_button.hpp"
#include "interfaces/i_rotary_encoder.hpp"
#include "interfaces/i_hal_freertos.hpp"

#include "ui_events.hpp"
#include "ui_input_manager.hpp"

static const char* TAG = "UiInputManager";

UiInputManager::UiInputManager(
    ui_inputs::IRotaryEncoder& encoder,
    ui_inputs::IButton& encoder_push,
    ui_inputs::IButton& boot_button,
    QueueHandle_t ui_event_queue,
    idf_hals::IHalFreertos& rtos,
    const UiInputManagerConfig& config)
    : encoder_(encoder)
    , encoder_push_(encoder_push)
    , boot_button_(boot_button)
    , ui_event_queue_(ui_event_queue)
    , rtos_(rtos)
    , config_(config)
{
}

esp_err_t UiInputManager::init()
{
    esp_err_t ret = encoder_.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init encoder: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = encoder_push_.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init encoder push button: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = boot_button_.init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init boot button: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "UiInputManager initialized");
    return ESP_OK;
}

esp_err_t UiInputManager::start()
{
    running_ = true;
    BaseType_t res = rtos_.task_create(
        task_fn,
        "ui_input_mgr",
        config_.task_stack_size,
        this,
        config_.task_priority,
        &task_handle_);

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ui_input_mgr task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UiInputManager task started");
    return ESP_OK;
}

esp_err_t UiInputManager::stop()
{
    if (!running_) {
        return ESP_OK;
    }

    running_ = false;
    if (task_handle_ != nullptr) {
        rtos_.task_delete(task_handle_);
        task_handle_ = nullptr;
    }
    return ESP_OK;
}

TaskHandle_t UiInputManager::get_task_handle() const
{
    return task_handle_;
}

void UiInputManager::task_fn(void* arg)
{
    auto* self = static_cast<UiInputManager*>(arg);
    self->poll_loop();
}

void UiInputManager::poll_loop()
{
    while (running_) {
        encoder_.update();
        int32_t steps = encoder_.get_steps();
        if (steps > 0) {
            UiEvent ev{UiEventType::NAV_NEXT, steps};
            rtos_.queue_send(ui_event_queue_, &ev, 0);
        } else if (steps < 0) {
            UiEvent ev{UiEventType::NAV_PREV, -steps};
            rtos_.queue_send(ui_event_queue_, &ev, 0);
        }

        encoder_push_.update();
        auto push_click = encoder_push_.get_last_click();
        if (push_click != ui_inputs::ButtonClickType::NONE_CLICK) {
            for (const auto& mapping : config_.encoder_push_mappings) {
                if (mapping.click_type == push_click) {
                    UiEvent ev{mapping.event_type, 0};
                    rtos_.queue_send(ui_event_queue_, &ev, 0);
                    break;
                }
            }
        }

        boot_button_.update();
        auto boot_click = boot_button_.get_last_click();
        if (boot_click != ui_inputs::ButtonClickType::NONE_CLICK) {
            for (const auto& mapping : config_.boot_button_mappings) {
                if (mapping.click_type == boot_click) {
                    UiEvent ev{mapping.event_type, 0};
                    rtos_.queue_send(ui_event_queue_, &ev, 0);
                    break;
                }
            }
        }

        rtos_.task_delay(pdMS_TO_TICKS(config_.poll_interval_ms));
    }
}
