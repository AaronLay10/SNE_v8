#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run as root (example): sudo $0" >&2
  exit 2
fi

apt-get update

# Core tooling
apt-get install -y \
  git \
  ca-certificates \
  curl \
  build-essential

# Container runtime (Ubuntu packages)
apt-get install -y docker.io

# Prefer Docker Compose v2 integration (either package name depending on repo).
# Ubuntu noble typically provides `docker-compose-v2`, while Docker CE repos provide `docker-compose-plugin`.
apt-get install -y docker-compose-v2 || apt-get install -y docker-compose-plugin

# Docker Buildx (Compose may use Bake/buildx for builds)
apt-get install -y docker-buildx || true

# Optional but useful for early validation
apt-get install -y \
  mosquitto-clients

if [[ -n "${SUDO_USER:-}" ]]; then
  usermod -aG docker "${SUDO_USER}" || true
else
  usermod -aG docker techadmin || true
fi

echo
echo "Installed: git, build-essential, docker.io, docker compose v2, docker-buildx, mosquitto-clients"
echo "Note: re-login (or restart SSH) for docker group membership to apply (or run docker commands via sudo)."
