# Runbook — Observability (Grafana + Loki) (v8)

Sentient v8 observability is containerized and split into:

- **Shared observability stack** (`infra/compose/shared`): Grafana + Loki + Promtail
- **Per-room stacks** (`infra/rooms/<room_id>`): core/mqtt/db/api/notify/osc-bridge

## 1) Bring up shared observability

```bash
cd infra/compose/shared
docker compose up -d
```

If Docker access fails, see `docs/runbooks/DOCKER_ACCESS.md`.

Grafana:

- URL: `http://<host>:3000`
- Default credentials: `admin` / `admin` (override via env)
- Provisioned dashboard: `Sentient v8 — Logs Overview` (filters by `compose_project` / `compose_service`)
- Optional: route via Traefik hostname (see `docs/runbooks/REVERSE_PROXY.md`)

## 2) Logs (Loki)

Promtail scrapes Docker container logs and pushes them to Loki.

In Grafana:

- Data source `Loki` is provisioned automatically.
- Explore → query `{compose_project="<your_room_stack_id>"}`.

## 3) Admin network

Room stacks attach to the shared `sentient-admin` network to allow central access for logs/dashboards.

See:

- `infra/compose/room-template/docker-compose.yml`
