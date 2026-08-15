#pragma once

#include <cstdint>

namespace hub::config {

// ── Solar System Parameters ──────────────────────────────────────
/// Installed solar capacity at STC (1000 W/m²) in Watts.
/// 8 panels × 330W = 2640W (8S configuration, polycristalline).
/// Source: docs/photovoltaic_panels_data.md
static constexpr uint16_t SOLAR_INSTALLED_CAPACITY_W = 2640;

/// Pmax temperature coefficient per °C.
/// Polycristalline panels: typical -0.40%/°C = -0.004 /°C.
static constexpr float SOLAR_TEMP_COEFF_PER_C = -0.004f;

/// Overall system efficiency factor (inverter + wiring losses).
/// Conservative estimate: 0.85 (85%). Calibrate in field.
static constexpr float SOLAR_SYSTEM_EFFICIENCY = 0.85f;

/// Reference sensor cell Isc at STC (mA).
/// Sensor panel Isc = 0.60A = 600mA. Source: docs/photovoltaic_panels_data.md
/// Spec: 600mA @ 1000 W/m², 25°C (STC).
static constexpr float SOLAR_REF_CELL_ISC_STC_MA = 600.0f;

// ── Load Control Thresholds (for future Load Control Task) ───────
static constexpr uint16_t LOAD_SAFETY_MARGIN_W = 50;  ///< Below: shed loads
static constexpr uint16_t LOAD_RESUME_MARGIN_W = 150; ///< Above: resume loads

} // namespace hub::config
