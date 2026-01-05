#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="${repo_root}/infra/compose/shared/.env.local"

if [[ -f "${target}" ]]; then
  echo "Refusing to overwrite existing ${target}" >&2
  exit 1
fi

gen() {
  python3 - <<'PY'
import secrets
print(secrets.token_urlsafe(24))
PY
}

jwt() {
  python3 - <<'PY'
import secrets
print(secrets.token_urlsafe(48))
PY
}

cat >"${target}" <<EOF
GRAFANA_ADMIN_USER=admin
GRAFANA_ADMIN_PASSWORD=$(gen)

TRAEFIK_DASHBOARD_HOSTNAME=traefik.sentientengine.ai
GRAFANA_HOSTNAME=grafana.sentientengine.ai
AUTH_HOSTNAME=auth.sentientengine.ai
TECH_UI_HOSTNAME=tech.sentientengine.ai
CREATIVE_UI_HOSTNAME=creative.sentientengine.ai
GM_UI_HOSTNAME=mythra.sentientengine.ai

AUTH_DB_PASSWORD=$(gen)
SENTIENT_JWT_SECRET=$(jwt)
AUTH_BOOTSTRAP_TOKEN=$(gen)

AUTH_MIN_PASSWORD_LEN=12
AUTH_JWT_TTL_SECONDS=43200
AUTH_LOGIN_RATE_LIMIT_PER_WINDOW=10
AUTH_LOGIN_RATE_LIMIT_WINDOW_SECONDS=60
EOF

chmod 600 "${target}"
echo "Wrote ${target}"
echo "Next: cd infra/compose/shared && docker compose --env-file .env.local up -d --build"

