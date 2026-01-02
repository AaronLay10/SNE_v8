# MQTT Topic Layout (v8)

This is the canonical topic layout for Sentient v8. Topics are per-room and per-device.

## Device Topics

- Commands (core → device): `room/{room_id}/device/{device_id}/cmd`
- Acks / completion (device → core): `room/{room_id}/device/{device_id}/ack`
- State (device → core): `room/{room_id}/device/{device_id}/state`
- Telemetry (device → core): `room/{room_id}/device/{device_id}/telemetry`
- Heartbeat (device → core): `room/{room_id}/device/{device_id}/heartbeat`

## Core Topics (optional)

- Core health: `room/{room_id}/core/heartbeat`
- Core faults: `room/{room_id}/core/fault`

