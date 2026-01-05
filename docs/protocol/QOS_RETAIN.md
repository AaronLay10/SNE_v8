# MQTT QoS + Retain Policy (v8)

This document defines the default QoS/retain rules for v8 MQTT topics.

This is a **policy** document (operational behavior), separate from the topic and payload schemas.

Related:

- `docs/protocol/MQTT_TOPICS.md`
- `docs/protocol/PAYLOADS.md`

---

## Defaults (Per Room)

| Topic | Producer → Consumer | QoS | Retain | Notes |
|---|---|---:|---:|---|
| `room/{room_id}/device/{device_id}/cmd` | core/api → device | 1 | no | Commands must not be dropped; not retained to avoid replay on reconnect. |
| `room/{room_id}/device/{device_id}/ack` | device → core | 1 | no | Acks are event-like; not retained. |
| `room/{room_id}/device/{device_id}/heartbeat` | device → core | 0 | no | Periodic; missing a single heartbeat is tolerable on LAN (core uses 3s timeout). |
| `room/{room_id}/device/{device_id}/presence` | device/broker → core | 1 | yes | Retained ONLINE + retained LWT OFFLINE. |
| `room/{room_id}/device/{device_id}/state` | device → core | 1 | yes | Retained from day 1; keep payload compact and versioned. |
| `room/{room_id}/device/{device_id}/telemetry` | device → core | 0 | no | High volume; best-effort. |
| `room/{room_id}/core/heartbeat` | core → tools | 0 | no | Periodic health. |
| `room/{room_id}/core/status` | core → tools | 1 | yes | Retained status snapshot (pause state, broker outage, counts). |
| `room/{room_id}/core/fault` | core → tools | 1 | yes | Retained last known fault/incident for UIs/notify. |
| `room/{room_id}/core/dispatch` | tools → core | 1 | no | Commissioning/control plane; not retained to avoid replay. |
| `room/{room_id}/core/control` | tools → core | 1 | no | Ops control plane (pause/resume dispatch); not retained. |
| `room/{room_id}/core/device/{device_id}/fault` | core → tools | 1 | yes | Retained device fault/incident (offline, auth failures, safety blocks). |
| `room/{room_id}/core/device/{device_id}/status` | core → tools | 1 | yes | Retained computed health status for UIs/tools. |
| `room/{room_id}/audio/cue` | core/api → osc-bridge | 1 | no | Cue requests are event-like; do not retain to avoid replay after restart. |
| `room/{room_id}/audio/ack` | osc-bridge → tools/core | 1 | no | Ack is event-like; not retained. |
| `room/{room_id}/audio/fault` | osc-bridge → tools | 1 | yes | Retained last known OSC delivery fault for notify/UIs. |

---

## Notes

- Heartbeat is QoS 0 by design; presence/LWT + timeout handles disconnects without broker backpressure.
- Commands are never retained to avoid replay on reconnect.
