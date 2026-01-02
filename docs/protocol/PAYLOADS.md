# MQTT Payloads (v8)

All payloads are JSON.

## Schema Field

All messages include `schema: "v8"` to enable future evolution without ambiguity.

## Command Envelope

Implemented in `crates/sentient-protocol/src/lib.rs` as `CommandEnvelope`.

Minimum required fields:

- `schema`, `room_id`, `device_id`
- `command_id` (UUID)
- `correlation_id` (UUID)
- `sequence` (u64, per-device)
- `issued_at_unix_ms` (u64)
- `action` (`OPEN|CLOSE|MOVE|SET`)
- `parameters` (JSON object; can be `{}`)
- `safety_class` (`CRITICAL|NON_CRITICAL`)

### Authentication (Controller Commands)

For real hardware deployments, commands are authenticated with HMAC-SHA256 using a per-device key.

- Field: `auth`
- Type: `CommandAuth`
- `alg`: `"HMAC-SHA256"`
- `kid`: optional key ID for rotation
- `mac_hex`: hex MAC over canonical signing bytes (canonicalization spec to be finalized)

## ACK / Completion

Implemented as `CommandAck`.

Status values:

- `ACCEPTED`
- `REJECTED`
- `COMPLETED`

## Heartbeat

Implemented as `Heartbeat`.

Notes:

- Heartbeats are periodic and also paired with MQTT LWT for disconnect detection.

## Audio Cue (Core → OSC Bridge → SCS)

Published by the core (or API) for the per-room `osc-bridge` to deliver to SCS:

- Topic: `room/{room_id}/audio/cue`
- Payload: `OscCue`

Arguments are typed as a tagged union:

```json
{"type":"INT","value":123}
```
