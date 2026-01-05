# Runbook — Auth (sentient-auth) (v8)

Sentient v8 uses a shared auth service (`sentient-auth`) that issues JWTs (HS256) for room APIs/UIs.

## 1) Configure shared auth env

Set these environment variables for `infra/compose/shared` (e.g. via an `.env` file in that folder):

- `AUTH_DB_PASSWORD` (Postgres password)
- `SENTIENT_JWT_SECRET` (shared secret; must be >= 16 chars)
- `AUTH_BOOTSTRAP_TOKEN` (one-time bootstrap bearer token; leave empty after bootstrapping to disable `/v8/auth/bootstrap`)
- Optional hardening:
  - `AUTH_MIN_PASSWORD_LEN` (default: 12)
  - `AUTH_JWT_TTL_SECONDS` (default: 43200)
  - `AUTH_LOGIN_RATE_LIMIT_PER_WINDOW` (default: 10)
  - `AUTH_LOGIN_RATE_LIMIT_WINDOW_SECONDS` (default: 60)

Bring up shared stack:

```bash
cd infra/compose/shared
docker compose --env-file .env.local up -d
```

Health check:

- `GET /health` (returns `ok`)

## 2) Bootstrap first admin user

Bootstrap is only allowed when **no users exist** in the auth DB.

```bash
curl -sS -X POST "https://auth.sentientengine.ai/v8/auth/bootstrap" \
  -H "Authorization: Bearer <AUTH_BOOTSTRAP_TOKEN>" \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"changeme","role":"ADMIN"}'
```

After bootstrapping, clear `AUTH_BOOTSTRAP_TOKEN` and restart the shared stack to disable the endpoint.

## 3) Login to get a JWT

```bash
curl -sS -X POST "https://auth.sentientengine.ai/v8/auth/login" \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"changeme"}'
```

Use the returned `access_token` as:

- `Authorization: Bearer <token>`

## 4) Configure room APIs to accept JWTs

In each room `.env`, set:

- `SENTIENT_JWT_SECRET` (must match shared)

Room API will then accept JWTs in addition to (optional) `API_TOKEN`.

## 5) Admin user management (ADMIN only)

Create user:

```bash
curl -sS -X POST "https://auth.sentientengine.ai/v8/auth/users" \
  -H "Authorization: Bearer <ADMIN_JWT>" \
  -H "Content-Type: application/json" \
  -d '{"username":"tech1","password":"changeme","role":"TECH"}'
```

List users:

```bash
curl -sS "https://auth.sentientengine.ai/v8/auth/users" \
  -H "Authorization: Bearer <ADMIN_JWT>"
```

Disable/enable user:

```bash
curl -sS -X POST "https://auth.sentientengine.ai/v8/auth/users/<username>/enabled" \
  -H "Authorization: Bearer <ADMIN_JWT>" \
  -H "Content-Type: application/json" \
  -d '{"enabled":false}'
```

Reset password:

```bash
curl -sS -X POST "https://auth.sentientengine.ai/v8/auth/users/<username>/password" \
  -H "Authorization: Bearer <ADMIN_JWT>" \
  -H "Content-Type: application/json" \
  -d '{"password":"new-strong-password"}'
```
