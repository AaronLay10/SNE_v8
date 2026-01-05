#!/usr/bin/env bash
set -euo pipefail

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

TS="$(date -u +%Y%m%dT%H%M%SZ)"
OUT_DIR="backups/auth-db"
OUT_FILE="${OUT_DIR}/auth-${TS}.sql.gz"
mkdir -p "${OUT_DIR}"

DB_CID="$(docker ps --filter "label=com.docker.compose.project=shared" --filter "label=com.docker.compose.service=auth-db" -q | head -n1)"
if [[ -z "${DB_CID}" ]]; then
  # Fallback: shared stack might not be named "shared" if started from a different directory/project.
  DB_CID="$(docker ps --filter "label=com.docker.compose.service=auth-db" -q | head -n1)"
fi

if [[ -z "${DB_CID}" ]]; then
  echo "Could not find auth-db container. Is infra/compose/shared up?" >&2
  exit 2
fi

echo "Backing up auth DB from container ${DB_CID} -> ${OUT_FILE}"

docker exec -e PGPASSWORD="${AUTH_DB_PASSWORD}" "${DB_CID}" \
  sh -lc 'pg_dump -U sentient -d sentient_auth --no-owner --no-privileges' \
  | gzip -9 > "${OUT_FILE}"

echo "OK: ${OUT_FILE}"

