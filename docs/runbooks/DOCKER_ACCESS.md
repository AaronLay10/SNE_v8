# Runbook — Docker Access (Host) (v8)

Sentient v8 services run in Docker containers. If Docker commands fail with:

- `permission denied while trying to connect to the Docker daemon socket`

…your SSH session likely does not yet have the updated group membership.

## Option A (recommended): re-login

1. Ensure your user is in the `docker` group:

```bash
getent group docker
```

2. Log out and SSH back in (or restart the SSH session).
3. Verify:

```bash
docker ps
```

## Option B: use sudo

Use `sudo docker ...` / `sudo docker compose ...` until you re-login.

## Option C: `sg docker` (no re-login)

If your user is already in the `docker` group but your current SSH session isn’t, you can run Docker commands via a subshell:

```bash
sg docker -c 'docker ps'
sg docker -c 'docker compose up -d --build'
```

## Buildx note

Some `docker compose build` flows use BuildKit/Bake; if you see:

- `Docker Compose is configured to build using Bake, but buildx isn't installed`

Either install Buildx:

```bash
sudo apt-get install -y docker-buildx
```

…or temporarily disable Bake for the session:

```bash
export COMPOSE_BAKE=false
```

Or use the helper wrapper:

```bash
./scripts/compose.sh infra/compose/shared --env-file .env.local up -d --build
```
