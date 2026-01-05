#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run as root (example): sudo $0" >&2
  exit 2
fi

apt-get update
apt-get install -y chrony

# Stop systemd-timesyncd to avoid two NTP clients fighting over clock discipline.
systemctl disable --now systemd-timesyncd.service || true

conf="/etc/chrony/chrony.conf"

# Ensure we have upstream sources (Ubuntu defaults often include pool lines already).
if ! grep -Eq '^\s*(pool|server)\s+' "${conf}"; then
  cat >>"${conf}" <<'EOF'
pool ntp.ubuntu.com iburst
EOF
fi

# Allow LAN clients (tighten to exact VLAN CIDRs once the IP plan is finalized).
if ! grep -Eq '^\s*allow\s+10\.0\.0\.0/8' "${conf}"; then
  cat >>"${conf}" <<'EOF'

# Sentient v8: allow room VLAN clients to use this host as NTP source.
# TODO: tighten these to exact room/admin VLAN CIDRs once finalized.
allow 10.0.0.0/8
allow 172.16.0.0/12
allow 192.168.0.0/16
EOF
fi

# If upstream is temporarily unavailable, serve local time at a high stratum rather than failing hard.
if ! grep -Eq '^\s*local\s+stratum\s+10' "${conf}"; then
  cat >>"${conf}" <<'EOF'

# Serve local time if isolated (high stratum to avoid being preferred over real sources).
local stratum 10
EOF
fi

systemctl enable --now chrony.service

echo
echo "chrony installed and enabled."
echo "Next checks:"
echo "  chronyc tracking"
echo "  chronyc sources -v"
echo "  ss -ulpn | rg ':123\\b'"

