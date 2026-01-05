#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  core-control.sh --room ROOM_ID --op OP [options]

Required (flags or env):
  --room        ROOM_ID         (or ROOM_ID env)
  --op          PAUSE_DISPATCH|RESUME_DISPATCH|RESET_SAFETY_LATCH|START_GRAPH|STOP_GRAPH|RELOAD_GRAPH (or OP env)

MQTT (env defaults):
  --host        MQTT_HOST       (default: localhost)
  --port        MQTT_PORT       (default: 1883)
  --username    MQTT_USERNAME   (required)
  --password    MQTT_PASSWORD   (required)

Optional:
  --params      JSON_OBJECT     (default: {})
  --qos         0|1             (default: 1)
  --token       CORE_CONTROL_TOKEN (optional; if core is configured to require it)

Examples:
  MQTT_USERNAME=sentient MQTT_PASSWORD=... \
    scripts/core-control.sh --room room1 --op PAUSE_DISPATCH

  scripts/core-control.sh --room room1 --op RESUME_DISPATCH

  # Safety latch reset (TECH/Admin via authenticated API is preferred; MQTT is for commissioning)
  scripts/core-control.sh --room room1 --op RESET_SAFETY_LATCH

  # Graph ops
  scripts/core-control.sh --room room1 --op START_GRAPH
  scripts/core-control.sh --room room1 --op STOP_GRAPH
  scripts/core-control.sh --room room1 --op RELOAD_GRAPH
EOF
}

ROOM_ID="${ROOM_ID:-}"
OP="${OP:-}"

MQTT_HOST="${MQTT_HOST:-localhost}"
MQTT_PORT="${MQTT_PORT:-1883}"
MQTT_USERNAME="${MQTT_USERNAME:-}"
MQTT_PASSWORD="${MQTT_PASSWORD:-}"

PARAMS_JSON="${PARAMS_JSON:-{}}"
QOS="${QOS:-1}"
CORE_CONTROL_TOKEN="${CORE_CONTROL_TOKEN:-}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --room) ROOM_ID="${2:-}"; shift 2;;
    --op) OP="${2:-}"; shift 2;;
    --host) MQTT_HOST="${2:-}"; shift 2;;
    --port) MQTT_PORT="${2:-}"; shift 2;;
    --username) MQTT_USERNAME="${2:-}"; shift 2;;
    --password) MQTT_PASSWORD="${2:-}"; shift 2;;
    --params) PARAMS_JSON="${2:-}"; shift 2;;
    --qos) QOS="${2:-}"; shift 2;;
    --token) CORE_CONTROL_TOKEN="${2:-}"; shift 2;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2;;
  esac
done

if [[ -z "${ROOM_ID}" || -z "${OP}" ]]; then
  echo "Missing required args: --room/--op" >&2
  usage
  exit 2
fi
if [[ -z "${MQTT_USERNAME}" || -z "${MQTT_PASSWORD}" ]]; then
  echo "Missing MQTT creds: --username/--password (or MQTT_USERNAME/MQTT_PASSWORD env)" >&2
  exit 2
fi

TOPIC="room/${ROOM_ID}/core/control"

PAYLOAD="$(
  ROOM_ID="${ROOM_ID}" OP="${OP}" PARAMS_JSON="${PARAMS_JSON}" CORE_CONTROL_TOKEN="${CORE_CONTROL_TOKEN}" \
  python3 - <<'PY'
import json
import os
import sys
import time

room_id = os.environ["ROOM_ID"]
op = os.environ["OP"]
params_raw = os.environ.get("PARAMS_JSON", "{}")
token = os.environ.get("CORE_CONTROL_TOKEN", "").strip()

try:
  params = json.loads(params_raw) if params_raw.strip() else {}
except Exception as e:
  print(f"Invalid --params JSON: {e}", file=sys.stderr)
  sys.exit(2)

if not isinstance(params, dict):
  print("--params must be a JSON object", file=sys.stderr)
  sys.exit(2)

if token:
  params.setdefault("token", token)

msg = {
  "schema": "v8",
  "room_id": room_id,
  "op": op,
  "parameters": params,
  "requested_at_unix_ms": int(time.time() * 1000),
}
print(json.dumps(msg, separators=(",", ":")))
PY
)"

if command -v mosquitto_pub >/dev/null 2>&1; then
  mosquitto_pub -h "${MQTT_HOST}" -p "${MQTT_PORT}" -u "${MQTT_USERNAME}" -P "${MQTT_PASSWORD}" \
    -q "${QOS}" -t "${TOPIC}" -m "${PAYLOAD}"
elif command -v docker >/dev/null 2>&1; then
  docker run --rm --network host eclipse-mosquitto:2.0 \
    mosquitto_pub -h "${MQTT_HOST}" -p "${MQTT_PORT}" -u "${MQTT_USERNAME}" -P "${MQTT_PASSWORD}" \
    -q "${QOS}" -t "${TOPIC}" -m "${PAYLOAD}"
else
  echo "mosquitto_pub not found (and docker not available for fallback)" >&2
  exit 127
fi

echo "Published control to ${TOPIC}: ${OP}"
