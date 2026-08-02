# Changelog - Smart Farm Hub

All notable changes to the `smart-farm-hub` firmware project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-07-21

### Added
- **Display Subsystem (`DisplayManager`)**: Autonomous component implementing `IDisplayManager` interface (`include/interfaces/i_display_manager.hpp`) with static allocation (`std::optional`) for OLED hardware driver and framebuffer context.
- **I2C Master HAL (`I2cHAL`)**: Added 1:1 I2C master driver wrapper and mock (`II2cHAL` / `MockI2cHAL`) in `idf_hals` component for host unit testing.
- **Documentation**: Added [DESIGN.md](DESIGN.md) system design document and [docs/display_decoupling_walkthrough.md](docs/display_decoupling_walkthrough.md) refactoring walkthrough.

### Changed
- **Refactored `HubApp`**: Fully decoupled `HubApp` from display rendering and I2C bus initialization. `HubApp` receives user commands via `app_cmd_queue` and updates `g_system_state`.
- **`main.cpp` Initialization**: Explicit initialization of `g_state_mutex`, `hal_i2c`, `DisplayManager`, and `HubApp`.
