// main/src/energy_monitor.cpp
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "energy_monitor.hpp"

static const char* TAG = "EnergyMonitor";

EnergyMonitor::EnergyMonitor(idf_hals::IGpioHAL& hal_gpio, idf_hals::IHalFreertos& hal_freertos)
    : hal_gpio_(hal_gpio)
    , hal_freertos_(hal_freertos)
{
}

EnergyMonitor::~EnergyMonitor()
{
    if (initialized_ && config_.enable_interrupts) {
        if (config_.solar_gpio != GPIO_NUM_NC) {
            hal_gpio_.isr_handler_remove(config_.solar_gpio);
        }
        if (config_.grid_gpio != GPIO_NUM_NC) {
            hal_gpio_.isr_handler_remove(config_.grid_gpio);
        }
    }
}

void EnergyMonitor::set_signal_semaphore(SemaphoreHandle_t signal_semaphore)
{
    signal_semaphore_ = signal_semaphore;
}

void IRAM_ATTR EnergyMonitor::gpio_isr_handler(void* arg)
{
    auto* self = static_cast<EnergyMonitor*>(arg);
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (self->signal_semaphore_ != nullptr) {
        xSemaphoreGiveFromISR(self->signal_semaphore_, &higher_priority_task_woken);
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

esp_err_t EnergyMonitor::init(const EnergyMonitorConfig& config)
{
    config_ = config;
    if (config_.signal_semaphore != nullptr) {
        signal_semaphore_ = config_.signal_semaphore;
    }

    if (config_.enable_interrupts && signal_semaphore_ == nullptr) {
        ESP_LOGE(TAG, "enable_interrupts is true but signal_semaphore is NULL! Refusing to initialize.");
        return ESP_ERR_INVALID_ARG;
    }

    gpio_int_type_t intr_type = config_.enable_interrupts ? GPIO_INTR_ANYEDGE : GPIO_INTR_DISABLE;

    if (config_.solar_gpio != GPIO_NUM_NC) {
        gpio_config_t io_conf = {};
        io_conf.intr_type = intr_type;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << config_.solar_gpio);
        io_conf.pull_up_en = config_.solar_active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = config_.solar_active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;

        esp_err_t err = hal_gpio_.config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure solar GPIO %d: %s", config_.solar_gpio, esp_err_to_name(err));
            return err;
        }
    }

    if (config_.grid_gpio != GPIO_NUM_NC) {
        gpio_config_t io_conf = {};
        io_conf.intr_type = intr_type;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << config_.grid_gpio);
        io_conf.pull_up_en = config_.grid_active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = config_.grid_active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;

        esp_err_t err = hal_gpio_.config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure grid GPIO %d: %s", config_.grid_gpio, esp_err_to_name(err));
            return err;
        }
    }

    if (config_.enable_interrupts) {
        // Try installing service (ignore ESP_ERR_INVALID_STATE if already installed)
        esp_err_t isr_err = hal_gpio_.install_isr_service(0);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "install_isr_service returned %s", esp_err_to_name(isr_err));
        }

        if (config_.solar_gpio != GPIO_NUM_NC) {
            hal_gpio_.isr_handler_add(config_.solar_gpio, gpio_isr_handler, this);
        }
        if (config_.grid_gpio != GPIO_NUM_NC) {
            hal_gpio_.isr_handler_add(config_.grid_gpio, gpio_isr_handler, this);
        }
    }

    initialized_ = true;
    ESP_LOGI(
        TAG,
        "EnergyMonitor initialized (solar_gpio=%d [%s], grid_gpio=%d [%s], isr=%s)",
        config_.solar_gpio,
        config_.solar_active_low ? "active_low (pullup)" : "active_high (pulldown)",
        config_.grid_gpio,
        config_.grid_active_low ? "active_low (pullup)" : "active_high (pulldown)",
        config_.enable_interrupts ? "enabled" : "disabled");

    return ESP_OK;
}

bool EnergyMonitor::is_solar_available() const
{
    if (!initialized_ || config_.solar_gpio == GPIO_NUM_NC) {
        return true; // Fallback to available if unconfigured/uninitialized
    }

    int level = hal_gpio_.get_level(config_.solar_gpio);
    return config_.solar_active_low ? (level == 0) : (level != 0);
}

bool EnergyMonitor::is_grid_available() const
{
    if (!initialized_ || config_.grid_gpio == GPIO_NUM_NC) {
        return true; // Fallback to available if unconfigured/uninitialized
    }

    int level = hal_gpio_.get_level(config_.grid_gpio);
    return config_.grid_active_low ? (level == 0) : (level != 0);
}
