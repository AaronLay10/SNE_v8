# Sentient Core — Minimal Graph JSON (v8)

This is an **initial, file-based** graph format to let `sentient-core` run a small sequence of cues without the UI/API stack.

It is intentionally minimal and will evolve.

## Enable

Set:

- `CORE_GRAPH_PATH=/path/to/graph.json`
- `CORE_GRAPH_AUTOSTART=true`

## DB-backed graphs (recommended)

If `CORE_GRAPH_PATH` is not set and the room DB is enabled (`CORE_DB_ENABLED=true`), `sentient-core` will load the active graph from the room DB tables:

- `graphs` (versions)
- `graph_active` (active_version pointer)

Use the room API to upload and activate graphs:

- `POST /v8/room/{room_id}/graphs`
- `POST /v8/room/{room_id}/graphs/activate`

Note: v8 MVP loads the active graph on core startup (restart `sentient-core` to apply changes).

## Format

```json
{
  "schema": "v8",
  "room_id": "clockwork",
  "start": "boot",
  "nodes": {
    "boot": { "kind": "NOOP", "next": "cue1" },
    "wait_ready": {
      "kind": "WAIT_STATE_EQUALS",
      "device_id": "keys_green_key_box",
      "pointer": "/ready",
      "equals": true,
      "timeout_ms": 10000,
      "next": "cue1"
    },
    "cue1": {
      "kind": "DISPATCH",
      "device_id": "lever_boiler_main",
      "action": "SET",
      "parameters": { "op": "noop" },
      "safety_class": "NON_CRITICAL",
      "next": "done"
    },
    "fork": { "kind": "NOOP", "next": ["cue1", "delay_500"] },
    "delay_500": { "kind": "DELAY", "ms": 500, "next": "done" },
    "done": { "kind": "NOOP" }
  }
}
```

## Notes

- `start` may be a string or an array of node ids (parallel starts).
- `next` may be a string or an array of node ids (fan-out / parallel paths).
- `sentient-core` uses the same command pipeline as the MQTT `core/dispatch` topic (HMAC keys still required).
- If the broker disconnects, dispatch pauses; graph execution stops until manually resumed.
- `WAIT_STATE_EQUALS` evaluates against the last retained `DeviceState.state` JSON for that device (via JSON pointer).
