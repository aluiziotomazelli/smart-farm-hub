# Smart Farm Hub — Pending Issues & Future Enhancements

## 1. `espnow_manager::clear_peer_stats(NodeId node_id)`
- **Description**: Add a method in `EspNowManager` to reset/clear ESP-NOW packet loss counters, retry metrics, and peer transmission statistics for a specific `NodeId`.
- **Target Use Case**: Required for option `5. Clear Stats` in the Universal Node Submenu to allow operators to benchmark connection quality from a fresh baseline.

## 2. Remote Node Firmware Version (`fw_version`) Persistence & Display
- **Description**: Store remote node firmware versions (`fw_version`) in Hub memory (`SystemState` / NVS) for each `NodeId`.
- **Target Use Case**: Display firmware version on the `1. Last Report` screen without sending blocking on-demand queries to sleeping nodes.
- **Population Strategy**:
  - Update `fw_version` when node completes an OTA update and sends post-update handshake; or
  - Include 2-byte `fw_version` field in common telemetry packet header.

## 3. Universal Node Submenu & Viewport Scrolling Engine (COMPLETED)
- **Status**: ✅ Implemented in `UIController` with `SubmenuItem` enum and 4-item sliding window viewport renderer (`render_node_submenu`).

## 4. Dynamic `PowerProfile` Telemetry & Hybrid Command Dispatch (Instant vs. Queued NVS) (COMPLETED)
- **Status**: ✅ Implemented. Incorporated 1-byte `PowerProfile` (`ALWAYS_ON`, `LOW_POWER`, `DEEP_SLEEP`) into node telemetry reports (with 21-byte fallback parsing in `WaterTankHandler`). `CommandManager` dynamically dispatches commands instantly over ESP-NOW for `ALWAYS_ON` nodes, and enqueues commands in FIFO for `DEEP_SLEEP` / `LOW_POWER` nodes to drain on their next wake cycle.

## 5. Multi-Command Queue per Node (`PendingNodeCommand` FIFO Queue & Logging Fix) (COMPLETED)
- **Status**: ✅ Implemented in `HubStats` with `pending_cmds[MAX_HUB_NODES][MAX_PENDING_PER_NODE]` matrix and FIFO methods (`push_pending`, `pop_pending`, `peek_pending`). Integrated with `CommandManager`.

## 6. Deduplication of Pending Command Logic & Centralized Auto-Time Sync Drift Check (COMPLETED)
- **Status**: ✅ Implemented in `CommandManager`. Centralized `send_command`, `process_node_wake`, `check_and_arm_time_sync`, and hybrid dispatch (`ALWAYS_ON` instant vs `DEEP_SLEEP` queued). Refactored `MessageDispatcher` to handle ACKs automatically via `espnow::AckStatus` return from `IPayloadHandler`. Removed direct IDF timer calls from `WaterTankHandler` in favor of `ITimerHAL`.

## 7. Command Retries Generating Sequence Number Mismatches (`ACK seq_num mismatch`) (COMPLETED)
- **Status**: ✅ Resolved. Fixed ordering issue where `confirm_reception` (ACKing the node's report) was executing *after* blocking command dispatch operations. Moving report ACK confirmation to the start of `handle_payload` stopped cascading node retransmissions. Additionally, fixed bug where `ui_controller.cpp` queued invalid `0xFE` command instead of `espnow::CommandType::REBOOT` (causing node `ERROR_PROCESSING` rejections).

## 8. Design Note: ESP-NOW Logical ACK Retries vs Application Retries (RESOLVED)
- **Status**: ✅ Resolved. Exposed `logical_ack_retries` via `EspNowConfig` (defaults to `0`). MAC delivery failures (L2 radio link) resolve in <15ms and trigger `NOTIFY_MAX_FAILURES`, while L7 ACK timeouts return `ESP_ERR_TIMEOUT` in 500ms without retransmitting duplicate packets over the air. Higher-level retries and sleeping node command queuing are cleanly handled by `CommandManager` FIFO.
- **Category**: Architecture & Design Decision Documenting
- **Note**: Configured `logical_ack_retries = 0` by default.
- **Trade-off / Rationale**:
  - *MAC-level retries*: Physical link failures (MAC ACK missing) increment `send_fail_count_`. Reaching `MAX_FAILURES` (3) notifies `rx_task` with `NOTIFY_MAX_FAILURES`. (For HUB nodes, `on_scan_requested(is_hub)` keeps FSM in `OPERATIONAL` state).
  - *Logical-level retries*: When MAC delivery succeeds (`ESP_NOW_SEND_SUCCESS`) but Logical ACK times out, `TxManager` retries `logical_ack_retries` times. With `logical_ack_retries = 0`, it immediately returns `ESP_ERR_TIMEOUT` after 1 ACK timeout (500ms), leaving command queuing and high-level retries to `CommandManager`.

## 9. Optional Application-Layer Encryption (AEAD / AES-128-GCM) in `MessageCodec`
- **Category**: Future Feature / Security Architecture
- **Description**: Hardware ESP-NOW encryption (LMK/PMK) is constrained by ESP32 hardware tables to a maximum of 6 encrypted peers. To support unlimited encrypted peers, encryption should be implemented at the application layer inside `MessageCodec`.
- **Proposed Architecture / Solution**:
  1. Introduce an optional `ICryptoProvider` interface injected into `MessageCodec`.
  2. Implement an `AesGcmCryptoProvider` using ESP-IDF's hardware-accelerated `mbedTLS` (AES-128-GCM or ChaCha20-Poly1305).
  3. Encrypt payload and attach MAC tag / sequence nonce during `encode()`; verify and decrypt during `decode()`.
  4. Keeps `espnow_manager` hardware-agnostic while enabling production-grade security and unlimited peer scaling.
