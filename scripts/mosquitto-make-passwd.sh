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
  docker_cmd=(docker run --rm -i eclipse-mosquitto:2.0 sh -c "mosquitto_passwd -b -c /tmp/pw \"${MQTT_USERNAME}\" \"${MQTT_PASSWORD}\" && cat /tmp/pw")

  # If the user is in the docker group but the current shell doesn't have it yet, prefer `sg docker`.
  if command -v sg >/dev/null 2>&1 && getent group docker >/dev/null 2>&1; then
    if ! id -nG | tr ' ' '\n' | grep -qx docker; then
      sg docker -c "${docker_cmd[*]} > '${out}'"
      chmod 600 "${out}"
      echo "Wrote ${out}"
      exit 0
    fi
  fi

  set +e
  "${docker_cmd[@]}" > "${out}"
  rc=$?
  set -e
  if [[ "${rc}" -ne 0 ]]; then
    echo "Error: docker failed (try re-login so your docker group applies, or install mosquitto_passwd)" >&2
    exit "${rc}"
  fi
else
  echo "Error: need 'mosquitto_passwd' (preferred) or 'docker' to generate password file" >&2
  exit 1
fi

chmod 600 "${out}"
echo "Wrote ${out}"
