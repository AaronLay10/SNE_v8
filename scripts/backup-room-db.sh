#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <room_id>" >&2
  exit 2
fi

ROOM_ID="$1"
ROOM_DIR="infra/rooms/${ROOM_ID}"
ENV_FILE="${ROOM_DIR}/.env"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "Missing ${ENV_FILE}. Create the room stack first (see docs/runbooks/ROOM_BRINGUP.md)." >&2
  exit 2
fi

# shellcheck disable=SC1090
set -a
source "${ENV_FILE}"
set +a

if [[ -z "${POSTGRES_PASSWORD:-}" ]]; then
  echo "POSTGRES_PASSWORD is empty in ${ENV_FILE}" >&2
  exit 2
fi

TS="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="backups/room-db/${ROOM_ID}"
OUT_FILE="${OUT_DIR}/${ROOM_ID}-${TS}.sql.gz"

mkdir -p "${OUT_DIR}"

# Rely on compose project label (directory name) to find the DB container.
DB_CID="$(docker ps --filter "label=com.docker.compose.project=${ROOM_ID}" --filter "label=com.docker.compose.service=timescaledb" -q | head -n1)"
if [[ -z "${DB_CID}" ]]; then
  echo "Could not find timescaledb container for compose project '${ROOM_ID}'. Is the room stack up?" >&2
  exit 2
fi

echo "Backing up room DB (${ROOM_ID}) from container ${DB_CID} -> ${OUT_FILE}"

docker exec -e PGPASSWORD="${POSTGRES_PASSWORD}" "${DB_CID}" \
  sh -lc 'pg_dump -U sentient -d sentient_room --no-owner --no-privileges' \
  | gzip -9 > "${OUT_FILE}"

echo "OK: ${OUT_FILE}"

