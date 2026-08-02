#pragma once

#include <optional>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "interfaces/i_display_manager.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_i2c.hpp"
#include "hal_display_ssd1306.hpp"
#include "framebuffer_graphics_context.hpp"
#include "ui_controller.hpp"

struct DisplayManagerConfig
{
    gpio_num_t sda_pin = GPIO_NUM_8;
    gpio_num_t scl_pin = GPIO_NUM_9;
    uint8_t i2c_address = 0x3C;
    uint16_t width = 128;
    uint16_t height = 64;
    uint32_t i2c_clk_speed_hz = 400000;
    Rotation rotation = Rotation::ROTATION_180;
    uint32_t task_stack_size = 4096;
    UBaseType_t task_priority = 2;
};

class DisplayManager : public IDisplayManager
{
public:
    DisplayManager(
        QueueHandle_t ui_event_queue,
        QueueHandle_t app_cmd_queue,
        idf_hals::IHalFreertos& rtos,
        idf_hals::II2cHAL& i2c_hal,
        const DisplayManagerConfig& config = {});

    ~DisplayManager() override = default;

    esp_err_t init() override;
    esp_err_t start() override;
    TaskHandle_t get_task_handle() const override;

private:
    static void task_fn(void* arg);
    void display_loop();

    QueueHandle_t ui_event_queue_;
    QueueHandle_t app_cmd_queue_;
    idf_hals::IHalFreertos& rtos_;
    idf_hals::II2cHAL& i2c_hal_;
    DisplayManagerConfig config_;

    i2c_master_bus_handle_t i2c_bus_{nullptr};
    std::optional<HalDisplaySsd1306> display_hal_;
    std::optional<FramebufferGraphicsContext> gfx_ctx_;
    TaskHandle_t task_handle_{nullptr};
};
