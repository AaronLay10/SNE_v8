# Command Authentication (HMAC-SHA256) — v8

Sentient v8 uses a simple HMAC-SHA256 authentication model for controller commands. This is designed for LAN/VLAN deployments where we want basic integrity/authentication without heavy PKI.

## Key Model

- Per-room shared MQTT username/password for broker access (transport auth).
- Per-device HMAC key for command authentication (message auth).
- Keys are provisioned and rotated via Technical UI (future); in early prototypes they may be file/env configured.

## What Is Signed

Commands use `CommandEnvelope` and include optional `auth`:

- `auth.alg`: `"HMAC-SHA256"`
- `auth.kid`: optional key ID (rotation)
- `auth.mac_hex`: hex-encoded MAC

The MAC is computed over a canonical byte string constructed from required fields in a fixed order.

## Canonical Signing Bytes (v1)

Create a UTF-8 byte string by concatenating key/value pairs, separated by `\n`, in this exact order:

1. `schema=<schema>`
2. `room_id=<room_id>`
3. `device_id=<device_id>`
4. `command_id=<uuid>`
5. `correlation_id=<uuid>`
6. `sequence=<u64>`
7. `issued_at_unix_ms=<u64>`
8. `action=<action>` (enum string as sent, e.g. `OPEN`)
9. `safety_class=<safety_class>` (e.g. `CRITICAL`)
10. `parameters=<canonical_parameters_json>`

### `canonical_parameters_json`

- JSON-serialize the `parameters` object with stable key ordering and no extra whitespace.
- If `parameters` is absent, treat it as `{}`.

## Verification Rules

- Devices must reject commands with missing/invalid `auth` when running in “enforced” mode.
- Devices may accept unsigned commands only in an explicit commissioning/test mode.
- Server should treat `auth` as required for safety-critical device classes once provisioning is implemented.

## Rationale

- Avoids relying on canonical JSON for the whole envelope.
- Easy to implement on microcontrollers and easy to debug.

