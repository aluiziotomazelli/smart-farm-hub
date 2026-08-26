# Smart Farm Hub - New Architecture & State Decomposition

## 1. Overview & Context

This document outlines the architectural evolution of the **Smart Farm Central Hub**, specifically documenting the transition from a monolithic shared state (`SystemState`) to a decoupled, domain-oriented, thread-safe architecture based on dedicated managers, isolated state machines, and fine-grained snapshot structures.

---

## 2. Deprecation & Planned Phase-out of `SystemState`

### 2.1 The Legacy Problem
Historically, `SystemState` acted as a global god-object struct containing:
- Per-node network and firmware metadata.
- Raw and derived telemetry for water tank and solar sensor nodes.
- Electrical load operational status and power consumption.
- System-level connectivity (Wi-Fi, SNTP time synchronization).
- Display navigation state timestamps.

**Architectural Bottlenecks:**
- **Concurrency & Locking Contention:** Required holding a global mutex (`g_state_mutex`) across multiple FreeRTOS tasks (message dispatcher, display task, main application loop), risking priority inversion and timing jitter.
- **Tight Coupling:** Components had direct visibility and write access to unrelated subsystems.
- **Heap Fragmentation Concerns:** Suboptimal data structure layouts for continuous 24/7 runtime.

### 2.2 Phase-out Roadmap
1. **Phase 1-3 (Completed):** Domain controllers (`TankController`, `EnergyMonitor`) and core arbitration engine (`LoadControlEngine`) operate on pure private domain interfaces and tables with zero global state dependencies.
2. **Phase 4 (Completed):** Introduction of `NodeRegistry`, `UiSnapshot`, and `LoadControlTask` (LCT), decoupling node lifecycle management, load arbitration execution, and real-time UI rendering from `SystemState`.
3. **Phase 5 (Completed):** Refactoring of `MessageDispatcher` handlers (`WaterTankHandler`, `SolarSensorHandler`, `LoadControlHandler`), `CommandManager`, `UIController`, `DisplayManager`, and `HubApp` to eliminate all references to `SystemState`, followed by the complete deletion of `system_state.hpp` and `g_state_mutex`.

---

## 3. Dedicated Node Management: `NodeRegistry`

### 3.1 Motivation & Responsibilities
Node identity, power profiles (`ALWAYS_ON`, `LOW_POWER`, `DEEP_SLEEP`), and firmware versions are metadata concerns distinct from continuous energy arbitration.

`NodeRegistry` is the **single source of truth** for:
- Mapping known `farm::NodeId`s to their firmware version (`major.minor.patch`).
- Maintaining node power regimes (`farm::PowerProfile`).
- Decoupling command routing policies (instant dispatch vs. queued FIFO in `CommandManager`).

### 3.2 Key Design Choices
- **Interface Segregation:** Defined via `INodeRegistry` in `include/interfaces/i_node_registry.hpp`.
- **Zero Heap Allocation:** Internal fixed-capacity storage using `std::array<farm::NodeMetadata, farm::MAX_HUB_NODES>` and returning `etl::vector<farm::NodeMetadata, farm::MAX_HUB_NODES>`.
- **Thread Safety:** Protected via fine-grained, localized `std::lock_guard<std::mutex>` (held for sub-microsecond memory operations).
- **Liveness Delegation:** Liveness is handled natively by the transport layer (`EspNowManager::is_peer_online`) based on the negotiated `heartbeat_interval_ms`, avoiding duplicated watchdog timers in application space.

---

## 4. Asynchronous Energy Arbitration: `LoadControlTask` (LCT)

### 4.1 Role & Concurrency Isolation
`LoadControlTask` is the **sole writer** to the `LoadControlEngine`. It encapsulates:
- Energy budget tracking (solar power vs allocated load consumption).
- Knapsack-style greedy power headroom arbitration.
- Episodic load solar window FSM (e.g. pump run capture during thermal surplus windows).
- Relay/contactor switching protection (minimum switch intervals, safe off times).

### 4.2 Reactive Multi-Channel IPC Architecture (`QueueSet`)
Instead of polling, `LoadControlTask` blocks on a FreeRTOS `QueueSet` (`xQueueSelectFromSet`) aggregating:
1. `solar_queue` (Length 1, updated via `queue_overwrite` on new PV telemetry).
2. `status_queues[N]` (Length 1 per load actuator, updated via `queue_overwrite`).
3. `energy_semaphore` (Binary semaphore triggered on grid/solar presence edge interrupts from `EnergyMonitor`).
4. `command_queue` (FIFO for incoming `LoadIntent`s from domain controllers).
5. 100ms periodic timeout (for internal FSM ticks and watchdogs).

```
   ┌─────────────────────────────────────────────────────────────┐
   │                       Event Sources                         │
   │  - Solar Telemetry (Overwrite Queue)                        │
   │  - Actuator Reports (Overwrite Queue)                       │
   │  - Grid / Inverter ISR (EnergyMonitor Binary Semaphore)     │
   │  - Domain Controller Intents (FIFO Queue)                   │
   └──────────────────────────────┬──────────────────────────────┘
                                  │ Wakeup (Non-blocking QueueSet)
                                  ▼
   ┌─────────────────────────────────────────────────────────────┐
   │             LoadControlTask (Sole Writer of Loads)          │
   │               - Evaluates LoadControlEngine                 │
   │               - Dispatches via ILoadActuatorDispatcher      │
   └──────────────────────────────┬──────────────────────────────┘
                                  │ update_energy_and_loads() (~100ms)
                                  ▼
   ┌─────────────────────────────────────────────────────────────┐
   │                        UiSnapshot                           │
   │  - Water Tank Telemetry (level, battery, status, backup)    │
   │  - Solar Telemetry (power, irradiance, panel temp, yield)   │
   │  - Grid & Headroom Status                                   │
   │  - Electrical Loads & Window States                         │
   │  - Thread-Safe Lock: std::lock_guard<std::mutex>            │
   └──────────────────────────────▲──────────────────────────────┘
                                  │ snapshot.get() (Microsecond copy)
                                  │
   ┌──────────────────────────────┴──────────────────────────────┐
   │                   UIController / DisplayTask                │
   │     - Renders OLED screens from local read-only copy        │
   │     - Zero blocking on LCT or network handlers              │
   └─────────────────────────────────────────────────────────────┘
```

---

## 5. Physical Grid & Solar Monitoring: `EnergyMonitor`

### 5.1 Fail-Fast Initialization & ISR Safety
`EnergyMonitor` provides hardware edge sensing for utility grid and inverter power rails:
- **Clean Construction:** Constructed without handle dependencies (`EnergyMonitor(IGpioHAL&, IHalFreertos&)`).
- **Explicit Signal Handover:** Receives `LoadControlTask`'s internal `energy_semaphore` during `init(config)` or via `set_signal_semaphore()`.
- **Fail-Fast Validation:** If `enable_interrupts == true` and `signal_semaphore == nullptr`, `init()` immediately returns `ESP_ERR_INVALID_ARG`, preventing silent ISR failure in production.

---

## 6. Message Handlers & Domain Integration

### 6.1 `WaterTankHandler`
- Receives incoming `farm::WaterLevelReport`.
- Updates `NodeRegistry::set_power_profile`.
- Updates `UiSnapshot::update_water_tank`.
- Feeds `TankController::on_tank_report` to compute policy and fill duration.
- Posts resulting `LoadIntent` to `ILoadControlTask::post_load_intent`.
- Forwards `TANK_LEVEL_UPDATE` to `CommandManager::broadcast_tank_level` for actuator node local display updates.
- Triggers `CommandManager::process_node_wake`.

### 6.2 `SolarSensorHandler`
- Receives incoming `farm::SolarSensorReport`.
- Computes pure domain AC power estimate via `solar::estimate(report, solar_cfg)`.
- Accumulates daily Wh generation integral (`daily_yield_wh_hub`).
- Updates `NodeRegistry::set_power_profile` (tracking day/night transition state).
- Updates `UiSnapshot::update_solar` with physical and calculated telemetry.
- Posts `SolarPowerUpdate` to `ILoadControlTask::post_solar_update` (waking LCT via `QueueSet` without polling).
- Triggers `CommandManager::process_node_wake`.

### 6.3 `LoadControlHandler`
- Receives incoming `farm::LoadControlStatusReport` from actuator nodes (e.g. Pump Control).
- Formats `LoadStatusUpdate` with circuit ID, load state, operating mode, active power source, instant watts, and runtime counters.
- Posts status directly to `ILoadControlTask::post_load_status` (overwriting the dedicated queue for that `LoadIndex`).
- Triggers `CommandManager::process_node_wake`.

---

## 7. Command Routing & Dispatching: `CommandManager`

### 7.1 Architecture & Dual Roles
`CommandManager` implements `ICommandManager` (which inherits from `ILoadActuatorDispatcher`):
1. **Automated Load Actuation (`dispatch_decision`):** Dispatches load state and source transitions directly from `LoadControlTask` to actuator nodes.
2. **Administrative Command Routing (`send_command` / `dispatch_single_command`):**
   - **`ALWAYS_ON` nodes:** Transmitted immediately via ESP-NOW.
   - **`DEEP_SLEEP` / `LOW_POWER` nodes:** Enqueued in a fixed-capacity, zero-heap RAM queue (`etl::queue<PendingCommand, MAX_PENDING_COMMANDS>`).
   - **Reactive Dispatch (`process_node_wake`):** Called by message handlers when a sleeping node transmits telemetry, draining pending commands instantly before the node returns to sleep.

---

## 8. Presentation Layer: `UiSnapshot` & `UIController`

### 8.1 Concept & Operational Model
The UI display task (running at ~2-5 Hz) requires a frozen, consistent photograph of system metrics without locking the high-frequency `LoadControlTask` (running at ~8-10 Hz).

`UiSnapshot` acts as a thread-safe telemetry cache:
- Producers (`LoadControlTask`, `WaterTankHandler`, `SolarSensorHandler`, `HubApp`) update specific sub-fields via fast lock guards.
- Consumers (`DisplayManager` / `UIController`) call `ui_snapshot.get()` once per frame, obtaining a local `UiSnapshotData` copy in sub-microseconds without blocking any application tasks.
- Static node metadata (firmware version, power profile) is queried on-demand from `INodeRegistry`.

---

## 9. Unified Subsystem Lifecycle & OTA Resilience: `HubApp`

### 9.1 Composition Root & Failure Rollback
To avoid leaving the Hub in a zombie state after a failed firmware update, all background tasks and hardware subsystems are orchestrated within `HubApp::init(...)`:

```
   ┌─────────────────────────────────────────────────────────────┐
   │                       app_main()                            │
   │  - Instantiates HALs, Drivers, Queues, Managers, Handlers   │
   │  - Injects all dependencies into HubApp                     │
   │  - Calls app.init() -> app.run()                            │
   └──────────────────────────────┬──────────────────────────────┘
                                  │
                                  ▼
   ┌─────────────────────────────────────────────────────────────┐
   │                       HubApp::init()                        │
   │  1. Check pending OTA verification                          │
   │  2. Initialize Core & Hub NVS Storage                       │
   │  3. Initialize & Start UiInputManager                       │
   │  4. Initialize & Start DisplayManager                       │
   │  5. Initialize LoadControlTask & hand semaphore to EnergyMon│
   │  6. Initialize EnergyMonitor & Start LoadControlTask        │
   │  7. Initialize WiFi & ESP-NOW Transport                     │
   │  8. Start MessageDispatcher Task                            │
   │  9. Start SNTP Time Synchronization                         │
   │  10. If ANY step fails -> Trigger check_firmware() Rollback │
   └─────────────────────────────────────────────────────────────┘
```

If any task creation or memory allocation fails when booting a new firmware partition, `HubApp::init` flags `session_healthy_ = false`, invokes `ota_manager_.rollback()`, and restarts into the previous stable firmware partition.

---

## 10. Architectural Comparison Summary

| Attribute | Legacy Architecture (`SystemState`) | New Architecture (`NodeRegistry` + `UiSnapshot` + LCT) |
| :--- | :--- | :--- |
| **State Paradigm** | Shared mutable global struct | Thread-private state + Lock-free/Fast Snapshot wrappers |
| **Locking Strategy** | Global recursive/binary FreeRTOS mutex | Localized fine-grained mutexes with sub-microsecond holds |
| **Memory Management** | Mixed dynamically sized structures | 100% Zero-Heap (`etl::vector`, `std::array`, fixed PODs) |
| **Domain Isolation** | All subsystems coupled via single header | Strict interface injection (`ITankController`, `INodeRegistry`, `ILoadControlTask`, etc.) |
| **Arbitration Execution**| Split across handlers and display loops | Centralized in dedicated `LoadControlTask` via `QueueSet` |
| **Lifecycle & Rollback** | Fragmented manual startups in `app_main` | Centralized in `HubApp::init` with unified OTA failure rollback |
| **Testability** | Requires instantiating global state | Fully testable in Linux host tests with isolated mocks/real HALs |
