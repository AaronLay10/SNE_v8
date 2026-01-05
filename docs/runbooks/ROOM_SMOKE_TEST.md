# Runbook — Room Smoke Test (v8)

Goal: quickly validate a room stack’s control plane (API → MQTT → core) and optional OSC cue publishing.

## Prereqs

- A room stack is up (`docs/runbooks/ROOM_BRINGUP.md`)
- `sentient-api` is reachable (room VLAN IP + port)
- If `API_TOKEN` is set, export it or keep it in the room `.env`

## Run

From repo root:

```bash
ROOM_ID=room1
./scripts/room-smoke-test.sh "${ROOM_ID}"
```

Or specify the API URL:

```bash
./scripts/room-smoke-test.sh room1 http://10.10.1.2:8080
```

## Optional: validate OSC delivery without real SCS

1. Set `SCS_HOST=scs-sim` in the room `.env`
2. Bring up the simulator:

```bash
cd "infra/rooms/${ROOM_ID}"
docker compose --env-file .env --profile dev up -d --build scs-sim
```

3. Re-run the smoke test and watch logs:

```bash
docker logs -f "${STACK_ID}_scs_sim"
```

## Optional: validate safety latch + reset (dev only)

1) Start the room stack with `controller-sim` enabled.

2) Set these in the room `.env` (dev only) and restart `controller-sim`:

- `SIM_SAFETY_KIND=FAULT`
- `SIM_SAFETY_LATCHED=true`
- `SIM_TRIGGER_FAULT_AFTER_MS=2000`

3) Wait ~2 seconds. Expected:

- `core/status.dispatch_paused_reason` becomes `SAFETY_LATCHED`
- `core/status.safety_latched_since_unix_ms` is set

4) Reset the latch (TECH/Admin):

```bash
curl -sS -X POST "http://<room_ip>:8080/v8/room/<room_id>/safety/reset/request" \
  -H "Authorization: Bearer <JWT or API_TOKEN (TECH/ADMIN)>" \
  -H "Content-Type: application/json" \
  -d '{"reason":"dev smoke test"}'

curl -sS -X POST "http://<room_ip>:8080/v8/room/<room_id>/safety/reset/confirm" \
  -H "Authorization: Bearer <JWT or API_TOKEN (TECH/ADMIN)>" \
  -H "Content-Type: application/json" \
  -d '{"reset_id":"<reset_id_from_request>"}'
```
