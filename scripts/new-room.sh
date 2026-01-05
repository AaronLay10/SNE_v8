#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <room_id>" >&2
  exit 2
fi

room_id="$1"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

template_dir="${repo_root}/infra/compose/room-template"
rooms_dir="${repo_root}/infra/rooms"
target_dir="${rooms_dir}/${room_id}"

if [[ -e "${target_dir}" ]]; then
  echo "Error: ${target_dir} already exists" >&2
  exit 1
fi

mkdir -p "${rooms_dir}"
cp -R "${template_dir}" "${target_dir}"

if [[ ! -f "${target_dir}/.env" ]]; then
  cp "${target_dir}/.env.example" "${target_dir}/.env"
fi

perl -0pi -e "s/^ROOM_ID=.*/ROOM_ID=${room_id}/m" "${target_dir}/.env"
perl -0pi -e "s/^STACK_ID=.*/STACK_ID=${room_id}/m" "${target_dir}/.env" || true
perl -0pi -e "s|^MQTT_PUBLIC_HOSTNAME=.*|MQTT_PUBLIC_HOSTNAME=mqtt.${room_id}.sentientengine.ai|m" "${target_dir}/.env" || true

echo "Created room stack at: ${target_dir}"
echo "Next:"
echo "  1) Edit ${target_dir}/.env"
echo "  2) Generate Mosquitto password file:"
echo "       ${repo_root}/scripts/mosquitto-make-passwd.sh ${target_dir}/.env"
echo "  3) Run: docker compose --env-file .env up -d"
