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

primary_dir="${rooms_dir}/${room_id}-primary"
standby_dir="${rooms_dir}/${room_id}-standby"

if [[ -e "${primary_dir}" || -e "${standby_dir}" ]]; then
  echo "Error: ${primary_dir} or ${standby_dir} already exists" >&2
  exit 1
fi

mkdir -p "${rooms_dir}"
cp -R "${template_dir}" "${primary_dir}"
cp -R "${template_dir}" "${standby_dir}"

cp "${primary_dir}/.env.example" "${primary_dir}/.env"
cp "${standby_dir}/.env.example" "${standby_dir}/.env"

perl -0pi -e "s/^ROOM_ID=.*/ROOM_ID=${room_id}/m" "${primary_dir}/.env"
perl -0pi -e "s/^ROOM_ID=.*/ROOM_ID=${room_id}/m" "${standby_dir}/.env"

perl -0pi -e "s/^STACK_ID=.*/STACK_ID=${room_id}_primary/m" "${primary_dir}/.env"
perl -0pi -e "s/^STACK_ID=.*/STACK_ID=${room_id}_standby/m" "${standby_dir}/.env"

# Controllers should connect to a stable per-room DNS name (manual recovery if broker goes down).
perl -0pi -e "s|^MQTT_PUBLIC_HOSTNAME=.*|MQTT_PUBLIC_HOSTNAME=mqtt.${room_id}.sentientengine.ai|m" "${primary_dir}/.env" || true
perl -0pi -e "s|^MQTT_PUBLIC_HOSTNAME=.*|MQTT_PUBLIC_HOSTNAME=mqtt.${room_id}.sentientengine.ai|m" "${standby_dir}/.env" || true

# If both stacks are on the same host, use distinct ports for standby by default.
perl -0pi -e "s/^MQTT_PORT=.*/MQTT_PORT=1884/m" "${standby_dir}/.env"
perl -0pi -e "s/^DB_PORT=.*/DB_PORT=5433/m" "${standby_dir}/.env"

# Standby should not dispatch commands until manually promoted.
perl -0pi -e "s/^CORE_DISPATCH_ENABLED=.*/CORE_DISPATCH_ENABLED=false/m" "${standby_dir}/.env"

echo "Created:"
echo "  Primary: ${primary_dir}"
echo "  Standby: ${standby_dir}"
echo
echo "Next:"
echo "  1) Edit both .env files (MQTT creds, Postgres password, SCS host)"
echo "  2) Generate Mosquitto password files:"
echo "       ${repo_root}/scripts/mosquitto-make-passwd.sh ${primary_dir}/.env"
echo "       ${repo_root}/scripts/mosquitto-make-passwd.sh ${standby_dir}/.env"
echo "  3) Bring up primary:"
echo "       (cd ${primary_dir} && docker compose --env-file .env up -d --build)"
echo "  4) Bring up standby:"
echo "       (cd ${standby_dir} && docker compose --env-file .env up -d --build)"
