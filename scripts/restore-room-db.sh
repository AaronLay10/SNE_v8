#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <room_id> <backup_file.sql.gz>" >&2
  exit 2
fi

ROOM_ID="$1"
BACKUP_FILE="$2"
ROOM_DIR="infra/rooms/${ROOM_ID}"
ENV_FILE="${ROOM_DIR}/.env"

if [[ ! -f "${BACKUP_FILE}" ]]; then
  echo "Missing backup file: ${BACKUP_FILE}" >&2
  exit 2
fi

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

DB_CID="$(docker ps --filter "label=com.docker.compose.project=${ROOM_ID}" --filter "label=com.docker.compose.service=timescaledb" -q | head -n1)"
if [[ -z "${DB_CID}" ]]; then
  echo "Could not find timescaledb container for compose project '${ROOM_ID}'. Is the room DB up?" >&2
  exit 2
fi

echo "Restoring room DB (${ROOM_ID}) from ${BACKUP_FILE} -> container ${DB_CID}"
echo "WARNING: this overwrites the existing database contents."

docker exec -e PGPASSWORD="${POSTGRES_PASSWORD}" "${DB_CID}" \
  sh -lc 'psql -U sentient -d postgres -v ON_ERROR_STOP=1 -c "DROP DATABASE IF EXISTS sentient_room;" -c "CREATE DATABASE sentient_room;"'

gzip -dc "${BACKUP_FILE}" \
  | docker exec -i -e PGPASSWORD="${POSTGRES_PASSWORD}" "${DB_CID}" \
    sh -lc 'psql -U sentient -d sentient_room -v ON_ERROR_STOP=1'

echo "OK: restore complete"

