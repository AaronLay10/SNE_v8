# MQTT Topic Layout (v8)

This is the canonical topic layout for Sentient v8. Topics are per-room and per-device.

## Device Topics

- Commands (core → device): `room/{room_id}/device/{device_id}/cmd`
- Acks / completion (device → core): `room/{room_id}/device/{device_id}/ack`
- State (device → core): `room/{room_id}/device/{device_id}/state`
- Telemetry (device → core): `room/{room_id}/device/{device_id}/telemetry`
- Heartbeat (device → core): `room/{room_id}/device/{device_id}/heartbeat`
- Presence (device/broker → core): `room/{room_id}/device/{device_id}/presence`

## Core Topics (optional)

- Core health: `room/{room_id}/core/heartbeat`
- Core status: `room/{room_id}/core/status`
- Core faults: `room/{room_id}/core/fault`
- Core control (tools → core): `room/{room_id}/core/control`
- Dispatch request (tools → core): `room/{room_id}/core/dispatch`
- Device faults (core → tools/UIs): `room/{room_id}/core/device/{device_id}/fault`
- Device status (core → UIs/tools): `room/{room_id}/core/device/{device_id}/status`

## Audio / OSC Topics

- Audio cue (core/api → osc-bridge): `room/{room_id}/audio/cue`
- Audio ack (osc-bridge → tools/core): `room/{room_id}/audio/ack`
- Audio faults (osc-bridge → tools/UIs/notify): `room/{room_id}/audio/fault`

See `docs/protocol/QOS_RETAIN.md` for QoS/retain policy.
