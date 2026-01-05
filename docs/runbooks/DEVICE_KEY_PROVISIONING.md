# Runbook — Device HMAC Key Provisioning (v8)

Sentient v8 uses **per-device HMAC-SHA256** for controller command authentication.

Each v8 `device_id` has a unique 32-byte secret key (hex encoded; 64 chars).

## 1) Generate Keys (per room)

From repo root:

```bash
./scripts/provision-device-keys.py --room clockwork \
  --device keys_green_key_box \
  --device keys_yellow_key_box \
  --device keys_blue_key_box \
  --device keys_red_key_box
```

This writes:

- `infra/secrets/clockwork/device_hmac_keys.json` (gitignored)

If you want a paste-ready env value:

```bash
./scripts/provision-device-keys.py --room clockwork --device sim1 --print-env
```

## 2) Configure `sentient-core` with device keys

In the room `.env` (example `infra/rooms/clockwork/.env`), set:

- `DEVICE_HMAC_KEYS_JSON` to the JSON mapping (`device_id` → `hex_key`)

Example:

```bash
DEVICE_HMAC_KEYS_JSON={"keys_green_key_box":"...","keys_yellow_key_box":"..."}
```

## 3) Configure Firmware Keys

For Teensy v8 sketches, set the appropriate HMAC key constant(s) to match the `device_id`.

Example (multi-device controller): `hardware/Controller Code Teensy/keys_v8/keys_v8.ino`

- `HMAC_GREEN_KEY_BOX`, `HMAC_YELLOW_KEY_BOX`, etc.

## 4) Verify

- Run the room stack.
- Dispatch a command (core will sign it) and confirm the device accepts it (ACK ACCEPTED + COMPLETED).

See:

- `docs/protocol/AUTH_HMAC.md`
- `docs/runbooks/ROOM_BRINGUP.md`

