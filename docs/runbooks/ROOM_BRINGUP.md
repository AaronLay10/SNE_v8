# Runbook — Room Bring-Up (v8)

This runbook brings up one room stack (MQTT + DB + core + osc-bridge) and verifies end-to-end command dispatch using the MQTT `core/dispatch` control plane.

Prereqs:

- Docker + Docker Compose installed
- Your SSH user can access Docker (re-login, `sudo`, or `sg docker`; see `docs/runbooks/DOCKER_ACCESS.md`)
- Shared stack is up first (creates the external `sentient-admin` network used by room stacks):
  - `cd infra/compose/shared && docker compose --env-file .env.local up -d`

If Docker access fails, see `docs/runbooks/DOCKER_ACCESS.md`.

---

## 1) Create Room Directory from Template

```bash
./scripts/new-room.sh room1
```

Edit `infra/rooms/${ROOM_ID}/.env`:

- Set `ROOM_ID`
- Set `STACK_ID` (usually the same as `ROOM_ID`)
- Set `MQTT_PUBLIC_HOSTNAME` (ex: `mqtt.clockwork.sentientengine.ai`) for controller configuration (UDM Pro DNS record per room VLAN; see `docs/runbooks/UDM_PRO_DNS.md`)
- Set `MQTT_USERNAME` / `MQTT_PASSWORD` (per-room shared creds)
- Set `POSTGRES_PASSWORD`
- Set `SCS_HOST` / `SCS_OSC_PORT`
- Set `API_BIND_IP` / `API_PORT` (optional; see `docs/runbooks/ROOM_API.md`)
- (Optional) Set `CORE_CONTROL_TOKEN` to require a token for core MQTT control operations (pause/resume/reset)

---

## 2) Create Mosquitto Password File

From the room dir:

```bash
cd "infra/rooms/${ROOM_ID}"
../../scripts/mosquitto-make-passwd.sh ./.env
```

---

## 3) Bring Up the Stack

```bash
cd "infra/rooms/${ROOM_ID}"
docker compose --env-file .env up -d --build
```

DB note: first boot initializes tables from `infra/compose/room-template/db/init/`.

Optional dev controller simulator:

```bash
docker compose --env-file .env --profile dev up -d --build controller-sim
```

Optional dev SCS simulator (to validate OSC delivery without a real SCS host):

1) Set `SCS_HOST=scs-sim` in the room `.env`
2) Bring up the simulator:

```bash
docker compose --env-file .env --profile dev up -d --build scs-sim
```

You should see logs from `scs-sim` when cues are delivered.

---

## 4) Configure Device HMAC Keys (Prototype)

For early testing, provide keys via env:

- `DEVICE_HMAC_KEYS_JSON` for core (device_id → hex key)
- `SIM_DEVICE_HMAC_KEY_HEX` for controller-sim (hex key)
- `ENFORCE_CMD_AUTH=true` for controller-sim

Recommended: generate per-device keys with `docs/runbooks/DEVICE_KEY_PROVISIONING.md`.

## 4.1) (Optional) Device Safety Registry

The core can treat some devices as safety-critical even if a tool submits `NON_CRITICAL`:

- Preferred: populate the room DB table `devices` (created at first boot)
- Optional override: set `DEVICE_SAFETY_CLASS_JSON` in `.env`

---

## 5) Dispatch a Test Command (Tools → Core → Device)

```bash
MQTT_HOST=localhost MQTT_PORT=1883 \
MQTT_USERNAME=sentient MQTT_PASSWORD=... \
scripts/core-dispatch.sh --room "${ROOM_ID}" --device sim1 --action SET --params '{"note":"smoke"}'
```

Verify:

- Core logs show `published device command` and `command completed`
- Device logs show `received command`

---

## 6) Smoke Test (API + Control Plane)

Run:

```bash
./scripts/room-smoke-test.sh "${ROOM_ID}"
```

See `docs/runbooks/ROOM_SMOKE_TEST.md`.
