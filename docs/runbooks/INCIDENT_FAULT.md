# Runbook — Live Incident (FAULT) (v8)

This runbook is for an active room incident where Sentient has raised a FAULT (or the room is not behaving safely).

Principles:

- **Safety first**: stop motion/actuators before debugging.
- **No automatic recovery** for broker outage or safety faults; recovery is operator-driven.
- **No “guessing”**: confirm device state and interlocks before resuming.

## 1) Immediate actions

1. Pause the room (Gamemaster/Technical).
2. Ensure the physical room is safe (E-stop if required).
3. Notify technical personnel if the fault is safety-related or recurring.

## 2) Identify the fault

Primary sources:

- Grafana → `Sentient v8 — Logs Overview` (filter to the room / service)
- MQTT retained faults:
  - `room/<room>/core/fault`
  - `room/<room>/core/device/<device>/fault`

## 3) Broker outage handling

If the issue is broker connectivity:

- Follow `docs/runbooks/BROKER_OUTAGE.md`.
- If promotion is required, follow `docs/runbooks/WARM_STANDBY_PROMOTION.md`.

## 4) Device offline / rejected / timeout

Typical causes:

- Controller power/network issue
- Wrong MQTT creds for the room
- Wrong per-device HMAC key
- Unsafe device state (controller rejects)
- Mechanical jam / interlock violation

Recommended checks:

- Verify broker is up and reachable from the room VLAN.
- Verify the controller is online (LWT/heartbeat topic present).
- Verify the device publishes `state` and reports `safety_state`.

## 5) Resume criteria

Only resume when:

- Physical room is safe.
- Fault root cause is understood (or isolated).
- Devices report SAFE (or an explicitly permitted state).
- Technical operator explicitly resumes (no implicit auto-resume).

