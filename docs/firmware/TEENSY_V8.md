# Teensy Firmware — v8 MQTT Integration (Permanent)

This document defines how Teensy firmware integrates with Sentient v8 **without legacy compatibility modes**.

Authoritative protocol docs:

- `docs/protocol/MQTT_TOPICS.md`
- `docs/protocol/PAYLOADS.md`
- `docs/protocol/AUTH_HMAC.md`
- `docs/protocol/QOS_RETAIN.md`

Reference implementation scaffolding in this repo:

- `hardware/Custom Libraries/ArduinoMQTT/` (MQTT client with QoS 1 support)
- `hardware/Custom Libraries/SentientCrypto/` (SHA256 + HMAC-SHA256)
- `hardware/Custom Libraries/SentientV8/` (v8 MQTT wrapper)
- `hardware/templates/v8/TeensyV8Template/TeensyV8Template.ino` (example sketch)

---

## 1) Device Identity Model (v8)

In v8, each firmware instance is addressed by a `device_id` and has its own topics:

- `room/{room_id}/device/{device_id}/cmd`
- `room/{room_id}/device/{device_id}/ack`
- `room/{room_id}/device/{device_id}/state`
- `room/{room_id}/device/{device_id}/telemetry`
- `room/{room_id}/device/{device_id}/heartbeat`
- `room/{room_id}/device/{device_id}/presence`

### Teensy controlling multiple outputs/sensors

One Teensy MCU may control many outputs/sensors.

**Permanent v8 default (Option 2):** model each logical sub-device with its own `device_id`.

- Example: `chemical_rfid_a`, `chemical_engine_block_actuator`
- This keeps each device’s state/acks/presence independent and makes orchestration simpler.

**Naming rule:** `device_id` MUST be unique within a room. Recommended convention:

- `device_id = "{controller_id}_{subdevice_id}"`

If a device truly has only one logical unit, `subdevice_id` can equal `controller_id` (ex: `clock_clock`), but prefer a meaningful suffix when possible.

If a single Teensy needs to expose multiple `device_id`s, it requires **multiple MQTT connections** (one per `device_id`) to preserve correct LWT semantics.

Commands use typed `action` + JSON `parameters`. The `parameters` schema is **device-specific** and should be documented per controller.

### `parameters.op` (required convention)

For permanent v8, controllers SHOULD require a string `parameters.op` that names the operation to perform (device-specific).

- Example: `action = "SET"`, `parameters = {"op":"lock"}` or `{"op":"set","on":true}`
- This avoids ambiguous “command string” bridges and keeps commands evolvable.

---

## 2) Presence + LWT

Each logical `device_id` should publish a retained ONLINE message and configure LWT retained OFFLINE:

- Topic: `room/{room_id}/device/{device_id}/presence`
- Payload: `Presence`
- QoS: 1
- Retain: true

If a single Teensy exposes multiple `device_id`s, that requires multiple MQTT connections (one per `device_id`) to preserve correct LWT semantics.

---

## 2.1) Network Bring-up (Teensy 4.1)

The v8 MQTT wrapper assumes the network stack is already initialized.

Typical pattern (DHCP):

- Call `teensyMAC(mac)` and `Ethernet.begin(mac)` once during `setup()`.

## 2.2) Broker Addressing (Per-Room Hostnames)

Controllers should connect using the **per-room broker hostname**:

- `mqtt.<room>.sentientengine.ai` (example: `mqtt.clockwork.sentientengine.ai`)

The v8 firmware library supports both:

- `Config.brokerHost` (recommended)
- `Config.brokerIp` (fallback)

## 3) Heartbeat

- Topic: `room/{room_id}/device/{device_id}/heartbeat`
- Payload: `Heartbeat`
- QoS: 0
- Retain: false

Recommended: publish every ~1s.

---

## 4) Commands + Acks

### Commands

- Subscribe topic: `room/{room_id}/device/{device_id}/cmd`
- Payload: `CommandEnvelope`

Firmware MUST:

- Validate `schema`, `room_id`, `device_id`
- Verify HMAC (when enforced) per `docs/protocol/AUTH_HMAC.md`

### Acks

- Publish topic: `room/{room_id}/device/{device_id}/ack`
- Payload: `CommandAck`
- QoS: 1

Required behavior:

- Publish `ACCEPTED` quickly
- Publish `COMPLETED` when action is finished
- Publish `REJECTED` on invalid/unsafe/unauthorized commands (with `reason_code`)

Idempotency:

- If the same `command_id` is received again (retry), firmware should not re-execute; it should re-ack consistently.

The reference v8 library in this repo (`hardware/Custom Libraries/SentientV8/`) includes a small in-memory idempotency cache and will re-ack duplicates by `command_id`.

---

## 5) State (Retained)

- Publish topic: `room/{room_id}/device/{device_id}/state`
- Payload: `DeviceState`
- QoS: 1
- Retain: true

Keep `state` small and stable (booleans, small ints, enums, etc.).

## 6) Telemetry (Optional)

- Publish topic: `room/{room_id}/device/{device_id}/telemetry`
- Payload: `DeviceTelemetry`
- QoS: 0
- Retain: false

Use telemetry for periodic sensor/metrics streams (ex: lux, temperatures, voltages).
