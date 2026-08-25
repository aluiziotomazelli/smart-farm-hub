// host_test/test_hub/main/test_energy_monitor_and_domain_controllers.cpp
#include <gtest/gtest.h>

#include "domain_controllers.hpp"
#include "energy_monitor.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_gpio.hpp"
#include "null_energy_monitor.hpp"

using namespace idf_hals;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;

TEST(NullEnergyMonitorTest, AlwaysReturnsTrue)
{
    NullEnergyMonitor monitor;
    EXPECT_TRUE(monitor.is_solar_available());
    EXPECT_TRUE(monitor.is_grid_available());
}

TEST(EnergyMonitorTest, UninitializedFallsBackToTrue)
{
    MockHalGpio mock_gpio;
    MockHalFreertos mock_freertos;
    EnergyMonitor monitor(mock_gpio, mock_freertos);

    // Not initialized yet -> safe fallback
    EXPECT_TRUE(monitor.is_solar_available());
    EXPECT_TRUE(monitor.is_grid_available());
}

TEST(EnergyMonitorTest, InitConfiguresGpiosWithoutInterrupts)
{
    MockHalGpio mock_gpio;
    MockHalFreertos mock_freertos;
    EnergyMonitor monitor(mock_gpio, mock_freertos);

    EnergyMonitorConfig cfg{
        .solar_gpio = GPIO_NUM_10,
        .grid_gpio = GPIO_NUM_11,
        .solar_active_low = false,
        .grid_active_low = true,
        .enable_interrupts = false,
    };

    EXPECT_CALL(mock_gpio, config(_)).Times(2).WillRepeatedly(Return(ESP_OK));

    EXPECT_EQ(monitor.init(cfg), ESP_OK);
}

TEST(EnergyMonitorTest, InitConfiguresInterruptsAndAttachesHandlers)
{
    MockHalGpio mock_gpio;
    MockHalFreertos mock_freertos;
    SemaphoreHandle_t dummy_sem = reinterpret_cast<SemaphoreHandle_t>(0x1234);

    EnergyMonitor monitor(mock_gpio, mock_freertos);

    EnergyMonitorConfig cfg{
        .solar_gpio = GPIO_NUM_10,
        .grid_gpio = GPIO_NUM_11,
        .solar_active_low = false,
        .grid_active_low = true,
        .enable_interrupts = true,
        .signal_semaphore = dummy_sem,
    };

    EXPECT_CALL(mock_gpio, config(_)).Times(2).WillRepeatedly(Return(ESP_OK));
    EXPECT_CALL(mock_gpio, install_isr_service(0)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_gpio, isr_handler_add(GPIO_NUM_10, _, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_gpio, isr_handler_add(GPIO_NUM_11, _, _)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(monitor.init(cfg), ESP_OK);

    // On destruction, handlers should be removed
    EXPECT_CALL(mock_gpio, isr_handler_remove(GPIO_NUM_10)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_gpio, isr_handler_remove(GPIO_NUM_11)).WillOnce(Return(ESP_OK));
}

TEST(EnergyMonitorTest, InitFailsFastWhenInterruptsEnabledWithoutSemaphore)
{
    MockHalGpio mock_gpio;
    MockHalFreertos mock_freertos;
    EnergyMonitor monitor(mock_gpio, mock_freertos, nullptr);

    EnergyMonitorConfig cfg{
        .solar_gpio = GPIO_NUM_10,
        .grid_gpio = GPIO_NUM_11,
        .enable_interrupts = true,
        .signal_semaphore = nullptr,
    };

    EXPECT_EQ(monitor.init(cfg), ESP_ERR_INVALID_ARG);
}

TEST(EnergyMonitorTest, SetSignalSemaphoreAllowsInitWithoutConfigSemaphore)
{
    MockHalGpio mock_gpio;
    MockHalFreertos mock_freertos;
    EnergyMonitor monitor(mock_gpio, mock_freertos, nullptr);

    SemaphoreHandle_t dummy_sem = reinterpret_cast<SemaphoreHandle_t>(0x5678);
    monitor.set_signal_semaphore(dummy_sem);

    EnergyMonitorConfig cfg{
        .solar_gpio = GPIO_NUM_10,
        .grid_gpio = GPIO_NUM_11,
        .enable_interrupts = true,
        .signal_semaphore = nullptr,
    };

    EXPECT_CALL(mock_gpio, config(_)).Times(2).WillRepeatedly(Return(ESP_OK));
    EXPECT_CALL(mock_gpio, install_isr_service(0)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_gpio, isr_handler_add(GPIO_NUM_10, _, _)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_gpio, isr_handler_add(GPIO_NUM_11, _, _)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(monitor.init(cfg), ESP_OK);

    EXPECT_CALL(mock_gpio, isr_handler_remove(GPIO_NUM_10)).WillOnce(Return(ESP_OK));
    EXPECT_CALL(mock_gpio, isr_handler_remove(GPIO_NUM_11)).WillOnce(Return(ESP_OK));
}

TEST(EnergyMonitorTest, ReadsActiveHighAndActiveLowCorrectly)
{
    MockHalGpio mock_gpio;
    MockHalFreertos mock_freertos;
    EnergyMonitor monitor(mock_gpio, mock_freertos);

    EnergyMonitorConfig cfg{
        .solar_gpio = GPIO_NUM_10,
        .grid_gpio = GPIO_NUM_11,
        .solar_active_low = false, // High = present
        .grid_active_low = true,   // Low = present
    };

    EXPECT_CALL(mock_gpio, config(_)).Times(2).WillRepeatedly(Return(ESP_OK));
    ASSERT_EQ(monitor.init(cfg), ESP_OK);

    // Solar GPIO reading 1 (High) -> Solar available
    EXPECT_CALL(mock_gpio, get_level(GPIO_NUM_10)).WillOnce(Return(1));
    EXPECT_TRUE(monitor.is_solar_available());

    // Solar GPIO reading 0 (Low) -> Solar unavailable
    EXPECT_CALL(mock_gpio, get_level(GPIO_NUM_10)).WillOnce(Return(0));
    EXPECT_FALSE(monitor.is_solar_available());

    // Grid GPIO reading 0 (Low) -> Grid available (active low)
    EXPECT_CALL(mock_gpio, get_level(GPIO_NUM_11)).WillOnce(Return(0));
    EXPECT_TRUE(monitor.is_grid_available());

    // Grid GPIO reading 1 (High) -> Grid unavailable (active low)
    EXPECT_CALL(mock_gpio, get_level(GPIO_NUM_11)).WillOnce(Return(1));
    EXPECT_FALSE(monitor.is_grid_available());
}

TEST(DomainControllersTest, FridgeControllerEmitsCorrectIntent)
{
    FridgeController fridge;
    EXPECT_EQ(fridge.get_load_index(), LoadIndex::FRIDGE);

    LoadIntent intent = fridge.get_current_intent();
    EXPECT_EQ(intent.load_index, LoadIndex::FRIDGE);
    EXPECT_EQ(intent.desired_state, LoadDesiredState::ON);
    EXPECT_EQ(intent.source_preference, SourcePreference::SOLAR_PREFERRED);
    EXPECT_EQ(intent.urgency, LoadUrgency::NORMAL);
    EXPECT_EQ(intent.max_hold_duration_s, 20 * 60);
    EXPECT_EQ(intent.estimated_on_duration_s, 0); // Continuous

    fridge.set_enabled(false);
    intent = fridge.get_current_intent();
    EXPECT_EQ(intent.desired_state, LoadDesiredState::OFF);
    EXPECT_EQ(intent.urgency, LoadUrgency::SHEDDABLE);
}

TEST(DomainControllersTest, FreezerControllerEmitsCorrectIntent)
{
    FreezerController freezer;
    EXPECT_EQ(freezer.get_load_index(), LoadIndex::FREEZER);

    LoadIntent intent = freezer.get_current_intent();
    EXPECT_EQ(intent.load_index, LoadIndex::FREEZER);
    EXPECT_EQ(intent.desired_state, LoadDesiredState::ON);
    EXPECT_EQ(intent.max_hold_duration_s, 30 * 60);
    EXPECT_EQ(intent.urgency, LoadUrgency::NORMAL);
}

TEST(DomainControllersTest, RouterControllerIsCriticalAndUnsheddable)
{
    RouterController router;
    EXPECT_EQ(router.get_load_index(), LoadIndex::ROUTER);

    LoadIntent intent = router.get_current_intent();
    EXPECT_EQ(intent.urgency, LoadUrgency::CRITICAL);
    EXPECT_EQ(intent.max_hold_duration_s, 0);
}

TEST(DomainControllersTest, LightingControllerIsSheddableSolarOnly)
{
    LightingController lighting;
    EXPECT_EQ(lighting.get_load_index(), LoadIndex::LIGHTING);

    LoadIntent intent = lighting.get_current_intent();
    EXPECT_EQ(intent.urgency, LoadUrgency::SHEDDABLE);
    EXPECT_EQ(intent.source_preference, SourcePreference::SOLAR_ONLY);
}
