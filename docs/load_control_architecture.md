# Load Control & Tank Controller Architecture

> **Status:** Design — Not yet implemented  
> **Last updated:** 2026-08-23

---

## 1. System Overview

The hub is the central controller of an off-grid solar system. There is **no battery bank**: the system operates
on real-time energy balance between solar generation and load consumption, with the public grid as a
complementary (not preferred) source.

Each load node has two interlocked contactors — **SOLAR** and **GRID** — and can only be on one at a time.
The hub's job is to signal each node which source to use, and when to run (for discretionary loads).

**Core principle:** Maximize solar utilization. Grid is a fallback, never a preference.

---

## 2. Load Classification

Loads fall into two fundamentally different categories:

### 2.1 Continuous Loads
Always powered. The only question is *which source* (SOLAR or GRID).

| Load | Notes |
|---|---|
| `FRIDGE` | Thermal inertia: compressor cycles on/off |
| `FREEZER` | Thermal inertia: compressor cycles on/off; longer hold tolerance |
| `ROUTER` | Always on; low wattage |
| `LIGHTING` | Circuit-level; may be turned off in emergencies |

### 2.2 Discretionary Loads
Turned on and off based on policy. Source selection also applies.

| Load | Notes |
|---|---|
| `PUMP` | Fills water tank; policy-driven; sole discretionary load for now |

---

## 3. Load Profile (Static Configuration)

Before real-time telemetry arrives, the hub needs a priori knowledge of each load's expected behavior.
This is a static `LoadProfile` per load, stored in NVS or provided at initialization.
It also serves as the **default** for load domain controllers that have no sensor input yet (see Section 6):

| Field | Description |
|---|---|
| `expected_watts_running` | Typical consumption when active (e.g., compressor ON) |
| `expected_watts_idle` | Typical consumption in idle/off-cycle (e.g., compressor OFF) |
| `can_shed` | Whether the load can be temporarily cut in emergencies |
| `max_shed_duration_s` | How long it can safely be without power (static/thermal default) |
| `is_continuous` | True for always-on loads; false for discretionary |

The `LoadProfile` is used for feasibility calculations before actual power reports arrive.
As load domain controllers grow smarter (e.g., via temperature sensors), they supersede
these static defaults dynamically without the LCT needing any changes.

---

## 4. Energy Availability — IEnergyMonitor

Two GPIO inputs on the hub, driven by filtered voltage presence signals:

- `is_solar_available()` → inverter is operating and has output voltage
- `is_grid_available()` → public grid has voltage

A `NullEnergyMonitor` implementation always returns `true`, enabling the system to work before
physical sensors are installed.

> **Note:** `is_solar_available()` is distinct from solar wattage. The wattage comes from the
> ESP-NOW solar sensor node. The GPIO only indicates whether the inverter is alive and has output.

---

## 5. Core Components

### 5.1 Load Control Task (LCT)

**Domain language:** Watts, sources, ON/OFF, watchdog timers.  
**Does NOT know about:** Tank levels, liters, flow rates, time-of-day fill policies.

**Responsibilities:**
- Maintain an internal table of all loads: current state, reported wattage, override mode.
- Calculate available solar headroom using real-time solar data + load states.
- Decide source assignment for continuous loads (SOLAR vs GRID) based on headroom.
- Accept `FillRequest` from TC and decide how to accommodate the pump.
- Manage pump command lifecycle: send start command, refresh watchdog, send stop on TC signal.
- Respect override mode: if a load has a physical source switch set (SOLAR or GRID), the hub
  must not send source-change commands to that load. It still counts its consumption in the
  energy budget.

**Triggered by:**
- Solar wattage change event (significant delta).
- Load status report from any node.
- `FillRequest` or `FillCancel` from TC.
- `IEnergyMonitor` state change (solar/grid available/unavailable).

**Two-layer decision model:**

The LCT separates pre-planning from continuous rebalancing:

| Layer | Data source | Purpose |
|---|---|---|
| **Pre-planning** | `LoadIntent.expected_watts_running` | "Can I commit this source for this load?" |
| **Continuous rebalancing** | Real wattage reported by each node | "What is actually happening? Adjust headroom." |

`expected_watts_running` only needs to be **conservatively correct**, not precise. It is used
to avoid over-committing solar before a load is turned on. Once the load is running, the LCT
discards the estimate and uses real telemetry exclusively.

**Compressor duty cycle as a natural opportunity:**

When a load node is on but its compressor is not running (thermostat satisfied), it reports
near-idle wattage. The LCT detects this automatically on each telemetry update and recalculates
headroom. This means the Scenario C window optimization (see Section 5.4) does not need to be
explicitly triggered by a controller — the LCT detects the surplus organically and can
activate a pending `FillRequest` or shift another load from GRID to SOLAR without any
domain controller needing to be aware of the others.

```
FreezerController: LoadIntent { desired=ON, expected=700W }
LCT: allocates 700W solar, sends CMD_ON to freezer node

[Thermostat satisfied → compressor not running]
Freezer node reports: 50W actual

LCT rebalances: 650W surplus detected → activates pending FillRequest on solar
                                      → or shifts fridge from GRID to SOLAR
```

---

### 5.2 Tank Controller (TC)

**Domain language:** Liters, level percentages, flow rate, time of day, fill policy.  
**Does NOT know about:** Watts, source selection, ESP-NOW commands.

**Responsibilities:**
- Monitor water tank level from telemetry reports.
- Apply fill policy: decide when to request filling, at what urgency, and to what target level.
- Recalculate and update `FillRequest` urgency as level changes over time.
- Calculate `estimated_fill_duration_s` = `volume_to_fill / pump_flow_rate + margin` and include
  it in the `FillRequest` so the LCT can set an appropriate watchdog timer.
- Signal LCT when target level is reached (`FillComplete`).
- Detect anomalies and emit events to the AlarmManager.

**Configuration (NVS or constructor):**
- `tank_capacity_liters`
- `pump_flow_rate_liters_per_second`
- Level thresholds and policies (see Section 6)

---

### 5.3 TC→LCT Interface — Pump LoadIntent

TC communicates its fill intention via the standard `LoadIntent` struct. The LCT never reads
tank levels or calculates fill duration math itself.

```
LoadIntent (emitted by TankController):
    load_index:               LoadIndex::PUMP
    desired_state:            ON | OFF
    source_preference:        SOLAR_ONLY | SOLAR_PREFERRED | ANY
    urgency:                  OPPORTUNISTIC | NORMAL | URGENT
    max_hold_duration_s:      0
    estimated_on_duration_s:  uint32_t  (calculated by TC: volume / flow_rate + margin)
```

TC may escalate urgency as level continues to drop between fill cycles:

| Urgency | Typical trigger | LCT behavior |
|---|---|---|
| `OPPORTUNISTIC` | Level > 75%, solar available | Only fills if solar window is free; never uses grid |
| `NORMAL` | Level 40–75% | Waits for a solar window up to `max_wait_s`; uses grid if exceeded |
| `URGENT` | Level < 40% or critical | Uses grid immediately if solar is insufficient |

---

### 5.4 Solar Window Optimization (Scenario C)

When the LCT wants to run the pump on solar but headroom is insufficient (e.g., freezer compressor
is running), it can wait for a **compressor off-cycle window** rather than immediately switching to grid.

**Detection:** Freezer reported wattage drops below `idle_threshold` → compressor has stopped.

**Window Fill FSM (inside LCT):**

```
FILL_PENDING
    │
    ├─ solar headroom OK → FILLING_SOLAR (direct)
    │
    └─ insufficient headroom
            │
            ▼
    WAITING_FOR_WINDOW
    (monitor freezer watts; timeout = max_wait_s per urgency)
            │
            ├─ compressor off-cycle detected
            │        │
            │        ▼
            │  FILLING_SOLAR_WINDOW
            │  freezer held (compressor not cycling, still powered)
            │  timer X started (max hold duration)
            │        │
            │        ├─ tank fills before X → DONE (restore freezer to normal)
            │        │
            │        └─ X expired, pump still running
            │                 → pump switches to GRID
            │                 → freezer restored to normal solar operation
            │                 → continue filling on GRID until FillComplete
            │
            └─ max_wait_s expired (no window appeared, or freezer in recovery)
                     → LCT decides based on urgency:
                       OPPORTUNISTIC → defer, no grid usage
                       NORMAL        → switch pump to GRID
                       URGENT        → switch pump to GRID immediately
```

**Hold duration X:** Static value from NVS today. In the future, can be dynamic based on freezer
compartment temperature if a temperature sensor is added to the freezer node.

**Freezer recovery detection:** If freezer reported wattage has been above `running_threshold` for
the last N minutes, the compressor is in thermal recovery (running continuously). In this state the
LCT skips window waiting, as a viable off-cycle is unlikely to appear soon.

---

### 5.5 Pump Command Protocol (Watchdog)

The pump node is fully command-driven. It does not stop autonomously except on watchdog expiry.

**Start:** LCT sends `CMD_FILL_[SOLAR|GRID]` with a watchdog timer derived from
`estimated_duration_s` provided by TC.

**Refresh:** Each time a tank report arrives, TC recalculates remaining duration and informs LCT.
LCT resends the command with the updated watchdog. This acts as a keep-alive: if the hub loses
comms, the pump self-stops after the last watchdog expires.

**Stop:** TC signals `FillComplete` (target level reached) → LCT sends `CMD_STOP`.

---

### 5.6 AlarmManager

A dedicated component that receives events from multiple sources and evaluates alarm conditions.
Separate from TC and LCT to avoid coupling domain logic with alerting behavior.

| Source | Event | Alarm |
|---|---|---|
| TC | Level dropping at night / unexpected rate | Irrigation left open |
| TC | Level below critical threshold | Tank nearly empty |
| IEnergyMonitor | Solar unavailable > X minutes | Inverter protection / fault |
| IEnergyMonitor | Grid unavailable | Grid outage |
| LCT | Load in shed state > X minutes | Degraded operation |

Alarm output (buzzer GPIO, display alert) is the AlarmManager's concern, not the event source's.

---

### 5.7 Solar Data Pipeline

Solar nodes transmit at up to 8 Hz. A **dedicated solar queue** prevents solar messages from
starving or being starved by load status messages.

```
Solar Node (≤8 Hz) → solar_queue → SolarProcessor task
    → moving average calculation
    → significant-change detection
    → signal LCT via EventBits
    → update solar snapshot (for UI)
```

LoadControlStatus messages flow through per-node queues directly to the LCT (see Section 5.8).

---

### 5.8 Synchronization Strategy

#### Queue Layout

The LCT uses a **FreeRTOS QueueSet** to block on multiple input sources simultaneously,
avoiding the need for polling loops or shared mutexes on its internal state table.

| Queue / Primitive | Type | Depth | Producer | Pattern |
|---|---|---|---|---|
| `solar_queue` | `SolarUpdate` | **1** | SolarProcessor task | `xQueueOverwrite` — always latest value |
| `status_queues[N]` | `LoadStatusUpdate` | **1 per load** | Message handler (per node) | `xQueueOverwrite` — always latest per node |
| `energy_semaphore` | binary semaphore | 1 | `IEnergyMonitor` ISR | Standard give/take |
| `command_queue` | `LctCommand` | 8 | TC, Domain Controllers | `xQueueSend` — no overwrite, ordered |

`solar_queue` and all `status_queues` use depth=1 with `xQueueOverwrite()`. This means the LCT
always reads the most recent value — stale intermediate states are discarded automatically.

`command_queue` uses standard `xQueueSend` because intents and requests must not be silently
discarded (`FillRequest`, `LoadIntent` changes, `FillCancel` are events, not continuous data).

#### QueueSet Membership

`solar_queue`, all `status_queues[N]`, and `energy_semaphore` are added to the QueueSet.
`command_queue` is **not** in the set — it is polled explicitly with timeout=0 at the start
of each loop iteration to guarantee command priority over data updates.

#### LCT Task Loop

```
loop:
  // 1. Drain commands first (priority over data)
  while xQueueReceive(command_queue, timeout=0):
      process_command(cmd)
      snapshot_dirty = true

  // 2. Block on data sources (solar, status, energy monitor)
  active = xQueueSelectFromSet(lct_set, timeout=100ms)
  if   active == solar_queue:        process_solar();       snapshot_dirty = true
  elif active == status_queues[i]:   process_status(i);     snapshot_dirty = true
  elif active == energy_semaphore:   process_energy();      snapshot_dirty = true
  else:                              process_periodic_tick()  // true idle timeout

  // 3. UI snapshot at ~10 Hz (time-based, not event-based)
  if (now - last_ui_update >= 100ms) and snapshot_dirty:
      write_ui_snapshot()   // spinlock, fast copy
      snapshot_dirty = false
      last_ui_update = now
```

#### Why time-based UI update (not on periodic_tick)

`process_periodic_tick()` only fires when the QueueSet timeout elapses with no data arriving.
During normal daytime operation (solar at 8 Hz), the timeout never fires — the LCT is
continuously processing `solar_queue`. A time-based check at the end of every loop iteration
guarantees the UI snapshot is refreshed at ~10 Hz regardless of system load.

#### Ownership and thread safety

The LCT is the **sole writer** of its internal load state table. All other tasks communicate
via queues — never by directly writing to LCT state. This eliminates the need for any mutex
on the load state table itself.

The UI snapshot is the only shared struct written by LCT and read by another task (UI Task).
It is protected by a `portMUX_TYPE` spinlock. The write is a fast struct copy; the lock is
held for microseconds.

#### Periodic housekeeping (process_periodic_tick)

Tasks that require time-based evaluation even during idle periods:
- TC: check if `WAITING_FOR_WINDOW` timeout has expired.
- LCT: check if any node has not reported within its expected interval (offline detection).
- AlarmManager: evaluate time-based alarm conditions (e.g., inverter silent for > X minutes).

---

### 5.9 LCT Decision Priority Rules

When solar capacity is insufficient to serve all active `LoadIntent` requests, the LCT must
decide which loads get solar and which migrate to GRID (or are deferred).

> **All priority values and tier assignments are configurable via NVS.** The tables below
> represent the recommended defaults. No priority rule is hardcoded in the LCT logic —
> the LCT reads a `PriorityConfig` table at startup and applies it uniformly.

#### Table A — Solar Loss: Who Migrates to GRID (or is Deferred) First

When solar headroom decreases, the LCT migrates or defers loads starting from the top:

| Order | Urgency | Load type | Action |
|---|---|---|---|
| 1st (first to go) | `SHEDDABLE` | any | Turn OFF or move to GRID if available |
| 2nd | `OPPORTUNISTIC` | any | Do not activate; deactivate if running |
| 3rd | `NORMAL` | discretionary (pump) | Defer `FillRequest`; pump waits for next window |
| 4th | `NORMAL` | continuous | Migrate to GRID; thermal inertia provides tolerance |
| 5th (last) | `CRITICAL` | any | Never migrated, never shed |

#### Table B — Solar Recovery: Who Returns to SOLAR First

When solar headroom increases, the LCT restores loads to solar starting from the top:

| Order | Urgency | Load type |
|---|---|---|
| 1st | `CRITICAL` | any (should already be on solar) |
| 2nd | `NORMAL` | continuous |
| 3rd | `NORMAL` | discretionary (pump, if TC has a pending request) |
| 4th | `OPPORTUNISTIC` | any |
| 5th | `SHEDDABLE` | any |

#### Tiebreaker Rules (within the same tier)

When two loads share the same urgency and type, the LCT applies these tiebreakers in order:

1. **`max_hold_duration_s` (descending):** The load that can tolerate being off longest
   is migrated to GRID first.
   *Example: Freezer (30 min) migrates before Fridge (20 min) — Fridge stays on solar longer.*

2. **Wattage vs. deficit (ascending):** If a single lower-wattage load is sufficient to
   resolve the deficit, prefer migrating it over a higher-wattage load.
   *Example: need to free 300 W — migrate Fridge (350 W) with one operation rather than
   Freezer (700 W), which would over-free solar and waste grid capacity.*

3. **Configurable per-load rank (NVS fallback):** Each `LoadProfile` includes an explicit
   `priority_rank` field (lower = higher priority for solar). Used as the final tiebreaker
   when rules 1 and 2 produce a tie.

#### Continuous vs. Discretionary at the Same Urgency

Within the same `urgency` tier, **continuous loads take precedence over discretionary loads**
for solar allocation:

- A continuous load migrated to GRID incurs a real cost (grid consumption).
- A discretionary load deferred simply waits — no power is consumed on either source.

Exception: a discretionary load at a *higher* urgency tier always beats a continuous load at
a *lower* tier.

*Example: `FillRequest` at `URGENT` > `FreezerController` at `NORMAL`.*

---

## 6. Load Domain Controllers

### 6.1 Motivation

Instead of the LCT hardcoding knowledge about each load's behavior (thermal tolerance, urgency
rules, sensor thresholds), each load has its own **domain controller** that understands its
physical constraints and emits a `LoadIntent` to the LCT.

This decouples two concerns:
- **LCT:** pure energy arbitrator — receives intents, allocates sources, manages headroom.
- **Domain controllers:** domain experts — understand temperature, level, time of day, etc.

The pattern applies uniformly whether a controller is trivial (wraps static NVS values) or
sophisticated (computes dynamic tolerances from live sensor data). **The LCT interface never
changes** as controllers grow smarter.

---

### 6.2 The LoadIntent Interface

Every domain controller emits a `LoadIntent` to the LCT whenever its desired state or
constraints change:

```
LoadIntent {
    load_index:               LoadIndex
    desired_state:            ON | OFF
    source_preference:        SOLAR_ONLY | SOLAR_PREFERRED | ANY
    urgency:                  CRITICAL | NORMAL | OPPORTUNISTIC | SHEDDABLE
    max_hold_duration_s:      uint32_t   // 0 = cannot be shed; max safe off-time
    estimated_on_duration_s:  uint32_t   // for episodic loads (pump); 0 for continuous
}
```

> `desired_state` has only two values: the controller either wants the load **ON** or **OFF**.
> "Turn on only if there is solar surplus" semantics are expressed via `urgency = OPPORTUNISTIC`
> combined with `source_preference = SOLAR_ONLY` — not via a third state.

The LCT uses these intents to decide source assignment and shed order. It never reads
sensor values or applies per-load policy logic itself.

---

### 6.3 Controller Summary

| Controller | Today (no sensor) | With sensor |
|---|---|---|
| `TankController` | Level from telemetry → dynamic `LoadIntent` | Same, extended with flow-rate learning |
| `FreezerController` | Static `max_hold` from NVS | Dynamic `max_hold` from compartment temp |
| `FridgeController` | Static `max_hold` from NVS | Dynamic `max_hold` from compartment temp |
| `RouterController` | Always `urgency = CRITICAL`, `max_hold = 0` | Unchanged |
| `LightingController` | Sheddable, `urgency = SHEDDABLE` | May add occupancy sensor later |

**A trivial controller today** simply reads its `LoadProfile` from NVS and emits a fixed
`LoadIntent`. It requires no sensor and no logic. As sensors are added, the controller's
`get_current_intent()` method is updated internally; the LCT receives the same struct type regardless.

---

## 7. Fill Policy (TC Behavior)

TC evaluates the following inputs on each tank report:

- Current level (%)
- Rate of level change (rising / stable / falling — estimated from recent reports)
- Time of day
- `IEnergyMonitor` state (solar available?)

**Example policy logic (thresholds tunable via NVS):**

| Condition | Request |
|---|---|
| Level \> 80%, solar available | `OPPORTUNISTIC / SOLAR_ONLY` |
| Level 60–80% | `OPPORTUNISTIC / SOLAR_PREFERRED` |
| Level 40–60% | `NORMAL / SOLAR_PREFERRED` |
| Level 20–40% | `NORMAL / ANY` |
| Level \< 20% | `URGENT / ANY` |
| After sunset, level ≥ end-of-day target | No request (wait until morning) |
| After sunset, level < end-of-day target | `NORMAL / ANY` (grid accepted at night) |

TC may escalate urgency dynamically. A pending `OPPORTUNISTIC` request is replaced with `NORMAL`
(or `URGENT`) if the level continues to fall before the LCT was able to accommodate it.

---

## 8. SystemState Refactoring

`SystemState` has grown into a monolithic shared object consumed by all components. It should
be refactored so that each domain owns its data:

- **LCT** maintains its own internal load state table (actual source, wattage, override mode).
- **TC** maintains its own tank state (level history, fill intent state).
- **Domain controllers** maintain their own per-load state (override, last intent emitted).
- **UI snapshot** is a lightweight struct populated by the hub app periodically for the UIController
  to render. It is read-only from the UI's perspective.

No two domain components should share mutable state through `SystemState`. Each component
owns its data; the UI reads a published snapshot.

---

## 9. Override Mode Handling

Each load node has a physical 3-position switch: `AUTO | SOLAR | GRID`.
The position is reported in every `LoadControlStatus` message.

**LCT behavior per override state:**

| Override | LCT action |
|---|---|
| `AUTO` | Full source control: LCT sends SOLAR or GRID commands freely |
| `SOLAR` | Cannot change source. Counts load as fixed on solar budget. For pump: can still START/STOP, but only on solar (no grid fallback) |
| `GRID` | Cannot change source. Counts load against grid; does not affect solar headroom |

---

## 10. What Does Not Change

- The ESP-NOW protocol and payload structures remain as-is.
- `LoadControlHandler` continues to route incoming payloads to the correct `LoadIndex`.
  `LoadIndex` remains hub-internal; nodes identify themselves by `NodeId`.
- The pump node's LED strip level rendering remains its own concern; the hub broadcasts tank
  level via ESP-NOW for that purpose.
- UIController and all existing screens are unaffected by this architecture.

---

## 11. Open Questions / Future Work

- **Temperature sensors (fridge/freezer):** `FreezerController` and `FridgeController` already
  define the interface for emitting `LoadIntent` with dynamic `max_hold_duration_s`. Adding a
  temperature sensor requires only updating the controller's internal logic; the LCT is unchanged.
- **Learned flow rate:** TC could estimate `pump_flow_rate` empirically by correlating pump-on
  duration with observed level changes, improving watchdog accuracy over time.
- **Multiple discretionary loads:** When more discretionary loads are added (e.g., water heater),
  each gets its own domain controller emitting `LoadIntent`. The window FSM in the LCT will need
  to generalize to handle competing episodic requests.
- **Solar sensor frequency:** Sending at 2 Hz minimum even when stable may be revisited on the
  solar node firmware. The hub already handles high-frequency data gracefully via the dedicated queue.
- **LoadIntent priority conflict resolution:** When two controllers simultaneously request solar
  (e.g., pump URGENT and freezer CRITICAL), the LCT needs a defined tiebreaker. This should be
  a configurable priority table, not hardcoded logic.

