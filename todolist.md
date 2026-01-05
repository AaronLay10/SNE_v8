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

- [x] Install/validate Ubuntu Server 24.04 LTS baseline (headless)
- [x] Fix host bootstrap script for Ubuntu 24.04 Compose packages (`scripts/host-bootstrap-ubuntu24.sh`)
- [ ] Harden host SSH access (keys, fail2ban, firewall)
- [@] Configure time sync (server is NTP source) and verify drift limits
  - [x] Add chrony setup script + runbook (`scripts/host-ntp-chrony.sh`, `docs/runbooks/TIME_SYNC.md`)
  - [ ] Run `sudo ./scripts/host-ntp-chrony.sh` on the host and verify `chronyc tracking`
- [@] Configure UDM Pro VLANs (one VLAN per room) + firewall rules (room VLAN → room stack only)
  - [x] Add runbook (`docs/runbooks/UDM_PRO_VLANS_FIREWALL.md`)
  - [x] Define VLAN IDs + subnets per room (Clockwork=30/192.168.30.0/24, Requiem=40/192.168.40.0/24, Pharaohs=50/192.168.50.0/24, Fly-Tanic=60/192.168.60.0/24)
  - [x] Assign R710 IP per room VLAN (`192.168.30.2`, `192.168.40.2`, `192.168.50.2`, `192.168.60.2`)
  - [ ] Apply firewall rules: room isolation + allow room→R710 MQTT/NTP + allow admin→room ops
- [ ] Document NIC mapping / bonding plan (if any) and IP addressing scheme
- [x] Decide DNS host: UDM Pro local DNS (split-horizon per room VLAN)
- [@] Create per-room MQTT DNS records on UDM Pro: `mqtt.<room>.sentientengine.ai` → room broker endpoint (`docs/runbooks/UDM_PRO_DNS.md`)
  - [x] `mqtt.clockwork.sentientengine.ai` (Clockwork)
  - [ ] `mqtt.<next_room>.sentientengine.ai`

## 2) Container Platform + Reverse Proxy

- [x] Install Docker + Docker Compose on the host
- [ ] Ensure `techadmin` can run Docker without sudo (re-login after docker group change) and install Buildx (`docker-buildx`) for Compose builds
  - [x] Add troubleshooting runbook (`docs/runbooks/DOCKER_ACCESS.md`)
- [@] Choose reverse proxy (Traefik vs NGINX) and standardize
- [x] Bring up shared reverse proxy stack (Traefik scaffolding)
- [x] Add Traefik host-based routing (HTTP) for shared services + room APIs (`docs/runbooks/REVERSE_PROXY.md`)
  - [x] Ensure per-room Traefik routers/services are unique per `STACK_ID` (avoid collisions)
- [@] Implement TLS strategy (cert provisioning/renewal, internal vs public)
  - [x] Enable Traefik HTTPS (`websecure`) + HTTP→HTTPS redirect (`infra/compose/shared/docker-compose.yml`)
  - [x] Add internal CA bootstrap script (`scripts/tls-bootstrap-internal-ca.sh`) + runbook (`docs/runbooks/TLS.md`)
  - [ ] Install internal Root CA on operator/admin devices
- [ ] Define per-room Docker networks and shared admin network
- [@] Define secrets strategy implementation (Docker secrets vs Vault) and bootstrap workflow

## 3) Per-Room Runtime Stack (Compose Template)

- [x] Create a Compose template for a room stack:
  - [x] `sentient-core` (real-time scheduler)
  - [x] MQTT v5 broker (per room)
  - [x] TimescaleDB (per room)
  - [x] `osc-bridge` (Sentient → SCS)
  - [x] `sentient-api` (room-scoped HTTP API)
  - [x] `sentient-notify` (room-scoped notifier)
- [x] Ensure room stack can be restarted independently without impacting other rooms
- [x] Support per-room bind IPs to avoid port conflicts (MQTT/DB) across rooms
- [x] Validate a live room stack (room1) with MQTT auth + core heartbeat publishing
- [x] Add dev-only controller simulator (MQTT heartbeat + ack loop)
- [@] Create production room instances
  - [x] Create `clockwork` room stack (`infra/rooms/clockwork`)
  - [x] Fix shared admin network to be external (`sentient-admin`) for room stacks
  - [x] Smoke-test Clockwork API locally (`scripts/room-smoke-test.sh`)
- [ ] Define resource limits and placement guidance (CPU/mem) per room
- [x] Define health checks and startup order dependencies (DB healthcheck + `depends_on: service_healthy`)
- [ ] Implement “arm/activate” operational modes (no execution vs live execution)
  - [x] MVP: `CORE_DISPATCH_ENABLED` + `CORE_CRITICAL_ARMED` env guardrails in `sentient-core`

## 4) Warm Standby (Manual Promotion) Per Room

- [x] Lock broker reachability convention: controllers use `mqtt.<room>.sentientengine.ai` (per-room DNS)
- [x] Pick manual promotion mechanism for `mqtt.<room>`: **manual DNS switch** (UDM Pro local record repoint after safety verification)
- [x] Implement standby room stacks (separate Compose project + isolated network endpoints)
- [x] Define manual promotion runbook (primary down → standby up → safety verification → re-arm) (`docs/runbooks/WARM_STANDBY_PROMOTION.md`)
- [x] Define data/config sync strategy (graphs/config versions, secrets, DB state needed to resume) (`docs/runbooks/WARM_STANDBY_PROMOTION.md`)
- [@] Add monitoring/alerts for “primary unhealthy” and “standby readiness”
  - [x] Alert on missing `core/status` updates (notify watchdog)

## 5) Sentient Core (Rust) — Real-Time Scheduler

- [x] Create `sentient-core` project skeleton (Rust + Tokio)
- [x] Implement 1ms scheduler loop (monotonic time source)
- [x] Connect to MQTT + publish core heartbeat
- [x] Subscribe to device topics and ingest publishes (heartbeat/ack/state/telemetry)
- [@] Implement directed-graph runtime:
  - [@] Node model (cue/logic), edges, parallel paths
  - [ ] Preconditions (boolean + temporal clauses) continuous evaluation
  - [@] Version pinning (no hot-swap during runs)
  - [ ] Per-node controls (pause/resume/skip)
  - [ ] Graph checkpoints (restore safe state + graph position)
  - [x] Minimal file-based sequential runner (prototype) (`docs/core/GRAPH_JSON.md`, `CORE_GRAPH_PATH`)
  - [x] DB-backed graph versions + activation pointer (API + core load-on-start/reload)
- [ ] Implement priority arbitration (scene > puzzle > manual override > safety)
- [@] Implement device command pipeline:
  - [x] Per-device sequence numbers (MVP in core dispatch)
  - [x] Correlation IDs (MVP in core dispatch / dev test)
  - [@] Idempotency expectations and duplicate detection (firmware + core rules)
    - [x] Firmware: re-ack duplicates by `command_id` (SentientV8 idempotency cache)
    - [ ] Core: de-dupe/track inflight + reconcile duplicates
  - [x] Timeouts, retries, and FAULT handling (MVP: retry only before ACCEPTED)
  - [x] Publish retained dispatch/command fault events for ops/notify (blocked/timeout/rejected)
  - [x] Add MQTT control-plane dispatch topic (`room/{room_id}/core/dispatch`)
- [ ] Implement dry-run mode (mock devices, full timing/logging, no hardware commands)
- [ ] Implement crash recovery (restore game state; safety-critical requires human verification)
- [@] Implement broker-down handling (room safety incident):
  - [x] If MQTT broker is unreachable/disconnected, immediately pause room execution (no dispatch)
  - [@] Notify gamemaster + technical personnel (high severity)
    - [x] Add `sentient-notify` service (webhook-capable) (`services/sentient-notify/`)
    - [ ] Configure `NOTIFY_WEBHOOK_URL` per room stack
  - [x] Require manual, safety-first recovery (no automatic failover) (pause gate + manual resume control)

## 6) MQTT Broker + Topic/Schema Contract

- [x] Standardize initial broker on Mosquitto (room-local, MQTT v5)
- [x] Define canonical topic layout (room/device/{id}/cmd, ack, state, telemetry, heartbeat)
- [x] Define payload schemas (command, ack/complete, heartbeat, safety state) and version them
- [@] Implement QoS strategy (QoS 1 commands) + retained messages policy
  - [x] Lock policy doc (`docs/protocol/QOS_RETAIN.md`)
  - [x] Align controller-sim heartbeat QoS 0 + state retained QoS 1
  - [x] Align core publishing defaults (heartbeat/device-status QoS/retain) everywhere
- [@] Implement LWT + heartbeat expectations and server-side liveness evaluation
  - [x] Device publishes retained `ONLINE` presence + broker LWT retained `OFFLINE`
  - [x] Core computes online/offline from presence + heartbeat timeout (3s)
  - [x] Decide/lock QoS + retain rules per topic (cmd/ack/state/telemetry/heartbeat/presence) (`docs/protocol/QOS_RETAIN.md`)
- [x] Publish retained device fault events (offline/online) for UIs/notify (`room/{room_id}/core/device/{device_id}/fault`)
- [x] Define device offline timeout (3s)
- [@] Implement authenticated commands (HMAC/signatures) and validation rules
  - [x] HMAC signing spec + canonical bytes defined
  - [x] Core can sign commands when device key is configured (dev test hook)
  - [x] Controller-sim can enforce HMAC and rejects invalid commands
  - [x] Implement firmware-side HMAC verification (Teensy v8 library: `hardware/Custom Libraries/SentientV8/`)
  - [ ] Implement key provisioning + rotation workflow (Tech UI / commissioning)
- [x] Require MQTT auth (shared credentials unique per room)
- [ ] Define and document per-room broker hostname behavior:
  - [ ] Controllers connect to `mqtt.<room>.sentientengine.ai` (split-horizon DNS per room VLAN)
  - [ ] Broker down is treated as a room crash/safety issue (pause + notify; manual recovery)
- [x] Publish retained core status snapshot for dashboards (`room/{room_id}/core/status`)

## 7) Controller Firmware Contract + Reference Implementations

- [x] Import current controller firmware baseline into `hardware/`
- [x] Write “Firmware Contract” spec (authoritative) aligned with architecture doc (`docs/firmware/FIRMWARE_CONTRACT.md`)
- [x] Add HMAC implementation reference for firmware (`docs/firmware/HMAC_REFERENCE.md`)
- [x] Decide v8 command model: typed `action` + device-specific JSON `parameters` (no legacy bridge fields)
- [ ] Teensy 4.1 reference firmware:
  - [x] MQTT client + heartbeat/LWT + ack/complete (v8 library scaffolding)
  - [x] Implement v8 command parsing + HMAC verify (v8 library scaffolding)
  - [x] Idempotency cache (re-ack same `command_id` without re-exec) (SentientV8 library)
  - [ ] Safety state reporting + reason codes
  - [x] Write Teensy v8 integration notes (`docs/firmware/TEENSY_V8.md`)
  - [x] Add Teensy v8 library scaffolding (`hardware/Custom Libraries/SentientV8/`, `hardware/Custom Libraries/SentientCrypto/`, `hardware/Custom Libraries/ArduinoMQTT/`)
  - [x] Add Teensy v8 example sketch (`hardware/templates/v8/TeensyV8Template/TeensyV8Template.ino`)
  - [x] Port keys controller to v8 (Option 2 multi-device IDs) (`hardware/Controller Code Teensy/keys_v8/keys_v8.ino`)
  - [x] Generate v8 stubs for all controllers (`scripts/generate-teensy-v8-stubs.py`, `hardware/Controller Code Teensy/*_v8/`)
  - [@] Port all controllers to v8 (implement real ops + state/telemetry):
    - [@] `boiler_room_subpanel` (real v8 firmware started: `hardware/Controller Code Teensy/boiler_room_subpanel_v8/boiler_room_subpanel_v8.ino`)
    - [@] `chemical` (real v8 firmware started: `hardware/Controller Code Teensy/chemical_v8/chemical_v8.ino`)
    - [@] `clock` (stateless v8 firmware started: `hardware/Controller Code Teensy/clock_v8/clock_v8.ino`)
    - [@] `crank` (real v8 firmware started: `hardware/Controller Code Teensy/crank_v8/crank_v8.ino`)
    - [@] `floor` (stateless v8 firmware started: `hardware/Controller Code Teensy/floor_v8/floor_v8.ino`)
    - [@] `fuse` (real v8 firmware started: `hardware/Controller Code Teensy/fuse_v8/fuse_v8.ino`)
    - [@] `gauge_1_3_4` (real v8 firmware started: `hardware/Controller Code Teensy/gauge_1_3_4_v8/gauge_1_3_4_v8.ino`)
    - [@] `gauge_2_5_7` (real v8 firmware started: `hardware/Controller Code Teensy/gauge_2_5_7_v8/gauge_2_5_7_v8.ino`)
    - [@] `gauge_6_leds` (real v8 firmware started: `hardware/Controller Code Teensy/gauge_6_leds_v8/gauge_6_leds_v8.ino`)
    - [@] `gear` (real v8 firmware started: `hardware/Controller Code Teensy/gear_v8/gear_v8.ino`)
    - [@] `gun_drawers` (real v8 firmware started: `hardware/Controller Code Teensy/gun_drawers_v8/gun_drawers_v8.ino`)
    - [@] `kraken` (real v8 firmware started: `hardware/Controller Code Teensy/kraken_v8/kraken_v8.ino`)
    - [@] `lab_rm_cage_a` (real v8 firmware started: `hardware/Controller Code Teensy/lab_rm_cage_a_v8/lab_rm_cage_a_v8.ino`)
    - [@] `lab_rm_cage_b` (real v8 firmware started: `hardware/Controller Code Teensy/lab_rm_cage_b_v8/lab_rm_cage_b_v8.ino`)
    - [@] `lab_rm_doors_hoist` (real v8 firmware started: `hardware/Controller Code Teensy/lab_rm_doors_hoist_v8/lab_rm_doors_hoist_v8.ino`)
    - [@] `lever_boiler` (real v8 firmware started: `hardware/Controller Code Teensy/lever_boiler_v8/lever_boiler_v8.ino`)
    - [@] `lever_fan_safe` (real v8 firmware started: `hardware/Controller Code Teensy/lever_fan_safe_v8/lever_fan_safe_v8.ino`)
    - [@] `lever_riddle` (real v8 firmware started: `hardware/Controller Code Teensy/lever_riddle_v8/lever_riddle_v8.ino`)
    - [@] `main_lighting` (real v8 firmware started: `hardware/Controller Code Teensy/main_lighting_v8/main_lighting_v8.ino`)
    - [@] `maks_servo` (real v8 firmware started: `hardware/Controller Code Teensy/maks_servo_v8/maks_servo_v8.ino`)
    - [@] `music` (real v8 firmware started: `hardware/Controller Code Teensy/music_v8/music_v8.ino`)
    - [@] `picture_frame_leds` (real v8 firmware started: `hardware/Controller Code Teensy/picture_frame_leds_v8/picture_frame_leds_v8.ino`)
    - [@] `pilaster` (real v8 firmware started: `hardware/Controller Code Teensy/pilaster_v8/pilaster_v8.ino`)
    - [@] `pilot_light` (real v8 firmware started: `hardware/Controller Code Teensy/pilot_light_v8/pilot_light_v8.ino`)
    - [@] `power_control_lower_left` (real v8 firmware started: `hardware/Controller Code Teensy/power_control_lower_left_v8/power_control_lower_left_v8.ino`)
    - [@] `power_control_lower_right` (real v8 firmware started: `hardware/Controller Code Teensy/power_control_lower_right_v8/power_control_lower_right_v8.ino`)
    - [@] `power_control_upper_right` (real v8 firmware started: `hardware/Controller Code Teensy/power_control_upper_right_v8/power_control_upper_right_v8.ino`)
    - [@] `riddle` (stateless v8 firmware started: `hardware/Controller Code Teensy/riddle_v8/riddle_v8.ino`)
    - [@] `study_a` (real v8 firmware started: `hardware/Controller Code Teensy/study_a_v8/study_a_v8.ino`)
    - [@] `study_b` (real v8 firmware started: `hardware/Controller Code Teensy/study_b_v8/study_b_v8.ino`)
    - [@] `study_d` (real v8 firmware started: `hardware/Controller Code Teensy/study_d_v8/study_d_v8.ino`)
    - [@] `syringe` (real v8 firmware started: `hardware/Controller Code Teensy/syringe_v8/syringe_v8.ino`)
    - [@] `vault` (real v8 firmware started: `hardware/Controller Code Teensy/vault_v8/vault_v8.ino`)
    - [@] `vern` (real v8 firmware started: `hardware/Controller Code Teensy/vern_v8/vern_v8.ino`)
- [ ] ESP32 reference firmware (with native OTA)
- [ ] Raspberry Pi agent/service (package/A-B update approach)
- [ ] Provisioning workflow:
  - [ ] Unauthenticated discovery announce + manual approval
  - [@] Per-device key provisioning + rotation support
    - [x] Add local key provisioning script + runbook (`scripts/provision-device-keys.py`, `docs/runbooks/DEVICE_KEY_PROVISIONING.md`)
  - [ ] Identity rebind for hardware replacement
- [ ] Define and implement OTA workflows per device class

## 8) Safety System (Dual-Layer)

- [ ] Define safety-critical device classification in config model (binary critical/non-critical)
- [@] Implement server-side safety gating (interlocks required before publish)
  - [x] Add room-local `devices` registry table (safety_class/enabled) (`infra/compose/room-template/db/init/002_devices.sql`)
  - [x] Enforce device safety class in core dispatch (registry can upgrade to CRITICAL) (`services/sentient-core/src/main.rs`)
- [x] Compute and publish aggregated room safety state (core heartbeat/status) (`services/sentient-core/src/main.rs`)
- [ ] Implement controller-side enforcement expectations (reject unsafe commands)
- [@] Implement canonical safety states (SAFE/BLOCKED/FAULT/E_STOP/MAINTENANCE + latching)
  - [x] Core latches room safety on device FAULT/E_STOP or `latched=true` and requires explicit reset (`RESET_SAFETY_LATCH`)
- [@] Implement Technical-UI-only safety reset (dual confirmation + controller SAFE prerequisites)
  - [x] API dual-confirm endpoints (request/confirm) for `RESET_SAFETY_LATCH` (`services/sentient-api/`)
  - [ ] Technical UI flow + UX + audit trail
- [ ] Define E-Stop behavior, audit, and recovery flow

## 9) Data Model + TimescaleDB

- [ ] Define schema for:
  - [ ] Rooms, devices, device capabilities, safety flags
  - [ ] Graph definitions + versions
  - [ ] Live state + sessions/runs
  - [ ] Commands/events with correlation IDs
  - [ ] Telemetry streams (hypertables)
- [@] Implement migrations and versioning
  - [x] Add room-local DB init SQL for `events` hypertable (`infra/compose/room-template/db/init/001_init.sql`)
  - [x] Persist core MQTT events (cmd/ack/state/heartbeat/presence/telemetry) to DB (`services/sentient-core/src/main.rs`)
- [ ] Implement retention policies (30–90 days raw telemetry + longer aggregates)
- [ ] Implement adaptive logging downshift thresholds and behavior

## 10) OSC Bridge (Sentient → SCS)

- [x] Define OSC cue payload contract (topic + JSON schema) for `osc-bridge`
- [x] Bootstrap `osc-bridge` placeholder service
- [x] Implement basic UDP OSC send (no ack/retry yet)
- [x] Implement application-level ack/retry (required) (`room/{room_id}/audio/ack`)
- [x] Implement failure handling (alerting + retries + escalation) (`room/{room_id}/audio/fault`, notify subscribed)
- [x] Add test harness for OSC contract (simulated SCS endpoint) (`services/scs-sim/`, room-template dev profile)

## 11) APIs + WebSockets (Shared Backend)

- [ ] Define API surface:
  - [ ] Technical configuration endpoints
  - [ ] Creative graph CRUD/versioning endpoints
  - [ ] Gamemaster controls (overrides, pause/resume/skip, checkpoints)
- [ ] Implement WebSocket realtime updates (state/events, controller health, safety status)
- [ ] Implement RBAC gates on all endpoints
- [ ] Implement full audit trail for overrides and sensitive actions

### 11.1 Room-Scoped API (Bootstrap)

- [@] Add `sentient-api` (room-scoped) HTTP service (`services/sentient-api/`, `docs/runbooks/ROOM_API.md`)
  - [x] Dispatch/control publish (MQTT `core/dispatch`, `core/control`)
  - [x] Safety reset (dual-confirm request/confirm endpoints)
  - [x] Read-only cache endpoints (core/device status + faults)
  - [x] WebSocket stream endpoint for realtime dashboards
  - [x] Optional DB events query endpoint (room `events` hypertable)

## 12) UIs (Three Web Apps)

- [@] Technical UI (`tech.sentientengine.ai`)
  - [x] Scaffold containerized UI + Traefik route (`apps/tech-ui/`, `docs/runbooks/UIS.md`)
  - [ ] User provisioning (create users/roles)
  - [ ] Device discovery approval + provisioning
  - [ ] Device configuration, safety flags, interlocks config
  - [x] Safety reset flow (dual confirmation)
- [@] Creative UI (`creative.sentientengine.ai`)
  - [x] Scaffold containerized UI + Traefik route (`apps/creative-ui/`, `docs/runbooks/UIS.md`)
  - [ ] Graph editor (directed graph drag/drop)
  - [ ] Versioning/publish workflow (no hot-swap)
  - [ ] Simulation/dry-run controls and playback of logs
- [@] Gamemaster UI (`mythra.sentientengine.ai`)
  - [x] Scaffold containerized UI + Traefik route (`apps/gm-ui/`, `docs/runbooks/UIS.md`)
  - [ ] Realtime dashboard (status, health, safety)
  - [ ] Priority-injection overrides (fully audited)
  - [ ] Checkpoints + per-node controls

## 13) Auth (Central RBAC)

- [@] Implement `sentient-auth` service (or shared auth module) with RBAC model
  - [x] Add containerized `sentient-auth` + Postgres (`infra/compose/shared/docker-compose.yml`, `services/sentient-auth/`)
  - [x] JWT login/bootstrap endpoints + room API JWT verification (`docs/runbooks/AUTH.md`)
  - [x] Document shared env vars (`infra/compose/shared/.env.example`)
  - [x] Add bootstrap safety + login rate limiting + audit log (`infra/compose/shared/auth-db/init/001_init.sql`, `services/sentient-auth/src/main.rs`)
  - [x] Bootstrap first ADMIN user via `AUTH_BOOTSTRAP_TOKEN`
- [ ] Implement session/token strategy for web apps
- [ ] Implement role/permission matrix and admin workflows

## 14) Notifications (FCM)

- [x] Implement `sentient-notify` service (room-scoped; webhook-capable MVP)
- [ ] Integrate Firebase Cloud Messaging (FCM) credentials + secure storage
- [ ] Define alert sources and severity mapping (INFO/WARN/CRITICAL)
- [ ] Implement escalation/until-acknowledged behavior + audit trail
- [ ] Implement deep links into web apps

## 15) Observability + Dashboards

- [ ] Define metrics to emit (scheduler jitter, MQTT RTT, backlog, heartbeat gaps, safety states)
- [ ] Implement metrics storage strategy (TimescaleDB + Grafana as specified)
- [@] Create mandatory Grafana dashboards per room + shared overview
  - [x] Add initial log-based dashboard (Loki) (`infra/compose/shared/grafana/provisioning/dashboards/json/sentient-logs-overview.json`)
- [ ] Define SLOs and alert thresholds (latency, missed heartbeats, FAULT/E_STOP)
- [x] Add containerized logs + Grafana base stack (Loki + Promtail + Grafana) (`infra/compose/shared/docker-compose.yml`, `docs/runbooks/OBSERVABILITY.md`)

## 16) Backup/Restore + Runbooks

- [ ] Implement scheduled backups to off-server storage
- [ ] Implement periodic restore tests into staging and validate integrity
- [ ] Write operational runbooks:
  - [x] Room bring-up (`docs/runbooks/ROOM_BRINGUP.md`)
  - [x] Room MQTT broker outage (`docs/runbooks/BROKER_OUTAGE.md`)
  - [x] Live incident (FAULT) (`docs/runbooks/INCIDENT_FAULT.md`)
  - [@] Safety reset (latched) (`docs/runbooks/SAFETY_RESET.md`)
  - [x] Warm standby promotion (`docs/runbooks/WARM_STANDBY_PROMOTION.md`)
  - [x] Backup restore test (`docs/runbooks/BACKUP_RESTORE_TEST.md`)
  - [x] Add DB backup/restore scripts (`scripts/backup-room-db.sh`, `scripts/restore-room-db.sh`, `scripts/backup-auth-db.sh`, `scripts/restore-auth-db.sh`)

## 17) Security Hardening

- [ ] Enforce signed firmware requirement and verification policy
- [@] Implement command authentication (HMAC) + rotation policy
- [x] Add protocol helpers for HMAC sign/verify
- [x] Optional: protect core MQTT control plane with `CORE_CONTROL_TOKEN` (prevents unauthenticated pause/resume/reset)
- [ ] Implement least-privilege network rules (VLAN + Docker networks)
- [ ] Implement configuration drift detection + alerting

## 18) Testing + Validation

- [@] Build integration test rig (mock MQTT devices + simulated room)
  - [x] Add CLI helper to publish dispatch requests (`scripts/core-dispatch.sh`)
  - [x] Add room smoke test (`scripts/room-smoke-test.sh`, `docs/runbooks/ROOM_SMOKE_TEST.md`)
  - [x] Add controller-sim idempotency + retry test knobs (`SIM_DROP_FIRST_ACCEPTED_ACK`)
  - [x] Add controller-sim safety fault injection (for testing safety latch/reset)
- [ ] Add soak/load testing plan (4 rooms concurrently)
- [ ] Define latency measurement methodology (end-to-end command latency)
- [ ] Add CI pipeline (build/test/lint) for Rust + web apps

## 19) Release + Deployment

- [ ] Define versioning and release process per service
- [ ] Implement deployment automation (compose updates, migrations, rollback)
- [ ] Define “between games” staged rollout workflow (validate → arm → activate)

## 20) Auto-start on Boot (Docker-only)

- [x] Ensure shared + room services use `restart: unless-stopped` (`docs/runbooks/AUTOSTART.md`)
- [ ] Bring up shared + room stacks once so Docker can restart them after reboot
