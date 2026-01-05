#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <backup_file.sql.gz>" >&2
  exit 2
fi

BACKUP_FILE="$1"

if [[ ! -f "${BACKUP_FILE}" ]]; then
  echo "Missing backup file: ${BACKUP_FILE}" >&2
  exit 2
fi

ENV_FILE="infra/compose/shared/.env"
if [[ -f "${ENV_FILE}" ]]; then
  # shellcheck disable=SC1090
  set -a
  source "${ENV_FILE}"
  set +a
fi

if [[ -z "${AUTH_DB_PASSWORD:-}" ]]; then
  echo "AUTH_DB_PASSWORD is not set. Set it in infra/compose/shared/.env (see infra/compose/shared/.env.example)." >&2
  exit 2
fi

DB_CID="$(docker ps --filter "label=com.docker.compose.service=auth-db" -q | head -n1)"
if [[ -z "${DB_CID}" ]]; then
  echo "Could not find auth-db container. Is infra/compose/shared up?" >&2
  exit 2
fi

echo "Restoring auth DB from ${BACKUP_FILE} -> container ${DB_CID}"
echo "WARNING: this overwrites the existing sentient_auth database."

docker exec -e PGPASSWORD="${AUTH_DB_PASSWORD}" "${DB_CID}" \
  sh -lc 'psql -U sentient -d postgres -v ON_ERROR_STOP=1 -c "DROP DATABASE IF EXISTS sentient_auth;" -c "CREATE DATABASE sentient_auth;"'

gzip -dc "${BACKUP_FILE}" \
  | docker exec -i -e PGPASSWORD="${AUTH_DB_PASSWORD}" "${DB_CID}" \
    sh -lc 'psql -U sentient -d sentient_auth -v ON_ERROR_STOP=1'

echo "OK: restore complete"

