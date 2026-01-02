#!/usr/bin/env bash
set -euo pipefail

missing=0

need() {
  local cmd="$1"
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo "missing: ${cmd}"
    missing=1
  else
    echo "ok: ${cmd} -> $(command -v "${cmd}")"
  fi
}

need git
need docker

if command -v docker >/dev/null 2>&1; then
  docker --version || true
  docker compose version || true
fi

need cargo
need rustc

if [[ "${missing}" -ne 0 ]]; then
  echo
  echo "One or more prerequisites are missing."
  echo "If this is the production host, start with: docs/HOST_BOOTSTRAP.md"
  exit 1
fi

