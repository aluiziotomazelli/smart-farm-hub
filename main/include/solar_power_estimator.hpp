#pragma once

#include <algorithm>
#include <climits>
#include <cstdint>

#include "farm_protocol_types.hpp"
#include "hub_config.hpp"

namespace hub::solar {

struct SolarSystemConfig
{
    uint16_t installed_capacity_w; ///< Nominal power at STC (1000 W/m²) in Watts
    float temp_coeff_per_c;         ///< Pmax temperature coefficient (/°C). Typical: -0.004
    float system_efficiency;        ///< Overall system efficiency (0.0–1.0). Typical: 0.85
    float ref_cell_isc_ma;          ///< Reference cell Isc at STC (mA). For cross-check/calibration.
    float stc_temp_c;               ///< STC reference temperature. Default: 25.0°C

    static SolarSystemConfig from_hub_config()
    {
        return {
            .installed_capacity_w = config::SOLAR_INSTALLED_CAPACITY_W,
            .temp_coeff_per_c = config::SOLAR_TEMP_COEFF_PER_C,
            .system_efficiency = config::SOLAR_SYSTEM_EFFICIENCY,
            .ref_cell_isc_ma = config::SOLAR_REF_CELL_ISC_STC_MA,
            .stc_temp_c = 25.0f,
        };
    }
};

struct SolarPowerEstimate
{
    uint16_t power_w_instant; ///< Estimated AC power output of the solar installation (W)
    uint16_t irradiance_wm2;  ///< Irradiance used for calculation (W/m²)
};

/**
 * @brief Estimate solar installation power from sensor raw telemetry.
 *
 * Formula (IEC 61853 simplified):
 *   P = P_stc × (irradiance / 1000) × [1 + Kp × (T_panel - T_stc)] × η_system
 *
 * If panel_temp_c == INT16_MIN (no sensor), temperature correction is skipped.
 * Result is clamped to [0, UINT16_MAX].
 *
 * @note isc_current_ma is carried in report for dual-estimation validation / future cross-check.
 */
inline SolarPowerEstimate estimate(
    const farm::SolarSensorReport& report,
    const SolarSystemConfig& cfg = SolarSystemConfig::from_hub_config())
{
    // Night mode or zero irradiance -> no generation
    if (report.is_night_mode || report.irradiance_wm2 == 0) {
        return {0, 0};
    }

    // Base: P_stc scaled by irradiance
    float power = static_cast<float>(cfg.installed_capacity_w) *
                  (static_cast<float>(report.irradiance_wm2) / 1000.0f);

    // Temperature derating (only if sensor present and valid)
    if (report.panel_temp_c != INT16_MIN) {
        float temp_c = static_cast<float>(report.panel_temp_c) / 10.0f;
        power *= (1.0f + cfg.temp_coeff_per_c * (temp_c - cfg.stc_temp_c));
    }

    // System efficiency (wiring, inverter losses, etc.)
    power *= cfg.system_efficiency;

    // Clamp to valid uint16_t range
    if (power < 0.0f) {
        power = 0.0f;
    }
    if (power > static_cast<float>(UINT16_MAX)) {
        power = static_cast<float>(UINT16_MAX);
    }

    return {
        .power_w_instant = static_cast<uint16_t>(power),
        .irradiance_wm2 = report.irradiance_wm2,
    };
}

} // namespace hub::solar
