# Runbook — Backup/Restore Test (v8)

Sentient v8 requires periodic restore testing. This runbook defines a container-first process that can be executed without installing DB tooling on the host.

Scope (current):

- Room TimescaleDB (`infra/rooms/<room_id>/timescaledb`)
- Shared Auth DB (`infra/compose/shared/auth-db`)

## 1) Backup (room DB)

From the repo root:

```bash
ROOM_ID=room1
./scripts/backup-room-db.sh "${ROOM_ID}"
```

This produces a timestamped `.sql.gz` under `backups/room-db/<room_id>/`.

## 2) Restore test into a new room stack (recommended)

Create a fresh “restore test” room directory (different compose project name), then restore into its DB:

```bash
RESTORE_ROOM_ID=room1-restoretest
./scripts/new-room.sh "${RESTORE_ROOM_ID}"
./scripts/mosquitto-make-passwd.sh "infra/rooms/${RESTORE_ROOM_ID}/.env"
(cd "infra/rooms/${RESTORE_ROOM_ID}" && docker compose --env-file .env up -d timescaledb)

./scripts/restore-room-db.sh "${RESTORE_ROOM_ID}" "backups/room-db/room1/<backup-file>.sql.gz"
```

Then bring up the rest of the stack and verify:

- DB contains `events` and expected rows
- Core can connect and continues persisting new events

## 3) Backup (auth DB)

```bash
./scripts/backup-auth-db.sh
```

Outputs under `backups/auth-db/`.

## 4) Restore test (auth DB)

Restoring auth DB is disruptive if you overwrite production; restore test should be done using a separate compose project or during a maintenance window.

```bash
./scripts/restore-auth-db.sh "backups/auth-db/<backup-file>.sql.gz"
```

## 5) Off-host storage

Backups should be copied off-host after creation (NAS/S3/etc). The exact destination is an ops decision; the repo scripts intentionally stop at producing portable dump files.

