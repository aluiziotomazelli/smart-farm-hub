#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @interface IDisplayManager
 * @brief Interface for display subsystem manager.
 */
class IDisplayManager
{
public:
    virtual ~IDisplayManager() = default;

    /**
     * @brief Initialize display hardware (I2C bus, SSD1306 driver, framebuffer context).
     * @return ESP_OK on success, error code otherwise.
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Start the display rendering task.
     * @return ESP_OK on success, error code otherwise.
     */
    virtual esp_err_t start() = 0;

    /**
     * @brief Get the FreeRTOS task handle for the display task.
     * @return TaskHandle_t handle.
     */
    virtual TaskHandle_t get_task_handle() const = 0;
};
