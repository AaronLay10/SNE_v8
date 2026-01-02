# Host Bootstrap (Ubuntu 24.04)

This repo assumes the host can run Docker Compose and build Rust services into containers.

## Prereqs

- Ubuntu Server 24.04 LTS
- Ability to run `sudo` commands (interactive password is fine)

## 1) Install baseline packages

```bash
sudo apt-get update
sudo apt-get install -y git ca-certificates curl build-essential
```

## 2) Install Docker (Ubuntu packages)

This is the fastest bootstrap path. If you prefer Docker CE from Docker’s apt repo, decide that in `docs/DECISIONS.md`.

```bash
sudo apt-get install -y docker.io docker-compose-plugin
sudo usermod -aG docker "$USER"
```

Log out/in (or restart your SSH session) so the `docker` group applies.

## 3) Sanity check

```bash
docker --version
docker compose version
git --version
```

Optional: run `./scripts/check-prereqs.sh` for a quick summary.

## One-shot bootstrap script

If you prefer a single command, run:

```bash
sudo ./scripts/host-bootstrap-ubuntu24.sh
```
