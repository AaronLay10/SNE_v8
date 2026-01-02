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
apt-get install -y \
  docker.io \
  docker-compose-v2

# Optional but useful for early validation
apt-get install -y \
  mosquitto-clients

usermod -aG docker techadmin || true

echo
echo "Installed: git, build-essential, docker.io, docker-compose-plugin, mosquitto-clients"
echo "Note: re-login (or restart SSH) for docker group membership to apply."
