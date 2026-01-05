#!/usr/bin/env bash
set -euo pipefail

# Wrapper to run docker compose without requiring Buildx/Bake.
# If buildx is installed, Compose will ignore COMPOSE_BAKE=false and use buildx normally.

export COMPOSE_BAKE="${COMPOSE_BAKE:-false}"

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <compose-dir> [--env-file PATH] <compose args...>" >&2
  echo "Example: $0 infra/compose/shared --env-file .env.local up -d --build" >&2
  exit 2
fi

compose_dir="$1"
shift

if [[ ! -d "${compose_dir}" ]]; then
  echo "Not a directory: ${compose_dir}" >&2
  exit 2
fi

if command -v sg >/dev/null 2>&1 && getent group docker >/dev/null 2>&1; then
  if ! id -nG | tr ' ' '\n' | grep -qx docker; then
    sg docker -c "cd '${compose_dir}' && docker compose $*"
    exit 0
  fi
fi

(cd "${compose_dir}" && docker compose "$@")
