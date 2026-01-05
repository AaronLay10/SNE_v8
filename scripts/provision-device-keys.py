#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import secrets
from pathlib import Path


def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Provision Sentient v8 per-device HMAC keys (hex).")
    p.add_argument("--room", required=True, help="Room ID (e.g. clockwork)")
    p.add_argument("--device", action="append", default=[], help="Device ID (repeatable)")
    p.add_argument("--device-file", help="Text file with one device_id per line")
    p.add_argument(
        "--out",
        help="Output JSON path (default: infra/secrets/<room>/device_hmac_keys.json)",
    )
    p.add_argument("--force", action="store_true", help="Overwrite if output file exists")
    p.add_argument(
        "--print-env",
        action="store_true",
        help="Print DEVICE_HMAC_KEYS_JSON=... (single line) for pasting into a room .env",
    )
    return p.parse_args()


def _load_devices(args: argparse.Namespace) -> list[str]:
    devices: list[str] = []
    for d in args.device:
        d = d.strip()
        if d:
            devices.append(d)

    if args.device_file:
        path = Path(args.device_file)
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            devices.append(line)

    # de-dupe while preserving order
    out: list[str] = []
    seen: set[str] = set()
    for d in devices:
        if d in seen:
            continue
        seen.add(d)
        out.append(d)
    return out


def main() -> int:
    args = _parse_args()
    repo_root = Path(__file__).resolve().parents[1]

    devices = _load_devices(args)
    if not devices:
        raise SystemExit("No devices provided. Use --device ... or --device-file ...")

    out_path = (
        Path(args.out)
        if args.out
        else repo_root / "infra" / "secrets" / args.room / "device_hmac_keys.json"
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)

    if out_path.exists() and not args.force:
        raise SystemExit(f"Refusing to overwrite existing file: {out_path} (use --force)")

    # 32-byte key per device_id, hex encoded (64 chars)
    mapping: dict[str, str] = {d: secrets.token_bytes(32).hex() for d in devices}

    out_path.write_text(json.dumps(mapping, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"Wrote: {out_path}")
    print(f"Devices: {len(devices)}")

    if args.print_env:
        env_json = json.dumps(mapping, separators=(",", ":"), sort_keys=True)
        print()
        print(f"DEVICE_HMAC_KEYS_JSON={env_json}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

