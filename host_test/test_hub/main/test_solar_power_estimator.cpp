// host_test/test_hub/main/test_solar_power_estimator.cpp
#include <gtest/gtest.h>

#include "solar_power_estimator.hpp"

using namespace hub::solar;

TEST(SolarPowerEstimatorTest, STC_Condition_ComputesExpectedNominalPower)
{
    farm::SolarSensorReport report{};
    report.irradiance_wm2 = 1000;
    report.panel_temp_c = 250; // 25.0 °C
    report.is_night_mode = false;

    // 2640W capacity * (1000/1000) * (1 + 0) * 0.85 efficiency = 2244 W
    auto est = estimate(report);
    EXPECT_EQ(est.power_w_instant, 2244);
    EXPECT_EQ(est.irradiance_wm2, 1000);
}

TEST(SolarPowerEstimatorTest, HalfIrradiance_ComputesHalfPower)
{
    farm::SolarSensorReport report{};
    report.irradiance_wm2 = 500;
    report.panel_temp_c = 250; // 25.0 °C
    report.is_night_mode = false;

    // 2640W capacity * (500/1000) * (1 + 0) * 0.85 = 1122 W
    auto est = estimate(report);
    EXPECT_EQ(est.power_w_instant, 1122);
    EXPECT_EQ(est.irradiance_wm2, 500);
}

TEST(SolarPowerEstimatorTest, HighTemperature_AppliesThermalDerating)
{
    farm::SolarSensorReport report{};
    report.irradiance_wm2 = 1000;
    report.panel_temp_c = 500; // 50.0 °C (ΔT = +25°C, -0.4%/°C = -10% derating)
    report.is_night_mode = false;

    // 2640W * 1.0 * (1 - 0.004 * 25) * 0.85 = 2640 * 0.90 * 0.85 = 2019.6 -> 2019 W
    auto est = estimate(report);
    EXPECT_EQ(est.power_w_instant, 2019);
}

TEST(SolarPowerEstimatorTest, MissingTemperatureSensor_SkipsThermalCorrection)
{
    farm::SolarSensorReport report{};
    report.irradiance_wm2 = 1000;
    report.panel_temp_c = INT16_MIN; // Sensor absent
    report.is_night_mode = false;

    // 2640W * 1.0 * 1.0 * 0.85 = 2244 W
    auto est = estimate(report);
    EXPECT_EQ(est.power_w_instant, 2244);
}

TEST(SolarPowerEstimatorTest, NightMode_ReturnsZeroPower)
{
    farm::SolarSensorReport report{};
    report.irradiance_wm2 = 800; // Even with non-zero irradiance
    report.panel_temp_c = 200;
    report.is_night_mode = true;

    auto est = estimate(report);
    EXPECT_EQ(est.power_w_instant, 0);
    EXPECT_EQ(est.irradiance_wm2, 0);
}

TEST(SolarPowerEstimatorTest, ZeroIrradiance_ReturnsZeroPower)
{
    farm::SolarSensorReport report{};
    report.irradiance_wm2 = 0;
    report.panel_temp_c = 250;
    report.is_night_mode = false;

    auto est = estimate(report);
    EXPECT_EQ(est.power_w_instant, 0);
    EXPECT_EQ(est.irradiance_wm2, 0);
}
