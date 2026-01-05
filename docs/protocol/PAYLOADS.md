# MQTT Payloads (v8)

All payloads are JSON.

## Schema Field

All messages include `schema: "v8"` to enable future evolution without ambiguity.

## Command Envelope

Implemented in `crates/sentient-protocol/src/lib.rs` as `CommandEnvelope`.

Minimum required fields:

- `schema`, `room_id`, `device_id`
- `command_id` (UUID)
- `correlation_id` (UUID)
- `sequence` (u64, per-device)
- `issued_at_unix_ms` (u64)
- `action` (`OPEN|CLOSE|MOVE|SET`)
- `parameters` (JSON object; can be `{}`)
- `safety_class` (`CRITICAL|NON_CRITICAL`)

### `parameters.op` (recommended convention)

For `action = "SET"`, devices SHOULD require `parameters.op` (string) to identify the device-specific operation to perform.

- Example: `{"op":"lock"}` or `{"op":"set","on":true}`

### Authentication (Controller Commands)

For real hardware deployments, commands are authenticated with HMAC-SHA256 using a per-device key.

- Field: `auth`
- Type: `CommandAuth`
- `alg`: `"HMAC-SHA256"`
- `kid`: optional key ID for rotation
- `mac_hex`: hex MAC over canonical signing bytes (canonicalization spec to be finalized)

## ACK / Completion

Implemented as `CommandAck`.

Status values:

- `ACCEPTED`
- `REJECTED`
- `COMPLETED`

## Heartbeat

Implemented as `Heartbeat`.

Notes:

- Heartbeats are periodic and also paired with MQTT LWT for disconnect detection.
- Default device offline timeout is 3s (configurable on the server).

## Presence (ONLINE/OFFLINE)

Topic: `room/{room_id}/device/{device_id}/presence`

Payload: `Presence`

Notes:

- Devices (or simulators) publish a retained `ONLINE` presence on startup.
- MQTT LWT publishes retained `OFFLINE` presence on unexpected disconnect.

## Device State (Device → Core)

Topic: `room/{room_id}/device/{device_id}/state`

Payload: `DeviceState`

Notes:

- Published as QoS 1 and retained (see `docs/protocol/QOS_RETAIN.md`).
- `state` is device-specific JSON; keep it small and stable.

## Device Telemetry (Device → Core)

Topic: `room/{room_id}/device/{device_id}/telemetry`

Payload: `DeviceTelemetry`

Notes:

- Published as QoS 0 and not retained (see `docs/protocol/QOS_RETAIN.md`).
- `telemetry` is device-specific JSON; use it for periodic metrics/sensors.

## Device Status (Core → Tools/UIs)

Topic: `room/{room_id}/core/device/{device_id}/status`

Payload example:

```json
{
  "schema": "v8",
  "room_id": "room1",
  "device_id": "doorA",
  "is_offline": false,
  "computed_at_unix_ms": 0,
  "last_heartbeat_at_unix_ms": 0,
  "last_presence_at_unix_ms": 0,
  "last_state_at_unix_ms": 0,
  "presence": "ONLINE"
}
```

## Core Status (Core → Tools/UIs)

Topic: `room/{room_id}/core/status`

Payload: `CoreStatus`

Notes:

- Published as QoS 1 and retained (see `docs/protocol/QOS_RETAIN.md`).
- Intended for dashboards and quick triage (paused state, broker outage, device counts).
- Includes `room_safety` (aggregated `SafetyState`) based on device-reported safety states.

## Core Dispatch Request (Tools → Core)

Topic: `room/{room_id}/core/dispatch`

Payload: `CoreDispatchRequest`

Notes:

- Intended for commissioning and early integration before the HTTP/WebSocket APIs exist.
- Core will sign and publish a `CommandEnvelope` to the target device (requires device HMAC key configured on core).
- Helper script: `scripts/core-dispatch.sh`

## Core Control Request (Tools → Core)

Topic: `room/{room_id}/core/control`

Payload: `CoreControlRequest`

Ops:

- `PAUSE_DISPATCH` (manual pause)
- `RESUME_DISPATCH` (manual resume; clears broker-outage pause)
- `RESET_SAFETY_LATCH` (TECH/Admin; clears core safety latch if all devices report SAFE)
- `START_GRAPH` (start graph execution)
- `STOP_GRAPH` (stop graph execution)
- `RELOAD_GRAPH` (reload active graph from DB; requires dispatch paused; denied if graph is running)

Helper script: `scripts/core-control.sh`

Optional security:

- If `CORE_CONTROL_TOKEN` is set on `sentient-core`, tools must include `parameters.token` matching it.

## Core Fault / Incident (Core → Tools/UIs)

Topic: `room/{room_id}/core/fault`

Payload: `CoreFault`

Notes:

- Published as QoS 1 and retained (see `docs/protocol/QOS_RETAIN.md`).
- Used for broker outage / dispatch paused incidents and future notify integration.

## Device Fault / Incident (Core → Tools/UIs)

Topic: `room/{room_id}/core/device/{device_id}/fault`

Payload: `CoreFault`

Notes:

- Published as QoS 1 and retained (see `docs/protocol/QOS_RETAIN.md`).
- Used for device offline/online transitions and future device-specific safety/auth incidents.

## Audio Cue (Core → OSC Bridge → SCS)

Published by the core (or API) for the per-room `osc-bridge` to deliver to SCS:

- Topic: `room/{room_id}/audio/cue`
- Payload: `OscCue`

Arguments are typed as a tagged union:

```json
{"type":"INT","value":123}
```

## Core Status (Core → Tools/UIs)

Topic: `room/{room_id}/core/status`

Payload: `CoreStatus`

Notes:

- `dispatch_paused_reason` indicates why dispatch is paused (manual pause, broker outage, safety latch).
- `broker_outage_since_unix_ms` is set when the broker disconnects and remains until manual resume.
- `safety_latched_since_unix_ms` is set when a safety latch is triggered and remains until explicit reset (`RESET_SAFETY_LATCH`).
- `graph_active_node` is the first active node id (for backwards-compatible dashboards).
- `graph_active_nodes` is the full set of active node ids (parallel paths).
- `graph_version` is populated when the graph was loaded from the room DB (`graphs`/`graph_active`).

## Audio Ack / Fault (OSC Bridge → Tools/UIs)

`osc-bridge` emits two MQTT messages for application-level delivery feedback:

- Topic: `room/{room_id}/audio/ack` (not retained)
- Payload: JSON:

```json
{
  "schema":"sentient-v8",
  "room_id":"clockwork",
  "cue_id":"intro_music",
  "correlation_id":"00000000-0000-0000-0000-000000000000",
  "status":"SENT",
  "attempts":1,
  "error":null,
  "observed_at_unix_ms":0
}
```

And for failures:

- Topic: `room/{room_id}/audio/fault` (QoS 1, retained)
- Payload: `CoreFault` with `kind = "OSC_SEND_FAILED"` and details including `cue_id`, `correlation_id`, `attempts`, `error`.
