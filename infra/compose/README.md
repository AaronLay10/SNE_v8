# Docker Compose Templates

Goal: one Compose project per room stack (core + broker + DB + osc-bridge), plus optional shared services (proxy/auth/notify/observability).

This folder will contain:

- `infra/compose/room-template/` reusable per-room stack template
- `infra/compose/shared/` shared services (reverse proxy, auth, notify, grafana)

