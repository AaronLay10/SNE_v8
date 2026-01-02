# Sentient Neural Engine v8 — TODO List

Single checklist to track all work required to build, configure, and ship Sentient Neural Engine (SNE) v8.

Legend: `[ ]` todo, `[x]` done, `[@]` in progress, `[?]` needs decision.

---

## 0) Project Setup

- [x] Create repo structure for services, shared libs, and docs
- [ ] Define coding standards (Rust/TS), branching strategy, and release/versioning conventions
- [ ] Define “Definition of Done” for: safety, latency, observability, and deployment
- [ ] Create staging environment plan (same topology as prod, smaller scale)

## 1) R710 Host + Network Foundation

- [ ] Install/validate Ubuntu Server 24.04 LTS baseline (headless)
- [ ] Harden host SSH access (keys, fail2ban, firewall)
- [ ] Configure time sync (server is NTP source) and verify drift limits
- [ ] Configure UDM Pro VLANs (one VLAN per room) + firewall rules (room VLAN → room stack only)
- [ ] Document NIC mapping / bonding plan (if any) and IP addressing scheme
- [ ] Decide and document DNS naming for apps/services (tech/creative/mythra + per-room internal endpoints)

## 2) Container Platform + Reverse Proxy

- [x] Install Docker + Docker Compose on the host
- [@] Choose reverse proxy (Traefik vs NGINX) and standardize
- [ ] Implement TLS strategy (cert provisioning/renewal, internal vs public)
- [ ] Define per-room Docker networks and shared admin network
- [@] Define secrets strategy implementation (Docker secrets vs Vault) and bootstrap workflow

## 3) Per-Room Runtime Stack (Compose Template)

- [x] Create a Compose template for a room stack:
  - [ ] `sentient-core` (real-time scheduler)
  - [ ] MQTT v5 broker (per room)
  - [ ] TimescaleDB (per room)
  - [ ] `osc-bridge` (Sentient → SCS)
- [x] Ensure room stack can be restarted independently without impacting other rooms
- [ ] Define resource limits and placement guidance (CPU/mem) per room
- [ ] Define health checks and startup order dependencies
- [ ] Implement “arm/activate” operational modes (no execution vs live execution)

## 4) Warm Standby (Manual Promotion) Per Room

- [ ] Define standby topology (hot/warm/cold) and pick warm standby implementation details
- [ ] Implement standby room stacks (separate Compose project + isolated network endpoints)
- [ ] Define manual promotion runbook (primary down → standby up → safety verification → re-arm)
- [ ] Define data/config sync strategy (graphs/config versions, secrets, DB state needed to resume)
- [ ] Add monitoring/alerts for “primary unhealthy” and “standby readiness”

## 5) Sentient Core (Rust) — Real-Time Scheduler

- [x] Create `sentient-core` project skeleton (Rust + Tokio)
- [ ] Implement 1ms scheduler loop (monotonic time source)
- [ ] Implement directed-graph runtime:
  - [ ] Node model (cue/logic), edges, parallel paths
  - [ ] Preconditions (boolean + temporal clauses) continuous evaluation
  - [ ] Version pinning (active run pinned to graph version)
  - [ ] Per-node controls (pause/resume/skip)
  - [ ] Graph checkpoints (restore safe state + graph position)
- [ ] Implement priority arbitration (scene > puzzle > manual override > safety)
- [ ] Implement device command pipeline:
  - [ ] Per-device sequence numbers
  - [ ] Correlation IDs tied to graph node + execution instance
  - [ ] Idempotency expectations and duplicate detection
  - [ ] Timeouts, retries, and FAULT handling
- [ ] Implement dry-run mode (mock devices, full timing/logging, no hardware commands)
- [ ] Implement crash recovery (restore game state; safety-critical requires human verification)

## 6) MQTT Broker + Topic/Schema Contract

- [ ] Choose broker implementation (e.g., Mosquitto/EMQX/etc.) and standardize config
- [ ] Define canonical topic layout (room/device/{id}/cmd, ack, state, telemetry, heartbeat)
- [ ] Define payload schemas (command, ack/complete, heartbeat, safety state) and version them
- [ ] Implement QoS strategy (QoS 1 commands) + retained messages policy
- [ ] Implement LWT + heartbeat expectations and server-side liveness evaluation
- [ ] Implement authenticated commands (HMAC/signatures) and validation rules
- [x] Require MQTT auth (shared credentials unique per room)
- [x] Define HMAC command signing spec (canonical bytes)

## 7) Controller Firmware Contract + Reference Implementations

- [ ] Write “Firmware Contract” spec (authoritative) aligned with architecture doc
- [ ] Teensy 4.1 reference firmware:
  - [ ] MQTT v5 client, heartbeat/LWT, ack/complete, watchdog safe idle
  - [ ] Safety state reporting + reason codes
- [ ] ESP32 reference firmware (with native OTA)
- [ ] Raspberry Pi agent/service (package/A-B update approach)
- [ ] Provisioning workflow:
  - [ ] Unauthenticated discovery announce + manual approval
  - [ ] Per-device key provisioning + rotation support
  - [ ] Identity rebind for hardware replacement
- [ ] Define and implement OTA workflows per device class

## 8) Safety System (Dual-Layer)

- [ ] Define safety-critical device classification in config model (binary critical/non-critical)
- [ ] Implement server-side safety gating (interlocks required before publish)
- [ ] Implement controller-side enforcement expectations (reject unsafe commands)
- [ ] Implement canonical safety states (SAFE/BLOCKED/FAULT/E_STOP/MAINTENANCE + latching)
- [ ] Implement Technical-UI-only safety reset (dual confirmation + controller SAFE prerequisites)
- [ ] Define E-Stop behavior, audit, and recovery flow

## 9) Data Model + TimescaleDB

- [ ] Define schema for:
  - [ ] Rooms, devices, device capabilities, safety flags
  - [ ] Graph definitions + versions
  - [ ] Live state + sessions/runs
  - [ ] Commands/events with correlation IDs
  - [ ] Telemetry streams (hypertables)
- [ ] Implement migrations and versioning
- [ ] Implement retention policies (30–90 days raw telemetry + longer aggregates)
- [ ] Implement adaptive logging downshift thresholds and behavior

## 10) OSC Bridge (Sentient → SCS)

- [ ] Define OSC cue contract (addresses, payloads, naming conventions)
- [x] Bootstrap `osc-bridge` placeholder service
- [ ] Implement UDP OSC send with application-level ack/retry
- [ ] Implement failure handling (alerting + retries + escalation)
- [ ] Add test harness for OSC contract (simulated SCS endpoint)

## 11) APIs + WebSockets (Shared Backend)

- [ ] Define API surface:
  - [ ] Technical configuration endpoints
  - [ ] Creative graph CRUD/versioning endpoints
  - [ ] Gamemaster controls (overrides, pause/resume/skip, checkpoints)
- [ ] Implement WebSocket realtime updates (state/events, controller health, safety status)
- [ ] Implement RBAC gates on all endpoints
- [ ] Implement full audit trail for overrides and sensitive actions

## 12) UIs (Three Web Apps)

- [ ] Technical UI (`tech.sentientengine.ai`)
  - [ ] User provisioning (create users/roles)
  - [ ] Device discovery approval + provisioning
  - [ ] Device configuration, safety flags, interlocks config
  - [ ] Safety reset flow (dual confirmation)
- [ ] Creative UI (`creative.sentientengine.ai`)
  - [ ] Graph editor (directed graph drag/drop)
  - [ ] Versioning/publish workflow (no hot-swap)
  - [ ] Simulation/dry-run controls and playback of logs
- [ ] Gamemaster UI (`mythra.sentientengine.ai`)
  - [ ] Realtime dashboard (status, health, safety)
  - [ ] Priority-injection overrides (fully audited)
  - [ ] Checkpoints + per-node controls

## 13) Auth (Central RBAC)

- [ ] Implement `sentient-auth` service (or shared auth module) with RBAC model
- [ ] Implement session/token strategy for web apps
- [ ] Implement role/permission matrix and admin workflows

## 14) Notifications (FCM)

- [ ] Implement `sentient-notify` service
- [ ] Integrate Firebase Cloud Messaging (FCM) credentials + secure storage
- [ ] Define alert sources and severity mapping (INFO/WARN/CRITICAL)
- [ ] Implement escalation/until-acknowledged behavior + audit trail
- [ ] Implement deep links into web apps

## 15) Observability + Dashboards

- [ ] Define metrics to emit (scheduler jitter, MQTT RTT, backlog, heartbeat gaps, safety states)
- [ ] Implement metrics storage strategy (TimescaleDB + Grafana as specified)
- [ ] Create mandatory Grafana dashboards per room + shared overview
- [ ] Define SLOs and alert thresholds (latency, missed heartbeats, FAULT/E_STOP)

## 16) Backup/Restore + Runbooks

- [ ] Implement scheduled backups to off-server storage
- [ ] Implement periodic restore tests into staging and validate integrity
- [ ] Write operational runbooks:
  - [ ] Room bring-up
  - [ ] Live incident (FAULT)
  - [ ] Safety reset (latched)
  - [ ] Warm standby promotion
  - [ ] Backup restore test

## 17) Security Hardening

- [ ] Enforce signed firmware requirement and verification policy
- [@] Implement command authentication (HMAC) + rotation policy
- [x] Add protocol helpers for HMAC sign/verify
- [ ] Implement least-privilege network rules (VLAN + Docker networks)
- [ ] Implement configuration drift detection + alerting

## 18) Testing + Validation

- [ ] Build integration test rig (mock MQTT devices + simulated room)
- [ ] Add soak/load testing plan (4 rooms concurrently)
- [ ] Define latency measurement methodology (end-to-end command latency)
- [ ] Add CI pipeline (build/test/lint) for Rust + web apps

## 19) Release + Deployment

- [ ] Define versioning and release process per service
- [ ] Implement deployment automation (compose updates, migrations, rollback)
- [ ] Define “between games” staged rollout workflow (validate → arm → activate)
