#!/usr/bin/env bash
set -euo pipefail

env_file="${1:-.env}"
if [[ ! -f "${env_file}" ]]; then
  echo "Usage: $0 <path-to-room-.env>   (default: .env in CWD)" >&2
  exit 2
fi

room_dir="$(cd "$(dirname "${env_file}")" && pwd)"

set -a
# shellcheck disable=SC1090
source "${env_file}"
set +a

: "${MQTT_USERNAME:?MQTT_USERNAME is required in ${env_file}}"
: "${MQTT_PASSWORD:?MQTT_PASSWORD is required in ${env_file}}"

mkdir -p "${room_dir}/mosquitto"
out="${room_dir}/mosquitto/passwd"

if command -v mosquitto_passwd >/dev/null 2>&1; then
  mosquitto_passwd -b -c "${out}" "${MQTT_USERNAME}" "${MQTT_PASSWORD}"
elif command -v docker >/dev/null 2>&1; then
  docker run --rm -i eclipse-mosquitto:2.0 sh -c \
    "mosquitto_passwd -b -c /tmp/pw \"${MQTT_USERNAME}\" \"${MQTT_PASSWORD}\" && cat /tmp/pw" > "${out}"
else
  echo "Error: need 'mosquitto_passwd' (preferred) or 'docker' to generate password file" >&2
  exit 1
fi

chmod 600 "${out}"
echo "Wrote ${out}"

