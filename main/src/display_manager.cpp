#include "display_manager.hpp"
#include "system_state.hpp"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

static const char* TAG = "DisplayManager";

DisplayManager::DisplayManager(
    QueueHandle_t ui_event_queue,
    QueueHandle_t app_cmd_queue,
    espnow::IEspNowManager* espnow,
    idf_hals::IHalFreertos& rtos,
    idf_hals::II2cHAL& i2c_hal,
    const DisplayManagerConfig& config)
    : ui_event_queue_(ui_event_queue)
    , app_cmd_queue_(app_cmd_queue)
    , espnow_(espnow)
    , rtos_(rtos)
    , i2c_hal_(i2c_hal)
    , config_(config)
{
}

esp_err_t DisplayManager::init()
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = config_.sda_pin;
    bus_config.scl_io_num = config_.scl_pin;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_hal_.new_master_bus(&bus_config, &i2c_bus_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C master bus via HAL: %s", esp_err_to_name(err));
        return err;
    }

    Ssd1306Config display_config;
    display_config.i2c_bus = i2c_bus_;
    display_config.i2c_address = config_.i2c_address;
    display_config.width = config_.width;
    display_config.height = config_.height;
    display_config.rst_gpio = -1;
    display_config.i2c_clk_speed_hz = config_.i2c_clk_speed_hz;

    display_hal_.emplace(display_config);
    err = display_hal_->init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HalDisplaySsd1306: %s", esp_err_to_name(err));
        return err;
    }

    gfx_ctx_.emplace(*display_hal_);
    gfx_ctx_->set_rotation(config_.rotation);
    gfx_ctx_->clear(0);
    gfx_ctx_->flush();

    ESP_LOGI(TAG, "DisplayManager initialized successfully (static allocation)");
    return ESP_OK;
}

esp_err_t DisplayManager::start()
{
    BaseType_t res = rtos_.task_create(
        task_fn,
        "display_task",
        config_.task_stack_size,
        this,
        config_.task_priority,
        &task_handle_);

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display_task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DisplayManager task started");
    return ESP_OK;
}

TaskHandle_t DisplayManager::get_task_handle() const
{
    return task_handle_;
}

void DisplayManager::task_fn(void* arg)
{
    auto* self = static_cast<DisplayManager*>(arg);
    self->display_loop();
}

void DisplayManager::display_loop()
{
    UIController ui(*gfx_ctx_, app_cmd_queue_, espnow_);

    while (true) {
        UiEvent event;
        if (ui_event_queue_ != nullptr) {
            if (rtos_.queue_receive(ui_event_queue_, &event, pdMS_TO_TICKS(500)) == pdTRUE) {
                ui.handle_event(event);
                while (rtos_.queue_receive(ui_event_queue_, &event, 0) == pdTRUE) {
                    ui.handle_event(event);
                }
            }
        }
        else {
            rtos_.task_delay(pdMS_TO_TICKS(500));
        }

        SystemState snapshot;
        if (g_state_mutex != nullptr && rtos_.semaphore_take(g_state_mutex, portMAX_DELAY) == pdTRUE) {
            snapshot = g_system_state;
            rtos_.semaphore_give(g_state_mutex);
        }

        gfx_ctx_->clear(0);
        ui.render_current_screen(snapshot);
        gfx_ctx_->flush();
    }
}
