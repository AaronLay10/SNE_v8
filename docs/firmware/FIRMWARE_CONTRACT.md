# Sentient v8 — Firmware Contract

This document is the **authoritative contract** for how controller firmware (Teensy/ESP32/Raspberry Pi agents) interfaces with Sentient Neural Engine v8 over MQTT.

Related specs:

- `docs/protocol/MQTT_TOPICS.md`
- `docs/protocol/PAYLOADS.md`
- `docs/protocol/AUTH_HMAC.md`
- `docs/firmware/HMAC_REFERENCE.md`
- `docs/firmware/TEENSY_V8.md`

---

## 1) Identity + Transport

Each controller firmware instance operates as a single MQTT “device”:

- `room_id` (string): room identifier (ex: `room1`)
- `device_id` (string): unique device identifier inside the room (ex: `chemical_rfid_a`)

Permanent v8 identity model (Option 2):

- A single Teensy may expose multiple logical sub-devices, each with its own v8 `device_id`.
- Recommended convention: `device_id = "{controller_id}_{subdevice_id}"` (room-unique).

Transport requirements:

- MQTT v5 connection to the **room-local broker** (one broker per room stack).
- Broker authentication: **username/password** (shared across the room; unique per room).
- Command authentication: **HMAC-SHA256 per device** (message authentication) — see `docs/protocol/AUTH_HMAC.md`.

---

## 2) Required MQTT Topics

Controllers MUST use the canonical v8 topic layout from `docs/protocol/MQTT_TOPICS.md`.

Minimum required topics (by controller firmware):

- Publish: `room/{room_id}/device/{device_id}/presence` (retained)
- Publish: `room/{room_id}/device/{device_id}/heartbeat`
- Publish: `room/{room_id}/device/{device_id}/ack`
- Subscribe: `room/{room_id}/device/{device_id}/cmd`

Optional (strongly recommended):

- Publish: `room/{room_id}/device/{device_id}/state` (on change; may be periodic)
- Publish: `room/{room_id}/device/{device_id}/telemetry` (periodic)

---

## 3) Presence + LWT (Online/Offline)

Controllers MUST implement presence using a retained ONLINE message + broker LWT retained OFFLINE:

1. Configure MQTT Last Will:
   - Topic: `room/{room_id}/device/{device_id}/presence`
   - Payload: `Presence` with `status = OFFLINE`
   - QoS: at least once
   - Retain: true
2. After a successful MQTT connect, publish retained ONLINE:
   - Topic: `room/{room_id}/device/{device_id}/presence`
   - Payload: `Presence` with `status = ONLINE`
   - QoS: at least once
   - Retain: true

Notes:

- Presence is the fastest “truth” for **unexpected disconnect**.
- The core also evaluates liveness using heartbeat timeouts (default 3s).

---

## 4) Heartbeat

Controllers MUST publish `Heartbeat` periodically (default target: **1s interval**).

Contract expectations:

- Include `observed_at_unix_ms` in the payload (controller’s best-effort wall clock).
- Include `uptime_ms` monotonic since boot.
- Include `firmware_version` (human readable).
- Include `safety_state` reflecting controller-local safety status (even if basic initially).

---

## 5) Commands + Acks

### 5.1 Command Receive

Controllers subscribe to:

- `room/{room_id}/device/{device_id}/cmd`

Payload is `CommandEnvelope`.

Permanent v8 convention (required by Sentient v8 firmware in this repo):

- `parameters.op` (string) MUST be present for `action = "SET"` and identifies the device-specific operation to perform.

On receipt the controller MUST:

1. Validate envelope fields:
   - `schema` is `v8`
   - `room_id` matches controller room
   - `device_id` matches controller device
2. If running with auth enforcement enabled:
   - Verify HMAC per `docs/protocol/AUTH_HMAC.md`
   - Reject if invalid/missing auth

### 5.2 Ack Behavior

Controllers MUST publish `CommandAck` to:

- `room/{room_id}/device/{device_id}/ack`

Ack behavior:

- Publish `ACCEPTED` as soon as the command is accepted for execution (target: < 50ms).
- Publish `REJECTED` if command is invalid, unsafe, unauthorized, or cannot be executed.
- Publish `COMPLETED` when the device has finished executing the command.

Recommended `reason_code` values for `REJECTED`:

- `AUTH_INVALID` (HMAC missing/invalid)
- `BAD_SCHEMA` / `BAD_ROOM` / `BAD_DEVICE`
- `UNSUPPORTED_ACTION`
- `INVALID_PARAMS`
- `BUSY`
- `SAFETY_BLOCKED`
- `INTERNAL_ERROR`

---

## 6) State + Telemetry (Recommended)

State:

- Publish on change to `room/{room_id}/device/{device_id}/state`.
- Keep payload compact and versioned (`schema: v8`).
- Include controller-local values that matter for orchestration (ex: `locked`, `position`, `fault`, etc.).

Telemetry:

- Publish periodic operational metrics to `room/{room_id}/device/{device_id}/telemetry`.
- Examples: temperatures, voltages, current draw, sensor raw values, RSSI, queue depth.

---

## 7) Safety Expectations

Controllers MUST implement a safe default stance:

- On boot: start in a known-safe state until a valid command is received.
- On internal FAULT: latch safe outputs and report `safety_state.kind = FAULT` (if possible).

Controllers SHOULD implement:

- A watchdog that returns the device to safe idle if firmware loops or stalls.
- A “maintenance/commissioning” mode where unsafe actions are blocked unless explicitly enabled.

---

## 8) Provisioning Inputs (Early Prototype)

Before the Technical UI exists, firmware configs must be provided out-of-band (file/build-time/env):

- `room_id`
- `device_id`
- MQTT broker host/port
- MQTT username/password (per room)
- Device HMAC key (per device)

Key rotation and provisioning workflows will be formalized later.
