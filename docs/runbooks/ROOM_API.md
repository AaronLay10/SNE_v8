# Runbook — Room API (sentient-api) (v8)

Each room stack includes a room-scoped HTTP API service (`sentient-api`) that:

- publishes MQTT control messages (`core/dispatch`, `core/control`)
- publishes OSC audio cue messages (`audio/cue`) for the `osc-bridge`
- serves cached `core/status`, `core/fault`, and device status/faults

## Configure

In the room `.env`:

- `API_BIND_IP` / `API_PORT` (bind to the room VLAN IP to avoid port conflicts across rooms)
- optional `API_TOKEN` (room-local break-glass; requires `Authorization: Bearer <token>`)
- optional `SENTIENT_JWT_SECRET` (accept JWTs issued by shared `sentient-auth`)

## Endpoints (room-scoped)

- `GET /health`
- `GET /v8/room/{room_id}/ws` (WebSocket stream)
- `GET /v8/room/{room_id}/core/status`
- `GET /v8/room/{room_id}/core/fault`
- `GET /v8/room/{room_id}/devices`
- `GET /v8/room/{room_id}/devices/{device_id}/status`
- `GET /v8/room/{room_id}/devices/{device_id}/fault`
- `GET /v8/room/{room_id}/events?limit=100` (requires DB)
- `POST /v8/room/{room_id}/dispatch`
- `POST /v8/room/{room_id}/control`
- `POST /v8/room/{room_id}/safety/reset/request`
- `POST /v8/room/{room_id}/safety/reset/confirm`
- `GET /v8/room/{room_id}/graphs`
- `POST /v8/room/{room_id}/graphs`
- `GET /v8/room/{room_id}/graphs/active`
- `POST /v8/room/{room_id}/graphs/activate`
- `POST /v8/room/{room_id}/audio/cue`
- `GET /v8/room/{room_id}/audio/fault`
- `GET /v8/room/{room_id}/audio/ack`

## Example

Dispatch (non-critical):

```bash
curl -sS -X POST "http://<room_ip>:8080/v8/room/<room_id>/dispatch" \
  -H "Content-Type: application/json" \
  -d '{"device_id":"sim1","action":"SET","parameters":{"op":"noop"},"safety_class":"NON_CRITICAL"}'
```

Pause/resume dispatch:

```bash
curl -sS -X POST "http://<room_ip>:8080/v8/room/<room_id>/control" \
  -H "Content-Type: application/json" \
  -d '{"op":"PAUSE_DISPATCH"}'
```

Audio cue (to `osc-bridge`):

```bash
curl -sS -X POST "http://<room_ip>:8080/v8/room/<room_id>/audio/cue" \
  -H "Content-Type: application/json" \
  -d '{"schema":"sentient-v8","room_id":"<room_id>","cue_id":"test","correlation_id":"00000000-0000-0000-0000-000000000000","address":"/scs/test","args":[{"type":"STRING","value":"hello"}],"issued_at_unix_ms":0}'
```
