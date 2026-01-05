# Runbook — Auto-start on Boot (Docker-only) (v8)

Sentient v8 services run in Docker containers. Auto-start on reboot is handled via Docker **restart policies** (no systemd units required for Sentient services).

## 1) Ensure containers have restart policies

- Room template services use `restart: unless-stopped`.
- Shared Traefik stack uses `restart: unless-stopped`.

## 2) Start stacks once

Docker only restarts containers that already exist, so bring stacks up at least once:

Shared proxy:

```bash
cd infra/compose/shared
docker compose up -d
```

Room (example):

```bash
cd infra/rooms/<room_id>
docker compose --env-file .env up -d --build
```

## 3) Verify after reboot

```bash
docker ps
docker compose ps
```

## Notes

- The Docker daemon itself is a host service; that’s the only required host-level dependency for container auto-start.

