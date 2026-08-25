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
2. **Phase 4 (Current):** Introduction of `NodeRegistry` and `UiSnapshot`, decoupling node lifecycle management and real-time UI rendering from `SystemState`.
3. **Phase 5 (Upcoming):** Refactoring `UIController` and message handlers to eliminate all remaining references to `SystemState`, followed by the complete deletion of `system_state.hpp` and `g_state_mutex`.

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

## 4. Decoupled Presentation Layer: `UiSnapshot`

### 4.1 Concept & Operational Model
The UI display task (running at ~2-5 Hz) requires a frozen, consistent photograph of system metrics without locking the high-frequency `LoadControlTask` (LCT, running at ~8-10 Hz).

```
   ┌─────────────────────────────────────────────────────────────┐
   │             LoadControlTask (Sole Writer of Loads)          │
   │               - Runs LoadControlEngine @ 8-10 Hz            │
   │               - Zero heap, pure ETL algorithms              │
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

### 4.2 Future Modularization of `UiSnapshotData`
To maintain strict single-responsibility principles in the upcoming UI refactor (Phase 5), `UiSnapshotData` will be partitioned into granular domain structures:
- `WaterTankUiData`: Focused strictly on level, float switch, ultrasonic metrics.
- `SolarUiData`: Focused on PV irradiance, instant generation, panel temperature, and daily Wh yield.
- `LoadControlUiData`: Focused on electrical load contactors, active source, power margin, and episodic window states.

Screen rendering functions in `UIController` will accept only their respective sub-structs (e.g., `render_water_tank_screen(const WaterTankUiData&)`).

---

## 5. Architectural Comparison Summary

| Attribute | Legacy Architecture (`SystemState`) | New Architecture (`NodeRegistry` + `UiSnapshot` + LCT) |
| :--- | :--- | :--- |
| **State Paradigm** | Shared mutable global struct | Thread-private state + Lock-free/Fast Snapshot wrappers |
| **Locking Strategy** | Global recursive/binary FreeRTOS mutex | Localized fine-grained mutexes with sub-microsecond holds |
| **Memory Management** | Mixed dynamically sized structures | 100% Zero-Heap (`etl::vector`, `std::array`, fixed PODs) |
| **Domain Isolation** | All subsystems coupled via single header | Strict interface injection (`ITankController`, `INodeRegistry`, etc.) |
| **Testability** | Requires instantiating global state | Fully testable in Linux host tests with isolated mocks/real HALs |
