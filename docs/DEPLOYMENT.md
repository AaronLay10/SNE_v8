# Deployment (Initial Scaffolding)

This describes the current repo scaffolding for per-room stacks and shared services. It is not production-hardened yet.

## Shared services (reverse proxy)

From repo root:

```bash
docker compose -f infra/compose/shared/docker-compose.yml up -d
```

## Create a room stack

```bash
./scripts/new-room.sh room1
cd infra/rooms/room1
```

Edit `.env` (especially `MQTT_USERNAME`, `MQTT_PASSWORD` (unique per room), `POSTGRES_PASSWORD`, `SCS_HOST`, `SCS_OSC_PORT`), then:

```bash
../../scripts/mosquitto-make-passwd.sh .env
docker compose --env-file .env up -d
```

## Notes

- Default templates use Mosquitto + TimescaleDB and build `sentient-core` / `osc-bridge` from this repo.
- Network/VLAN firewalling is configured outside this repo (UDM Pro); see `todolist.md`.
