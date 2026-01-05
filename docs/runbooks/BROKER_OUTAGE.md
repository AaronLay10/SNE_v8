# Runbook — Room MQTT Broker Outage (v8)

If a room’s MQTT broker is down/unreachable, this is a **room crash / safety incident**.

v8 policy:

- **No automatic failover**.
- Room must **pause** and require **manual, safety-first recovery**.

## What Sentient Does Automatically

- `sentient-core` detects broker disconnects and **pauses dispatch** (blocks new commands).
- After the broker is reachable again, `sentient-core` publishes a retained incident to:
  - `room/{room_id}/core/fault` (`kind = "BROKER_OUTAGE"`)
- On device liveness changes and dispatch failures, `sentient-core` also publishes retained device faults to:
  - `room/{room_id}/core/device/{device_id}/fault`
- Dispatch remains paused until an operator manually resumes it.

## Operator Procedure (Per Room)

### 1) Put the room in a safe state

- Stop motion/actuators and verify interlocks.
- Do not attempt “quick retries” that could re-enable unsafe behavior.

### 2) Identify the room stack directory

Room stacks live under:

- `infra/rooms/<room_id>/` (single stack) or
- `infra/rooms/<room_id>-primary/` and `infra/rooms/<room_id>-standby/` (warm standby)

### 3) Diagnose broker status

From the room stack directory:

```bash
docker compose ps
docker compose logs --tail=200 mqtt
```

If the broker container is down, bring it back:

```bash
docker compose up -d mqtt
```

If configuration/auth was recently changed, verify Mosquitto auth files exist:

- `mosquitto/passwd`
- `mosquitto/mosquitto.conf`

### 4) Verify broker connectivity (controllers + core)

- Controllers should reconnect to `mqtt.<room>.sentientengine.ai`.
- `sentient-core` should reconnect and emit a `BROKER_OUTAGE` retained fault.

### 5) Manually resume dispatch (required)

Once the room is confirmed safe and broker is stable, resume dispatch:

```bash
MQTT_USERNAME=sentient MQTT_PASSWORD=... \
scripts/core-control.sh --room <room_id> --op RESUME_DISPATCH
```

If you need to pause dispatch manually:

```bash
MQTT_USERNAME=sentient MQTT_PASSWORD=... \
scripts/core-control.sh --room <room_id> --op PAUSE_DISPATCH
```

## Notes

- This runbook only covers MQTT broker outages. Safety FAULT/E_STOP incidents require their own recovery procedure.
