# Grafana Dashboards (Provisioned)

Place JSON dashboards in this folder to have Grafana auto-load them.

Initial dashboards are intentionally log-based (Loki), so they work before we wire up
room DB connections and metrics.

Included dashboards:

- `sentient-logs-overview.json` — quick operational view across rooms/services using Loki labels:
  - `compose_project` (room stack name; created by `infra/rooms/<room_id>/`)
  - `compose_service` (service name; ex: `sentient-core`, `mosquitto`)

Next: add TimescaleDB datasources per room and build room dashboards against the `events`
hypertable once network/isolation is finalized.

- which metrics sources we rely on (Timescale queries vs Prometheus),
- which room IDs exist and how the admin network reaches each room DB.
