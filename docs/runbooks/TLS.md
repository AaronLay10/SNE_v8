# TLS (Traefik Termination) — Sentient v8

Sentient v8 uses **Traefik TLS termination** for all HTTP services (UIs, auth, Grafana, room APIs).

Because these hostnames are typically **split-horizon/internal** (UDM Pro local DNS), the recommended production approach is an **internal Root CA** with operator devices configured to trust it.

## Generate internal CA + wildcard cert

Pick SANs. Minimum:

- `*.sentientengine.ai`
- `*.clockwork.sentientengine.ai` (and one wildcard per room)

Run:

```bash
TLS_WILDCARDS="*.sentientengine.ai,*.clockwork.sentientengine.ai" \
  ./scripts/tls-bootstrap-internal-ca.sh
```

Outputs (mounted into Traefik):

- `infra/compose/shared/traefik/certs/sentient.crt`
- `infra/compose/shared/traefik/certs/sentient.key`
- `infra/compose/shared/traefik/certs/sentient-root-ca.crt`

## Trust the Root CA (operators/admin devices)

Install `infra/compose/shared/traefik/certs/sentient-root-ca.crt` as a trusted root CA on:

- Tech laptops/tablets
- GM station(s)
- Any device accessing `https://` UIs/APIs

## Apply (Traefik)

From `infra/compose/shared`:

```bash
docker compose --env-file .env.local up -d --build
```

## Verify

- `https://<TRAEFIK_DASHBOARD_HOSTNAME>`
- `https://<GRAFANA_HOSTNAME>`
- `https://<AUTH_HOSTNAME>`
- `https://<API_PUBLIC_HOSTNAME>` (per room)
