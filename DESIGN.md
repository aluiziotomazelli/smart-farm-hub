# Smart Farm - Central Hub Architecture (`smart-farm-hub`)

## 1. Overview

The **Smart Farm Central Hub** is the primary gateway, coordinator, and off-grid power control node in the Smart Farm ecosystem.

Its primary responsibilities include:
- **Central Telemetry & Node Management**: Collecting sensor reports and telemetry from edge nodes (Water Tank, Solar Sensor, Actuator nodes) via ESP-NOW, managed via `NodeRegistry`.
- **Solar Off-Grid Load Control**: Real-time power balance monitoring, knapsack greedy power allocation, and multi-tier load arbitration executed asynchronously in `LoadControlTask`.
- **Water Tank Domain Control**: Automated policy management (`TankController`) featuring multi-stage dynamic fill targets (`TankFillTier`), pre-sunset acceleration, and manual stop cooldown protection.
- **Time Synchronization Gateway**: Synchronizing system time via Wi-Fi SNTP (`TimeManager`) and broadcasting time sync packets to battery-powered edge nodes.
- **User Interface Subsystem**: Non-blocking OLED status rendering (`DisplayManager`, `UIController`) consuming thread-safe, lock-minimized copies from `UiSnapshot`.
- **Over-The-Air (OTA) Management**: Managing firmware updates and automatic rollback upon boot failure over Wi-Fi (`OtaManager`).

---

## 2. System Subsystems & Component Architecture

```mermaid
flowchart TD
    subgraph Hardware & HAL Abstraction (idf_hals)
        I2CHAL[HalI2cMaster]
        FreeRTOSHAL[HalFreertos]
        GpioHAL[GpioHAL]
        PcntHAL[HalPcnt]
        TimerHAL[HalTimer]
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
        SolarH[SolarSensorHandler]
        LoadH[LoadControlHandler]
        OtaH[OtaStatusHandler]
    end

    subgraph Load Control & Energy Subsystem
        LCT[LoadControlTask : P3 / QueueSet]
        LCE[LoadControlEngine]
        TC[TankController]
        EnergyMon[EnergyMonitor]
        CmdMgr[CommandManager]
    end

    subgraph Central State & Core
        NodeReg[NodeRegistry]
        UiSnap[UiSnapshot]
        HubApp[HubApp Task / Loop : P1]
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
    Dispatcher -->|WATER_LEVEL_REPORT / FILL_REQUEST| TankH
    Dispatcher -->|SOLAR_SENSOR_REPORT| SolarH
    Dispatcher -->|LOAD_CONTROL_STATUS| LoadH
    Dispatcher -->|OTA_STATUS_REPORT| OtaH

    TankH -->|Updates Telemetry| UiSnap
    TankH -->|Feeds Report & Evaluates Intent| TC
    TC -->|LoadIntent| LCT

    SolarH -->|Updates Telemetry| UiSnap
    SolarH -->|SolarPowerUpdate| LCT

    LoadH -->|LoadStatusUpdate| LCT
    LoadH -->|on_pump_status_update| TC

    EnergyMon -->|Energy Availability Semaphore| LCT
    LCT -->|Arbitrates Headroom & Constraints| LCE
    LCT -->|LoadControlDecision| CmdMgr
    CmdMgr -->|LOAD_ON / LOAD_OFF / SYNC_TIME| EspNowMgr
    LCT -->|update_energy_and_loads| UiSnap

    UiSnap -->|Fast Snapshot Copy| UIController
    DisplayMgr --> SSD1306
    I2CHAL --> SSD1306
```

### 2.1 Hardware Abstraction Layer (HAL)
In compliance with the project's Architectural Guidelines:
- **Strict HAL Rule**: No direct ESP-IDF or FreeRTOS C API calls are made outside `idf_hals` implementation files.
- **Mockability**: All HALs (`I2cHAL`, `HalFreertos`, `GpioHAL`, `HalTimer`, `HalPcnt`) implement interfaces under `include/interfaces/`, enabling complete host unit testing under Linux.

### 2.2 Dedicated Node Management (`NodeRegistry`)
- **Single Source of Truth**: Tracks known edge nodes (`farm::NodeId`), firmware versions (`major.minor.patch`), and power profiles (`ALWAYS_ON`, `LOW_POWER`, `DEEP_SLEEP`).
- **Zero Dynamic Allocation**: Fixed-capacity internal storage (`std::array<farm::NodeMetadata, farm::MAX_HUB_NODES>`).
- **Liveness Delegation**: Peer liveness is tracked natively by transport layer heartbeats in `EspNowManager`.

### 2.3 Presentation Layer (`UiSnapshot` & `UIController`)
- **Lock-Minimized Telemetry Cache**: Producers (`LoadControlTask`, `WaterTankHandler`, `SolarSensorHandler`) update localized sub-structs under microsecond mutex guards.
- **Non-Blocking UI Rendering**: `DisplayManager` and `UIController` call `ui_snapshot.get()` once per frame, obtaining a read-only frozen copy (`UiSnapshotData`) with zero lock contention against high-frequency communication or arbitration loops.

### 2.4 Command Routing & Dispatching (`CommandManager`)
- Implements `ICommandManager` and `ILoadActuatorDispatcher`.
- **Immediate Dispatch**: Sends commands immediately to `ALWAYS_ON` nodes.
- **Queued FIFO Routing**: Enqueues commands for `DEEP_SLEEP` / `LOW_POWER` nodes in a zero-heap RAM queue (`etl::queue<PendingCommand, MAX_PENDING_COMMANDS>`), drained instantly when the node wakes and sends telemetry (`process_node_wake`).

### 2.5 Centralized Task Configuration (`hub_tasks.hpp`)

| Task Name | Priority | Stack Size | Description |
| :--- | :--- | :--- | :--- |
| **`espnow_rx` / `espnow_tx`** | 5 (Highest) | 4.0 KB / 3.5 KB | ESP-NOW transport driver packet handling |
| **`msg_dispatcher`** | 4 | 3.5 KB | Real-time telemetry dispatching & handler routing |
| **`ui_input`** | 4 | 3.0 KB | Rotary encoder & push button event polling |
| **`load_control_task`** | 3 | 4.0 KB | Energy arbitration, solar packing & decision dispatch |
| **`display_mgr`** | 2 | 4.0 KB | OLED status rendering & UI navigation |
| **`hub_app` / `main`** | 1 (Background) | Main Thread | Application lifecycle, Wi-Fi, SNTP, NVS, & OTA |

---

## 3. Off-Grid Solar & Load Arbitration Architecture

### 3.1 Reactive IPC Architecture (`LoadControlTask`)
Instead of spin-polling, `LoadControlTask` blocks on a FreeRTOS `QueueSet` aggregating:
1. `solar_queue`: Overwritten with latest PV generation telemetry.
2. `status_queues[N]`: Overwritten with per-load actuator operational reports.
3. `energy_semaphore`: Triggered by `EnergyMonitor` on grid/solar presence edge transitions.
4. `command_queue`: FIFO for incoming `LoadIntent` objects from domain controllers.
5. 100ms periodic timeout for internal watchdog evaluation and smooth UI state publishing.

### 3.2 Power Balancing & Knapsack Allocation
The `LoadControlEngine` arbitrates power strictly according to:
$$\text{Total Active Solar Loads (W)} \le \text{Instantaneous Solar Generation (W)}$$

- **Urgency Priority**: `CRITICAL` > `NORMAL` > `OPPORTUNISTIC` > `SHEDDABLE`.
- **Knapsack Greedy Optimization**: For equal urgency, larger continuous loads are prioritized to maximize solar absorption.
- **Source Constraints**: Respects actuator 3-position switch lock (`selected_source == SOLAR` or `GRID`). When set to `AUTO`, the Hub dynamically routes to Solar if headroom permits, falling back to Grid only if allowed by load preference.
- **Local Manual Run Protection**: When an operator starts a load locally (`control_mode == MANUAL_RUN`), the Hub respects manual operation and only forces `LOAD_OFF` for safety/emergency limits (e.g. tank full).

---

## 4. Water Tank Policy & Dynamic Demand Tiers (`TankController`)

The `TankController` governs pump demand by translating sensor reports and solar time into `LoadIntent` declarations.

### 4.1 Tiered Dynamic Target Architecture (`TankFillTier`)

Rather than a static 100% target that would waste grid energy during night-time recovery, `TankController` operates on dynamic demand tiers:

| Tier (`TankFillTier`) | Trigger Threshold | Dynamic Fill Target | Urgency | Source Preference | Rationale |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`CRITICAL_RECOVERY`** | Level $< 300‰$ | `normal_min_permille` (500‰) | `CRITICAL` | `ANY` (Grid / Solar) | Immediate recovery from drought danger; does not waste grid power filling beyond safe level |
| **`NORMAL_FILL`** | Level $< 500‰$ | `opportunistic_min_permille` (900‰) | `NORMAL` | `SOLAR_PREFERRED` | Standard daylight refill |
| **`OPPORTUNISTIC`** | Level $< 900‰$ (or pre-sunset) | `target_fill_permille` (1000‰) | `OPPORTUNISTIC` | `SOLAR_ONLY` / `SOLAR_PREF` | Tops off tank strictly on solar surplus; escalates before sunset |
| **`MANUAL_REQUEST`** | Button `FILL_REQUEST` | `target_fill_permille` (1000‰) | `NORMAL` | `SOLAR_PREFERRED` (day) / `ANY` (night) | User-demanded full fill |

### 4.2 Seamless Tier Transition & Dynamic Watchdog
- **Continuous Operation**: When rising level satisfies a tier (e.g., passing 500‰), if daylight conditions permit moving to `NORMAL_FILL`, the pump **remains running continuously** without contactor cycling.
- **Dynamic Watchdog**: `calculate_estimated_duration_s()` computes deficit precisely against the active tier target, preventing over-allocation of watchdog timers.

### 4.3 Operator Manual Stop Cooldown Protection
- If an operator manually stops the pump while filling (`LoadState::RUNNING` $\rightarrow$ `IDLE` with level $< 1000‰$), `TankController` clears `manual_fill_requested_` and enters a **30-minute cooldown**.
- During cooldown, automatic routine refills are suppressed so the Hub does not immediately restart the pump against the operator's intention.
- **Safety Overrides**: Cooldown is instantly bypassed if level drops into `CRITICAL_RECOVERY` ($< 300‰$) or if the operator presses the fill button again (`FILL_REQUEST`).
- Communication watchdog expiry (`LoadState::ERROR_TIMEOUT`) does **not** trigger cooldown.

---

## 5. Time Synchronization Architecture

- **SNTP Synchronization**: `TimeManager` connects to external NTP servers over Wi-Fi, setting POSIX timezone `<-04>4` (UTC-4).
- **Edge Node Time Sync**: Node reports trigger `CommandManager::process_node_wake`, immediately dispatching `farm::CommandType::SYNC_TIME` with epoch milliseconds if the node is out of sync.

---

## 6. Over-The-Air (OTA) Updates & Self-Healing

- **Self-Update Engine**: `OtaManager` validates manifests, streams image data to secondary OTA partitions, and verifies SHA256 checksums.
- **Fail-Safe Lifecycle Rollback**: If initialization of any core task or subsystem fails in `HubApp::init`, `session_healthy_ = false` is flagged and `OtaManager::rollback()` reboots into the previous valid firmware partition.
