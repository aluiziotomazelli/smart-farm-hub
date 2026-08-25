// main/include/energy_monitor.hpp
#pragma once

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "interfaces/i_energy_monitor.hpp"
#include "interfaces/i_hal_freertos.hpp"
#include "interfaces/i_hal_gpio.hpp"

/**
 * @struct EnergyMonitorConfig
 * @brief Pin configuration and active level polarities for EnergyMonitor.
 */
struct EnergyMonitorConfig {
    gpio_num_t solar_gpio = GPIO_NUM_NC; ///< GPIO for inverter output voltage sensing
    gpio_num_t grid_gpio = GPIO_NUM_NC;  ///< GPIO for public grid voltage sensing
    bool solar_active_low = false;       ///< True if LOW level indicates voltage presence (enables pull-up)
    bool grid_active_low = false;        ///< True if LOW level indicates voltage presence (enables pull-up)
    bool enable_interrupts = false;      ///< True to attach ISR on edge changes (GPIO_INTR_ANYEDGE)
};

/**
 * @class EnergyMonitor
 * @brief Concrete GPIO-based implementation of IEnergyMonitor.
 */
class EnergyMonitor : public IEnergyMonitor
{
public:
    /**
     * @brief Constructs EnergyMonitor with injected GPIO and FreeRTOS HAL dependencies.
     * @param hal_gpio Injected GPIO HAL interface.
     * @param hal_freertos Injected FreeRTOS HAL interface.
     * @param signal_semaphore Optional binary semaphore to give from ISR on level changes.
     */
    EnergyMonitor(
        idf_hals::IGpioHAL& hal_gpio,
        idf_hals::IHalFreertos& hal_freertos,
        SemaphoreHandle_t signal_semaphore = nullptr);

    ~EnergyMonitor() override;

    /**
     * @brief Initializes GPIOs, configures pull resistors according to polarity, and sets interrupts.
     * @param config Hardware pin, polarity, and interrupt configuration.
     * @return ESP_OK on success, error code otherwise.
     */
    esp_err_t init(const EnergyMonitorConfig& config);

    /** @copydoc IEnergyMonitor::is_solar_available */
    bool is_solar_available() const override;

    /** @copydoc IEnergyMonitor::is_grid_available */
    bool is_grid_available() const override;

private:
    static void IRAM_ATTR gpio_isr_handler(void* arg);

    idf_hals::IGpioHAL& hal_gpio_;
    idf_hals::IHalFreertos& hal_freertos_;
    EnergyMonitorConfig config_;
    SemaphoreHandle_t signal_semaphore_ = nullptr;
    bool initialized_ = false;
};
