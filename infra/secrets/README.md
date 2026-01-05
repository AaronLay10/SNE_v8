# infra/secrets (local-only)

This folder holds **local secrets** for Sentient v8 (per room):

- device HMAC keys (`device_id` → 32-byte hex)
- other room-local config that must not be committed

This directory is gitignored (except this README).

Recommended layout:

- `infra/secrets/<room_id>/device_hmac_keys.json`

