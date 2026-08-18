# Changelog - Smart Farm Hub

All notable changes to the `smart-farm-hub` firmware project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-08-18

### Changed
- Refactored `HubNvs` to inherit from the generic `AppStorage<HubStats, Magic, Version>` CRTP base class in `smart-farm-common`, eliminating local NVS boilerplate and implementation files.
- Decoupled domain struct `HubStats` from storage metadata (`magic`, `version`, `crc`), wrapping it automatically with the new `StorageEnvelope` pattern.
- Migrated `CoreStorage` usage in `HubApp` to pure `CoreData` and separated `process_boot_reasons()` from storage initialization.
- Simplified `init_hub_storage()` and `init_core_storage()` logic utilizing `init_app_data()` / `init()` with automatic fallback to defaults.
- Updated RTC static memory allocation in `main.cpp` using `HubStorage`.
- Bumped firmware version to `0.2.0`.

## [0.1.0] - 2026-07-21

### Added
- **Message Dispatcher Subsystem (`MessageDispatcher`)**: Autonomous task running at Priority 4 that reads ESP-NOW `AppMessage` objects from `rx_queue_` with zero spin-polling latency (`portMAX_DELAY`) and routes them via an `IPayloadHandler` registry.
- **Centralized FreeRTOS Task Configuration ([`hub_tasks.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/main/include/hub_tasks.hpp))**: Centralized header defining task priorities and stack sizes across all Hub subsystems (ESP-NOW driver, MessageDispatcher, UiInputManager, LoadDecisionEngine, DisplayManager).
- **Modular Payload Handlers (`IPayloadHandler`)**: Created `WaterTankHandler` (`WATER_LEVEL_REPORT`) and `OtaStatusHandler` (`OTA_STATUS_REPORT`) encapsulating `g_system_state` updates and ACK responses.
- **Display Subsystem (`DisplayManager`)**: Autonomous component implementing `IDisplayManager` interface (`include/interfaces/i_display_manager.hpp`) with static allocation (`std::optional`) for OLED hardware driver and framebuffer context.
- **I2C Master HAL (`I2cHAL`)**: Added 1:1 I2C master driver wrapper and mock (`II2cHAL` / `MockI2cHAL`) in `idf_hals` component for host unit testing.
- **Documentation**: Added [DESIGN.md](DESIGN.md) system design document and [docs/display_decoupling_walkthrough.md](docs/display_decoupling_walkthrough.md) refactoring walkthrough.

### Changed
- **Refactored `HubApp`**: Fully decoupled `HubApp` from ESP-NOW message receiving/switching loops. `HubApp` now focuses on handling `AppCommand` queue and top-level application lifecycle.
- **`main.cpp` Initialization**: Explicit initialization of `g_state_mutex`, `hal_i2c`, `DisplayManager`, `MessageDispatcher`, payload handlers, and `HubApp`.
