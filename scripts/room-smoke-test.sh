#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <room_id> [api_base_url]" >&2
  echo "Example: $0 room1 http://10.10.1.2:8080" >&2
  exit 2
fi

ROOM_ID="$1"
BASE_URL="${2:-}"

ENV_FILE="infra/rooms/${ROOM_ID}/.env"
if [[ -f "${ENV_FILE}" ]]; then
  # shellcheck disable=SC1090
  set -a
  source "${ENV_FILE}"
  set +a
fi

if [[ -z "${BASE_URL}" ]]; then
  API_BIND_IP="${API_BIND_IP:-127.0.0.1}"
  API_PORT="${API_PORT:-8080}"
  BASE_URL="http://${API_BIND_IP}:${API_PORT}"
fi

# Optional curl TLS helpers for internal CA + split-horizon DNS.
# - CURL_CACERT: path to root CA cert
# - CURL_RESOLVE: value passed to curl --resolve (e.g. api.clockwork.sentientengine.ai:443:127.0.0.1)
CURL_ARGS=()
if [[ -n "${CURL_CACERT:-}" ]]; then
  CURL_ARGS+=(--cacert "${CURL_CACERT}")
fi
if [[ -n "${CURL_RESOLVE:-}" ]]; then
  CURL_ARGS+=(--resolve "${CURL_RESOLVE}")
fi

AUTH_HEADER=()
if [[ -n "${API_TOKEN:-}" ]]; then
  AUTH_HEADER=(-H "Authorization: Bearer ${API_TOKEN}")
fi

echo "Room: ${ROOM_ID}"
echo "API:  ${BASE_URL}"
echo

echo "[1/4] GET /health"
curl -fsS "${CURL_ARGS[@]}" "${BASE_URL}/health" >/dev/null
echo "OK"

echo "[2/4] GET /v8/room/${ROOM_ID}/core/status"
STATUS_JSON="$(curl -fsS "${CURL_ARGS[@]}" "${AUTH_HEADER[@]}" "${BASE_URL}/v8/room/${ROOM_ID}/core/status")"
echo "OK"

PAUSED_REASON="$(
  python3 - <<PY
import json
j = json.loads(${STATUS_JSON@Q})
print(j.get("dispatch_paused_reason") or "")
PY
)"

if [[ "${PAUSED_REASON}" == "SAFETY_LATCHED" ]]; then
  echo "[3/5] POST /v8/room/${ROOM_ID}/safety/reset/request + confirm"
  RESET_JSON="$(curl -fsS "${CURL_ARGS[@]}" "${AUTH_HEADER[@]}" -H "Content-Type: application/json" \
    -d '{"reason":"smoke test"}' \
    "${BASE_URL}/v8/room/${ROOM_ID}/safety/reset/request" || true)"
  RESET_ID="$(
    python3 - <<PY
import json
raw = ${RESET_JSON@Q}
j = json.loads(raw) if raw else {}
print(j.get("reset_id", ""))
PY
  )"
  if [[ -z "${RESET_ID}" ]]; then
    echo "WARN: safety reset request failed (requires TECH/ADMIN auth)" >&2
  else
    curl -fsS "${CURL_ARGS[@]}" "${AUTH_HEADER[@]}" -H "Content-Type: application/json" \
      -d "{\"reset_id\":\"${RESET_ID}\"}" \
      "${BASE_URL}/v8/room/${ROOM_ID}/safety/reset/confirm" >/dev/null || {
        echo "WARN: safety reset confirm failed (requires TECH/ADMIN auth and all devices SAFE)" >&2
      }
  fi
else
  echo "[3/5] POST /v8/room/${ROOM_ID}/control (PAUSE_DISPATCH then RESUME_DISPATCH)"
  curl -fsS "${CURL_ARGS[@]}" "${AUTH_HEADER[@]}" -H "Content-Type: application/json" \
    -d '{"op":"PAUSE_DISPATCH","parameters":{}}' \
    "${BASE_URL}/v8/room/${ROOM_ID}/control" >/dev/null
  curl -fsS "${CURL_ARGS[@]}" "${AUTH_HEADER[@]}" -H "Content-Type: application/json" \
    -d '{"op":"RESUME_DISPATCH","parameters":{}}' \
    "${BASE_URL}/v8/room/${ROOM_ID}/control" >/dev/null
fi
curl -fsS "${CURL_ARGS[@]}" "${AUTH_HEADER[@]}" -H "Content-Type: application/json" \
  -d '{"op":"RESUME_DISPATCH","parameters":{}}' \
  "${BASE_URL}/v8/room/${ROOM_ID}/control" >/dev/null || true
echo "OK"

echo "[4/5] POST /v8/room/${ROOM_ID}/audio/cue (optional; requires osc-bridge configured)"
CORR_ID="$(python3 - <<'PY'
import uuid
print(uuid.uuid4())
PY
)"
NOW_MS="$(python3 - <<'PY'
import time
print(int(time.time() * 1000))
PY
)"
PAYLOAD="$(
  python3 - <<PY
import json
room = ${ROOM_ID@Q}
print(json.dumps({
  "schema": "sentient-v8",
  "room_id": room,
  "cue_id": "smoke_test",
  "correlation_id": "${CORR_ID}",
  "address": "/sentient/smoke_test",
  "args": [{"type": "STRING", "value": "hello"}],
  "issued_at_unix_ms": ${NOW_MS},
}))
PY
)"
curl -fsS "${AUTH_HEADER[@]}" -H "Content-Type: application/json" \
  "${CURL_ARGS[@]}" \
  -d "${PAYLOAD}" \
  "${BASE_URL}/v8/room/${ROOM_ID}/audio/cue" >/dev/null || {
  echo "WARN: audio cue publish failed (osc-bridge may not be configured or API auth missing)" >&2
}

echo "[5/5] Done"
echo "Done."
