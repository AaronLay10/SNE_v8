# Sentient v8 — Decisions Log

Record decisions that affect architecture, security posture, or operational practices.

## Open Decisions

- [ ] Reverse proxy: Traefik vs NGINX (TLS/certs strategy depends on this)
- [ ] MQTT broker: Mosquitto vs EMQX (config + performance tuning depends on this)
- [ ] Secrets management: Vault vs sops+age (how we bootstrap keys + deploy)
- [ ] UI stack (React/Next.js vs something else)
- [ ] Database schema ownership (migrations per service vs single migration owner)

## Current Defaults (Scaffolding Only)

These defaults are used in the initial Compose templates and can be changed without re-architecting.

- Reverse proxy: Traefik (`infra/compose/shared/docker-compose.yml`)
- MQTT broker: Mosquitto 2.x (`infra/compose/room-template/docker-compose.yml`)
- Secrets: file-backed placeholders via `.env` (real encrypted secrets workflow TBD)

## Locked Decisions (from v8 Q&A)

- Per-room isolated stacks (core + broker + DB) with independent restart
- MQTT v5 for commands/events; QoS 1 for commands; topic-per-device
- Core in Rust/Tokio; 1ms scheduler resolution; server-authoritative execution
- Dual-layer safety gating (server checks + controller enforcement)
- Directed graph creative model; version pinning; per-node control; checkpoints
- TimescaleDB for config/state/telemetry; adaptive downshift under load
- OSC bridge to external SCS for audio (in-core playback deferred)
- VLAN per room + Docker network isolation per room
- Warm standby per room with manual promotion

## Locked Decisions (Implementation)

- Controller command authentication: HMAC-SHA256 (simple local-LAN friendly; per-device keys)
- MQTT client auth: shared username/password for all controllers (unique per room)
- MQTT broker DNS naming: per-room hostname `mqtt.<room>.sentientengine.ai` (ex: `mqtt.clockwork.sentientengine.ai`)
- Split-horizon DNS host: UDM Pro (local DNS records per room VLAN)
- Warm standby promotion mechanism: manual DNS switch (UDM Pro local record repoint) after safety verification; no automatic failover
- Device offline timeout: 3s (heartbeat-based)
- Command model: typed `action` + device-specific JSON `parameters` (no legacy bridge fields)
- Device identity model: Option 2 (one v8 `device_id` per logical sub-device)
- Device ID convention: room-unique `device_id = "{controller_id}_{subdevice_id}"`
- Command routing convention: require `parameters.op` (string) for `action = "SET"`
- MQTT QoS/retain policy locked: `docs/protocol/QOS_RETAIN.md`
