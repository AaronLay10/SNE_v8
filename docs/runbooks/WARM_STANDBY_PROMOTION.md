# Runbook — Warm Standby Promotion (Manual) (v8)

Warm standby is **manual promotion**: no automatic failover. The goal is to keep a standby stack ready, then deliberately promote it after verifying the room is safe.

This runbook assumes:

- A **primary** stack and a **standby** stack exist for the same `ROOM_ID`
- The standby `sentient-core` starts with `CORE_DISPATCH_ENABLED=false`

## 1) Create Primary + Standby Directories

```bash
scripts/new-room-standby.sh room1
```

Edit both:

- `infra/rooms/room1-primary/.env`
- `infra/rooms/room1-standby/.env`

Generate Mosquitto password files:

```bash
scripts/mosquitto-make-passwd.sh infra/rooms/room1-primary/.env
scripts/mosquitto-make-passwd.sh infra/rooms/room1-standby/.env
```

Bring up both:

```bash
(cd infra/rooms/room1-primary && docker compose --env-file .env up -d --build)
(cd infra/rooms/room1-standby && docker compose --env-file .env up -d --build)
```

## 2) Critical Decision: How Controllers Reach the MQTT Broker

Promotion only works if controllers can reach the broker on the promoted stack.

Locked decision for v8: controllers connect to a **per-room broker hostname**:

- `mqtt.<room>.sentientengine.ai` (example: `mqtt.clockwork.sentientengine.ai`)

This hostname must resolve (within that room’s VLAN / split-horizon DNS) to the room’s active broker endpoint.

Recovery/promotion is always **manual** (safety-first; no automatic failover).

Chosen for v8: **Option B (manual DNS switch)** on the UDM Pro after safety verification.

Implementation options (for reference) for what the hostname points to:

- **Option A: Static host IP (no failover)** — `mqtt.<room>` points to the primary broker IP. If the broker is down, the room must pause and notify (manual repair required).
- **Option B: Manual DNS switch (recommended for standby)** — `mqtt.<room>` points to primary normally; during promotion an operator repoints it to standby after verifying safety.
- **Option C: VIP move (manual)** — `mqtt.<room>` points at a VIP; an operator moves the VIP to the standby host during promotion.

If `mqtt.<room>` cannot be repointed (Option A), standby promotion will be “core-only” (useful for logs/DB) but controllers will remain bound to the primary broker endpoint.

## 3) Promotion Procedure (Manual)

### 3.1 Put the room in a safe state

- Ensure all active motion/actuators are stopped and any safety interlocks are satisfied.
- If the primary is flapping, shut it down to prevent “dual control”.

```bash
(cd infra/rooms/room1-primary && docker compose --env-file .env down)
```

### 3.2 Ensure standby stack is up

```bash
(cd infra/rooms/room1-standby && docker compose --env-file .env up -d)
```

### 3.3 Enable dispatch on standby core

Edit `infra/rooms/room1-standby/.env`:

- Set `CORE_DISPATCH_ENABLED=true`
- Keep `CORE_CRITICAL_ARMED=false` until safety checks are complete (then set true if needed)

Then recreate only `sentient-core`:

```bash
(cd infra/rooms/room1-standby && docker compose --env-file .env up -d --force-recreate sentient-core)
```

### 3.4 Verify end-to-end dispatch

Use `scripts/core-dispatch.sh` against the **standby** broker endpoint (VIP/DNS/port):

```bash
MQTT_HOST=localhost MQTT_PORT=1884 \
MQTT_USERNAME=sentient MQTT_PASSWORD=... \
scripts/core-dispatch.sh --room room1 --device sim1 --action SET --params '{"note":"promotion smoke test"}'
```

Confirm:

- Core receives `core/dispatch`, publishes device command, sees `ack` + `complete`.
- Device/controller reacts.

## 4) Demotion / Failback

- Disable standby dispatch first (`CORE_DISPATCH_ENABLED=false` + recreate core).
- Bring primary back up and re-verify before re-enabling dispatch on primary.

## 5) Keeping Standby Current (Data/Config Sync)

Warm standby is only useful if it matches the primary’s configuration/version.

Recommended approach for v8:

- **Version sync:** keep both hosts/dirs on the same repo commit/tag (pull updates in maintenance windows).
- **Config sync:** keep the room `.env` files aligned (except bind ports/IPs if needed).
- **Broker auth:** regenerate Mosquitto `passwd` after any MQTT credential change:
  - `scripts/mosquitto-make-passwd.sh infra/rooms/<room>-primary/.env`
  - `scripts/mosquitto-make-passwd.sh infra/rooms/<room>-standby/.env`
- **DB state (optional but recommended):** periodically dump the primary room DB and restore into standby during maintenance:
  - Backup: `scripts/backup-room-db.sh <room>`
  - Restore (standby): `scripts/restore-room-db.sh <room-standby> <backup.sql.gz>`

During promotion, treat DB restore as optional: the priority is safe pause, verified controller connectivity, then controlled re-arm.
