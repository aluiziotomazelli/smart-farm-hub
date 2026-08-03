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
        UiInputs[UiInputManager Task : P4]
        DisplayMgr[DisplayManager Task : P2]
        UIController[UIController]
        SSD1306[HalDisplaySsd1306]
    end

    subgraph ESP-NOW & Dispatcher Subsystem
        EspNowMgr[EspNowManager Driver : P5]
        RxQueue[rx_queue_ : Depth 30]
        Dispatcher[MessageDispatcher Task : P4]
        TankH[WaterTankHandler]
        OtaH[OtaStatusHandler]
    end

    subgraph Hub Core Application
        HubApp[HubApp Task / Loop : P1]
        SysState[SystemState / g_state_mutex]
        WiFiMgr[WiFiManager]
        TimeMgr[TimeManager]
        OtaMgr[OtaManager]
    end

    UiInputs -->|UiEvent| UIQueue[ui_event_queue]
    UIQueue --> DisplayMgr
    DisplayMgr --> UIController
    UIController -->|AppCommand| CmdQueue[app_cmd_queue]
    CmdQueue --> HubApp

    EspNowMgr -->|AppMessage| RxQueue
    RxQueue --> Dispatcher
    Dispatcher -->|WATER_LEVEL_REPORT| TankH
    Dispatcher -->|OTA_STATUS_REPORT| OtaH

    TankH -->|Updates State under Mutex| SysState
    TankH -->|Confirms ACK / Dispatches Cmd| EspNowMgr
    OtaH -->|Confirms ACK| EspNowMgr

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

### 2.4 Message Dispatcher & Payload Handlers (`MessageDispatcher`)
- **Autonomous Task**: Runs on a dedicated FreeRTOS task at **Priority 4** (`hub::tasks::DISPATCHER_PRIORITY`), consuming `espnow::AppMessage` objects from `rx_queue_` with zero spin-polling latency (`portMAX_DELAY`).
- **High-Frequency Telemetry Isolation**: Isolates message dispatching from blocking application operations (e.g. Wi-Fi reconnection, SNTP sync, NVS commits), ensuring `rx_queue_` never overflows even under high-frequency solar telemetry (10–20 Hz).
- **Modular Handlers (`IPayloadHandler`)**: Payload routing is handled via a type-safe handler registry:
  - `WaterTankHandler`: Updates tank permille, distance, battery, and RSSI fields in `SystemState`, dispatches armed pending commands (e.g., `SYNC_TIME`), and sends ACKs.
  - `OtaStatusHandler`: Logs remote node firmware update results and sends ACKs.
- **Dependency Injection**: Domain handlers receive `SystemState&`, `g_state_mutex`, and service references via constructor injection, encapsulating thread-safe state updates.

### 2.5 Centralized Task Configuration (`hub_tasks.hpp`)
All FreeRTOS tasks across the Hub ecosystem have their priorities, stack sizes, and core affinity centrally declared in [`main/include/hub_tasks.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-hub/main/include/hub_tasks.hpp):

| Task Name | Priority | Stack Size | Description |
| :--- | :--- | :--- | :--- |
| **`espnow_rx` / `espnow_tx`** | 5 (Highest) | 4.0 KB / 3.5 KB | ESP-NOW transport driver packet handling |
| **`msg_dispatcher`** | 4 | 3.5 KB | Real-time telemetry dispatching & handler routing |
| **`ui_input`** | 4 | 3.0 KB | Rotary encoder & push button event polling |
| **`solar_arbitrator`** | 3 | 3.0 KB | `LoadDecisionEngine` real-time power balancing |
| **`display_mgr`** | 2 | 4.0 KB | OLED status rendering & UI navigation |
| **`hub_app` / `main`** | 1 (Background) | Main Thread | Application lifecycle, Wi-Fi, SNTP, NVS, & OTA |

### 2.6 Central Application (`HubApp`)
- Coordinates Wi-Fi connectivity, SNTP time sync, NVS persistence, and OTA updates.
- Fully hardware-agnostic regarding display and transport implementation; processes user actions via `app_cmd_queue` and delegates message handling entirely to `MessageDispatcher`.

---

## 3. Off-Grid Solar & Load Arbitration Architecture

### 3.1 Solar Power Balancing Constraint
The Hub operates in an off-grid solar environment without large energy buffers, enforcing instantaneous power equilibrium:

$$\text{Total Load Consumption (W)} \le \text{Instantaneous Solar Generation (W)} - \text{Safety Margin (W)}$$

### 3.2 High-Frequency Monitoring & Control Tasks
- **Solar Generation Monitoring**: High-frequency ESP-NOW telemetry from solar sensor nodes.
- **Load Control Arbitration**: `LoadDecisionEngine` evaluates available power headroom in real time to switch loads (water pump relays / contactors, refrigeration) on or off according to priority rules.
- **Hardware Interlocking**: Physical and software interlocking on water pump contactors (Solar Contactor vs Grid Contactor) to guarantee mutual exclusion.

---

## 4. Time Synchronization Architecture

- **SNTP Synchronization**: `TimeManager` connects to external NTP servers (`pool.ntp.org`) over Wi-Fi, configuring POSIX timezone `<-04>4` (UTC-4).
- **Edge Node Time Broadcast**: `WaterTankHandler` packages system epoch timestamps into `farm::TimeSyncCommand` payloads and broadcasts them over ESP-NOW to sync deep-sleep edge nodes (e.g. Water Tank node).

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
