# Runbook — Reverse Proxy (Traefik) (v8)

Sentient v8 uses a shared reverse proxy (`traefik`) for internal host-based routing on the admin LAN.

Status: TLS termination is enabled on the `websecure` entrypoint (443) with HTTP→HTTPS redirect.

TLS setup: `docs/runbooks/TLS.md`.

## Shared services (infra/compose/shared)

Configured hostnames (set in `infra/compose/shared/.env`):

- `TRAEFIK_DASHBOARD_HOSTNAME` → Traefik dashboard (admin LAN only)
- `GRAFANA_HOSTNAME` → Grafana
- `AUTH_HOSTNAME` → `sentient-auth`
- `TECH_UI_HOSTNAME` → Technical UI
- `CREATIVE_UI_HOSTNAME` → Creative UI
- `GM_UI_HOSTNAME` → Gamemaster UI

Bring up:

```bash
cd infra/compose/shared
docker compose --env-file .env.local up -d
```

## Room APIs (infra/rooms/<room_id>)

Each room stack’s `sentient-api` can be routed by hostname:

- `API_PUBLIC_HOSTNAME=api.<room>.sentientengine.ai`

Traefik must be able to reach the room API container via the shared `sentient-admin` Docker network.

Note: the room compose template uses `STACK_ID` in Traefik router/service names to avoid collisions when multiple room stacks are running.

## DNS

For internal access, add A-records on the UDM Pro:

- `api.<room>.sentientengine.ai` → R710 admin LAN IP (Traefik)
- `grafana.sentientengine.ai` → R710 admin LAN IP (Traefik)
- `auth.sentientengine.ai` → R710 admin LAN IP (Traefik)

See `docs/runbooks/UDM_PRO_DNS.md`.
