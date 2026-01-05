#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  core-dispatch.sh --room ROOM_ID --device DEVICE_ID --action ACTION [options]

Required (flags or env):
  --room        ROOM_ID         (or ROOM_ID env)
  --device      DEVICE_ID       (or DEVICE_ID env)
  --action      OPEN|CLOSE|MOVE|SET (or ACTION env)

MQTT (env defaults):
  --host        MQTT_HOST       (default: localhost)
  --port        MQTT_PORT       (default: 1883)
  --username    MQTT_USERNAME   (required)
  --password    MQTT_PASSWORD   (required)

Optional:
  --safety      CRITICAL|NON_CRITICAL (default: NON_CRITICAL)
  --params      JSON_OBJECT           (default: {})
  --qos         0|1                   (default: 1)

Examples:
  MQTT_USERNAME=sentient MQTT_PASSWORD=... \
    scripts/core-dispatch.sh --room room1 --device sim1 --action SET --params '{"target":"dropPanelFuse","value":"UNLOCK"}'

  scripts/core-dispatch.sh --room room1 --device sim1 --action SET \
    --params '{"note":"gm override"}' --safety NON_CRITICAL
EOF
}

ROOM_ID="${ROOM_ID:-}"
DEVICE_ID="${DEVICE_ID:-}"
ACTION="${ACTION:-}"

MQTT_HOST="${MQTT_HOST:-localhost}"
MQTT_PORT="${MQTT_PORT:-1883}"
MQTT_USERNAME="${MQTT_USERNAME:-}"
MQTT_PASSWORD="${MQTT_PASSWORD:-}"

SAFETY_CLASS="${SAFETY_CLASS:-NON_CRITICAL}"
PARAMS_JSON="${PARAMS_JSON:-{}}"
QOS="${QOS:-1}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --room) ROOM_ID="${2:-}"; shift 2;;
    --device) DEVICE_ID="${2:-}"; shift 2;;
    --host) MQTT_HOST="${2:-}"; shift 2;;
    --port) MQTT_PORT="${2:-}"; shift 2;;
    --username) MQTT_USERNAME="${2:-}"; shift 2;;
    --password) MQTT_PASSWORD="${2:-}"; shift 2;;
    --safety) SAFETY_CLASS="${2:-}"; shift 2;;
    --action) ACTION="${2:-}"; shift 2;;
    --params) PARAMS_JSON="${2:-}"; shift 2;;
    --qos) QOS="${2:-}"; shift 2;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2;;
  esac
done

if [[ -z "${ROOM_ID}" || -z "${DEVICE_ID}" || -z "${ACTION}" ]]; then
  echo "Missing required args: --room/--device/--action" >&2
  usage
  exit 2
fi
if [[ -z "${MQTT_USERNAME}" || -z "${MQTT_PASSWORD}" ]]; then
  echo "Missing MQTT creds: --username/--password (or MQTT_USERNAME/MQTT_PASSWORD env)" >&2
  exit 2
fi

TOPIC="room/${ROOM_ID}/core/dispatch"

PAYLOAD="$(
  ROOM_ID="${ROOM_ID}" DEVICE_ID="${DEVICE_ID}" \
  SAFETY_CLASS="${SAFETY_CLASS}" ACTION="${ACTION}" PARAMS_JSON="${PARAMS_JSON}" \
  python3 - <<'PY'
import json
import os
import sys

room_id = os.environ["ROOM_ID"]
device_id = os.environ["DEVICE_ID"]
safety_class = os.environ["SAFETY_CLASS"]
action = os.environ["ACTION"]
params_raw = os.environ.get("PARAMS_JSON", "{}")

try:
  params = json.loads(params_raw) if params_raw.strip() else {}
except Exception as e:
  print(f"Invalid --params JSON: {e}", file=sys.stderr)
  sys.exit(2)

if not isinstance(params, dict):
  print("--params must be a JSON object", file=sys.stderr)
  sys.exit(2)

msg = {
  "schema": "v8",
  "room_id": room_id,
  "device_id": device_id,
  "action": action,
  "parameters": params,
  "safety_class": safety_class,
}
print(json.dumps(msg, separators=(",", ":")))
PY
)"

if command -v mosquitto_pub >/dev/null 2>&1; then
  mosquitto_pub -h "${MQTT_HOST}" -p "${MQTT_PORT}" -u "${MQTT_USERNAME}" -P "${MQTT_PASSWORD}" \
    -q "${QOS}" -t "${TOPIC}" -m "${PAYLOAD}"
elif command -v docker >/dev/null 2>&1; then
  # Fallback: run mosquitto_pub from a container (uses host networking by default).
  docker run --rm --network host eclipse-mosquitto:2.0 \
    mosquitto_pub -h "${MQTT_HOST}" -p "${MQTT_PORT}" -u "${MQTT_USERNAME}" -P "${MQTT_PASSWORD}" \
    -q "${QOS}" -t "${TOPIC}" -m "${PAYLOAD}"
else
  echo "mosquitto_pub not found (and docker not available for fallback)" >&2
  exit 127
fi

echo "Published dispatch to ${TOPIC}: ${DEVICE_ID} <- ${ACTION}"
