# Multi-Language Support (i18n) — Walkthrough

## Summary of Accomplishments

We implemented **on-the-fly multi-language support** (`EN_US` and `PT_BR`) for the `smart-farm-hub` project. The user can switch languages dynamically via the newly added **`SETTINGS_SCREEN`** using the encoder control, and the preference is persisted across reboots directly in NVS.

---

## 1. Architecture & Design Principles

- **Zero Dynamic RAM Allocation**: String tables for `EN_US` and `PT_BR` are `constexpr` arrays stored entirely in Flash memory (`PROGMEM`).
- **SOLID Compliance**:
  - **SRP (Single Responsibility Principle)**: `I18n` class handles string lookups; `HubNvs` manages persistence.
  - **OCP (Open-Closed Principle)**: Adding new languages or strings only requires extending `StrId` / `Language` enums and string tables.
  - **DIP (Dependency Inversion Principle)**: Standardized `StrId` enums decouple UI rendering code from raw string literals.
- **NVS Integration**: Preserved existing storage system by adding `uint8_t language = 0` to `HubStats` struct (bumped struct `VERSION` to `2`).

---

## 2. Key Code Changes

### A. New i18n Subsystem (`main/include/i18n/`)
- [`language.hpp`](main/include/i18n/language.hpp): Defines `Language` enum (`EN_US`, `PT_BR`) and `StrId` string keys.
- [`strings_en.hpp`](main/include/i18n/strings_en.hpp): English string table (`"FARM HUB"`, `"WATER-TANK"`, `"SETTINGS"`, etc.).
- [`strings_pt.hpp`](main/include/i18n/strings_pt.hpp): Portuguese string table using ASCII-only terms (`"CENTRAL"`, `"CAIXA AGUA"`, `"CONFIG"`, etc.).
- [`i18n.hpp`](main/include/i18n/i18n.hpp): Static `I18n::get(StrId)` lookup class.

### B. User Preferences & Storage
- [`hub_stats.hpp`](main/include/hub_stats.hpp): Updated `HubStats` struct with `language` field and updated `operator==` comparison.
- [`hub_app.cpp`](main/src/hub_app.cpp):
  - On startup: Restores language preference from NVS into `I18n::set_language()`.
  - On language change event: Persists new `stats_.language` setting to NVS automatically.

### C. UI & Navigation
- [`ui_controller.hpp`](main/include/ui_controller.hpp): Added `ScreenMode::SETTINGS_SCREEN` and `render_settings_screen()`.
- [`ui_controller.cpp`](main/src/ui_controller.cpp):
  - Added `SETTINGS_SCREEN` to navigation sequence (`STATS_SCREEN` ➔ `SETTINGS_SCREEN` ➔ `MAIN_SCREEN`).
  - Added `CONFIRM` click event handler on `SETTINGS_SCREEN` to toggle language.
  - Replaced all hardcoded screen titles and menu items with `I18n::get(StrId::...)`.

---

## 3. Verification Results

### ESP-IDF Firmware Build
- Build target: `esp32s3`
- Firmware status: **`SUCCESS`** (`hub.bin` created cleanly).

### Host GoogleTest Suite (`display_gfx`)
- Tests executed: **15 / 15 PASSED** (0 failures).

---

## 4. UI Comparison Example

| Screen | English (`EN_US`) | Portuguese (`PT_BR`) |
| :--- | :--- | :--- |
| **Main Screen Header** | `FARM HUB` | `CENTRAL` |
| **Water Tank Header** | `WATER-TANK` | `CAIXA AGUA` |
| **Solar Screen Header** | `SOLAR GENERATION` | `GERACAO SOLAR` |
| **Settings Screen Header** | `SETTINGS` | `CONFIG` |
| **Submenu OTA Option** | `1. Start OTA` | `1. Iniciar OTA` |
