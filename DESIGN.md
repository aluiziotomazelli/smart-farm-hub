# Smart Farm - Central Hub Architecture (`smart-farm-hub`)

## 1. Overview

The **Smart Farm Central Hub** is the primary gateway, coordinator, and off-grid power control node in the Smart Farm ecosystem.

Its primary responsibilities include:
- **Central Telemetry & Node Management**: Collecting sensor reports and telemetry from edge nodes (e.g., Water Tank node, Solar sensors) via ESP-NOW.
- **Solar Off-Grid Load Control**: Real-time power balance monitoring and load arbitration (e.g., water pump relay contactors, refrigeration loads).
- **Time Synchronization Gateway**: Synchronizing system time via Wi-Fi SNTP and broadcasting time sync packets to battery-powered edge nodes.
- **User Interface Subsystem**: Interactive visual display (SSD1306 OLED) and input navigation (rotary encoder + buttons).
- **Over-The-Air (OTA) Management**: Managing firmware updates and rollbacks over Wi-Fi.

---

## 2. System Subsystems & Component Architecture

```mermaid
flowchart TD
    subgraph Hardware & HAL Abstraction (idf_hals)
        I2CHAL[HalI2cMaster]
        FreeRTOSHAL[HalFreertos]
        GpioHAL[GpioHAL]
        PcntHAL[HalPcnt]
    end

    subgraph UI & Display Subsystem
        UiInputs[UiInputManager Task]
        DisplayMgr[DisplayManager Task]
        UIController[UIController]
        SSD1306[HalDisplaySsd1306]
    end

    subgraph Hub Core Application
        HubApp[HubApp Core Loop]
        SysState[SystemState / g_state_mutex]
        WiFiMgr[WiFiManager]
        EspNowMgr[EspNowManager]
        TimeMgr[TimeManager]
        OtaMgr[OtaManager]
    end

    UiInputs -->|UiEvent| UIQueue[ui_event_queue]
    UIQueue --> DisplayMgr
    DisplayMgr --> UIController
    UIController -->|AppCommand| CmdQueue[app_cmd_queue]
    CmdQueue --> HubApp

    HubApp -->|Updates Telemetry & Status| SysState
    SysState -->|Safe Snapshot Read| DisplayMgr
    DisplayMgr --> SSD1306
    I2CHAL --> SSD1306
```

### 2.1 Hardware Abstraction Layer (HAL)
In compliance with the project's Architectural Guidelines:
- **Strict HAL Rule**: No direct ESP-IDF or FreeRTOS C API calls are made outside `idf_hals` implementation files.
- **I2C Master HAL (`I2cHAL`)**: Wraps ESP-IDF `driver/i2c_master.h` 1:1 (`new_master_bus`, `del_master_bus`, `master_bus_add_device`, `master_transmit_receive`, etc.), allowing full host unit testing via `MockI2cHAL`.

### 2.2 Display Subsystem (`DisplayManager`)
- **Decoupled Architecture**: Fully decoupled from `HubApp`. `DisplayManager` implements `IDisplayManager` (`include/interfaces/i_display_manager.hpp`).
- **Zero Dynamic Allocation (No `new`)**: Driver (`HalDisplaySsd1306`) and graphics context (`FramebufferGraphicsContext`) are held statically as `std::optional` member variables inside `DisplayManager`.
- **Thread-Safe Snapshot Rendering**: Reads `g_system_state` using `g_state_mutex` to render status screens asynchronously without locking `HubApp`.
- **Night Mode & Auto-Sleep Policy**: Responds to UI activity timestamps and time of day to manage display power (`set_power(bool)`).

### 2.3 UI Input Subsystem (`UiInputManager`)
- Processes rotary encoder signals (PCNT HAL + Timer HAL) and push button clicks (GPIO HAL).
- Posts non-blocking `UiEvent` items (`NAV_NEXT`, `NAV_PREV`, `CONFIRM`, `BACK`) to `ui_event_queue`.

### 2.4 Central Application (`HubApp`)
- Coordinates Wi-Fi connectivity, ESP-NOW node discovery/telemetry, SNTP time sync, and NVS persistence.
- Completely hardware-agnostic regarding display hardware; receives user actions strictly as `AppCommand` structures from `app_cmd_queue`.

---

## 3. Off-Grid Solar & Load Arbitration Architecture

### 3.1 Solar Power Balancing Constraint
The Hub operates in an off-grid solar environment without large energy buffers, enforcing instantaneous power equilibrium:

$$\text{Total Load Consumption (W)} \le \text{Instantaneous Solar Generation (W)} - \text{Safety Margin (W)}$$

### 3.2 High-Frequency Monitoring & Control Tasks
- **Solar Generation Monitoring**: High-frequency ESP-NOW telemetry from solar sensor nodes.
- **Load Control Arbitration**: Real-time evaluation of available power headroom to switch loads (water pump relays / contactors, refrigeration) on or off according to priority rules.
- **Hardware Interlocking**: Physical and software interlocking on water pump contactors (Solar Contactor vs Grid Contactor) to guarantee mutual exclusion.

---

## 4. Time Synchronization Architecture

- **SNTP Synchronization**: `TimeManager` connects to external NTP servers (`pool.ntp.org`) over Wi-Fi, configuring POSIX timezone `<-04>4` (UTC-4).
- **Edge Node Time Broadcast**: `HubApp` packages system epoch timestamps into `farm::TimeSyncCommand` payloads and broadcasts them over ESP-NOW to sync deep-sleep edge nodes (e.g. Water Tank node).

---

## 5. State Management & Storage

### 5.1 SystemState Struct
Shared application state is stored in `g_system_state` and protected by `g_state_mutex`:
- **Water Tank Node**: Water level (permille), distance (cm), battery (mV), sensor status, fill state.
- **Wi-Fi & Network**: Connection state, RSSI, ESP-NOW peer counts, average link RSSI.
- **Statistics**: Messages received/sent, packet loss, RTT.
- **OTA State**: Firmware verification and update flags.

### 5.2 NVS & RTC Persistence
- `NvsCore`: System boot counts, reset reasons, crash metrics, hardware profiles.
- `HubNvs`: Hub operational statistics, node telemetry logs, pending command flags.

---

## 6. Over-The-Air (OTA) Updates

- **Self-Update Engine**: `OtaManager` polls update server over Wi-Fi, validates firmware manifests, downloads update images to secondary OTA partitions, and manages rollbacks upon session failure.
- **Edge Node OTA Relay**: Distributes firmware binaries or update triggers to downstream nodes via ESP-NOW.
