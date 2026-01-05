# Runbook — UIs (Tech / Creative / Gamemaster) (v8)

Sentient v8 includes three web UIs, all deployed as Docker containers behind Traefik:

- Technical UI: `tech.sentientengine.ai`
- Creative UI: `creative.sentientengine.ai`
- Gamemaster UI: `mythra.sentientengine.ai`

## 1) Bring up shared stack

```bash
cd infra/compose/shared
cp .env.example .env.local
docker compose --env-file .env.local up -d --build
```

Ensure your UDM Pro internal DNS has A-records pointing these hostnames at the R710 admin LAN IP.

## 2) Login

All UIs use `sentient-auth` (`auth.sentientengine.ai`) to obtain a JWT.

Bootstrap + login flow: `docs/runbooks/AUTH.md`.

## 3) Room access

Each UI requires:

- Room API base URL (example): `https://api.clockwork.sentientengine.ai`
- Room ID (example): `clockwork`

Room API setup + routing: `docs/runbooks/ROOM_API.md` and `docs/runbooks/REVERSE_PROXY.md`.

## Notes

- These UIs are MVP scaffolds intended to be expanded (polish, RBAC gating per action, audit UX).
- TLS termination is enabled in Traefik; operator/admin devices must trust the internal Root CA (`docs/runbooks/TLS.md`).
