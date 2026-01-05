#!/usr/bin/env python3
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


RE_DEVICE = re.compile(r'constexpr\s+const\s+char\s+\*\s*(DEV_[A-Z0-9_]+)\s*=\s*"([^"]+)"\s*;')


@dataclass(frozen=True)
class Controller:
    src_dir: Path
    dst_dir: Path
    controller_id: str
    devices: list[str]


def _controller_id_from_dirname(dirname: str) -> str:
    if dirname.endswith("_v2"):
        return dirname[: -len("_v2")]
    if dirname.endswith("_v8"):
        return dirname[: -len("_v8")]
    return dirname


def _dst_dirname(dirname: str) -> str:
    if dirname.endswith("_v2"):
        return dirname[: -len("_v2")] + "_v8"
    if dirname.endswith("_v8"):
        return dirname
    return dirname + "_v8"


def _parse_devices(controller_naming_h: Path) -> list[str]:
    text = controller_naming_h.read_text(encoding="utf-8", errors="replace")
    devices: list[str] = []
    for _, device_id in RE_DEVICE.findall(text):
        devices.append(device_id.strip())
    # Preserve order, drop duplicates
    out: list[str] = []
    seen: set[str] = set()
    for d in devices:
        if d in seen:
            continue
        seen.add(d)
        out.append(d)
    return out


def _cpp_ident(s: str) -> str:
    s = re.sub(r"[^a-zA-Z0-9_]", "_", s)
    s = re.sub(r"__+", "_", s)
    if not s or s[0].isdigit():
        s = "d_" + s
    return s.lower()


def _render_stub(controller: Controller) -> str:
    sketch_name = controller.dst_dir.name
    firmware_version = f"{sketch_name}-stub"

    # v8 device_id must be unique within room; prefix with controller_id.
    device_ids = [f"{controller.controller_id}_{d}" for d in controller.devices]

    decls_cfg = []
    decls_client = []
    handler_sets = []
    state_publish = []
    last_state_init = []

    for i, dev in enumerate(device_ids):
        ident = _cpp_ident(dev)
        decls_cfg.append(f'static sentient_v8::Config cfg_{ident} = make_cfg("{dev}", HMAC_KEY_PLACEHOLDER);')
        decls_client.append(f"static sentient_v8::Client sne_{ident}(cfg_{ident});")
        handler_sets.append(f"  sne_{ident}.setCommandHandler(handleCommand, &ctx[{i}]);")
        state_publish.append(f"  publish_state({i});")
        last_state_init.append("0")

    clients_array = ", ".join([f"&sne_{_cpp_ident(dev)}" for dev in device_ids])

    return f"""// {sketch_name} — v8 Stub (Teensy 4.1)
//
// Permanent v8 MQTT integration scaffold:
// - Per logical sub-device: unique v8 `device_id` (Option 2)
// - One MQTT connection per `device_id` (required for correct LWT OFFLINE semantics)
// - Commands require `parameters.op` (string) for dispatch routing

#include <Arduino.h>
#include <ArduinoJson.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
// Room ID is a compile-time string literal so MQTT_BROKER_HOST can be derived.
#define ROOM_ID "room1"

// Per-room broker hostname: mqtt.<room>.sentientengine.ai
#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC key, hex encoded (64 chars). Replace per device during provisioning.
static const char *HMAC_KEY_PLACEHOLDER = "0000000000000000000000000000000000000000000000000000000000000000";

static sentient_v8::Config make_cfg(const char *device_id, const char *hmac_key_hex) {{
  sentient_v8::Config c;
  c.brokerIp = IPAddress(0, 0, 0, 0);
  c.brokerHost = MQTT_BROKER_HOST;
  c.brokerPort = MQTT_PORT;
  c.username = MQTT_USERNAME;
  c.password = MQTT_PASSWORD;
  c.roomId = ROOM_ID;
  c.deviceId = device_id;
  c.deviceHmacKeyHex = hmac_key_hex;
  c.keepAliveSeconds = 10;
  c.reconnectDelayMs = 1000;
  c.heartbeatIntervalMs = 2000;
  c.rxJsonCapacity = 2048;
  c.txJsonCapacity = 2048;
  return c;
}}

{chr(10).join(decls_cfg)}
{chr(10).join(decls_client)}

static sentient_v8::Client *clients[] = {{{clients_array}}};

struct DeviceCtx {{
  int idx;
}};
static DeviceCtx ctx[{len(device_ids)}] = {{{", ".join([f"{{{i}}}" for i in range(len(device_ids))])}}};

static unsigned long last_state_publish[{len(device_ids)}] = {{{", ".join(last_state_init)}}};

static void ensure_ethernet_dhcp() {{
#if !defined(ESP32)
  static bool started = false;
  if (started) return;
  byte mac[6];
  teensyMAC(mac);
  Ethernet.begin(mac);
  delay(250);
  started = true;
#endif
}}

static void publish_state(int idx) {{
  if (idx < 0) return;
  if ((size_t)idx >= (sizeof(clients) / sizeof(clients[0]))) return;

  StaticJsonDocument<256> st;
  st["is_stub"] = true;
  st["firmware_version"] = "{firmware_version}";
  st["last_op"] = "boot";
  clients[idx]->publishState(st);
  last_state_publish[idx] = millis();
}}

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void *vctx) {{
  DeviceCtx *dctx = (DeviceCtx *)vctx;
  int idx = dctx ? dctx->idx : -1;
  if (idx < 0) {{
    rejectedAckReason["reason_code"] = "INTERNAL_ERROR";
    return false;
  }}

  const char *action = cmd["action"] | "";
  if (strcmp(action, "SET") != 0) {{
    rejectedAckReason["reason_code"] = "UNSUPPORTED_ACTION";
    return false;
  }}

  JsonVariantConst p = cmd["parameters"];
  const char *op = p["op"] | "";
  if (!op || !op[0]) {{
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }}

  // Stub ops:
  // - op=noop (accept)
  // - op=set_power_led with {{\"on\": true|false}} (safe commissioning indicator)
  if (strcmp(op, "set_power_led") == 0) {{
    if (!p.containsKey("on")) {{
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }}
    bool on = p["on"] | false;
    pinMode(13, OUTPUT);
    digitalWrite(13, on ? HIGH : LOW);
  }} else if (strcmp(op, "noop") != 0) {{
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }}

  StaticJsonDocument<256> st;
  st["is_stub"] = true;
  st["firmware_version"] = "{firmware_version}";
  st["last_op"] = op;
  clients[idx]->publishState(st);
  last_state_publish[idx] = millis();
  return true;
}}

void setup() {{
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) {{
    if (!clients[i]->begin()) {{
      while (true) delay(1000);
    }}
  }}

{chr(10).join(handler_sets)}

{chr(10).join(state_publish)}
}}

void loop() {{
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) {{
    clients[i]->loop();
  }}

  const unsigned long now = millis();
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) {{
    if (now - last_state_publish[i] > 60UL * 1000UL) {{
      publish_state((int)i);
    }}
  }}
}}
"""


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    controllers_root = root / "hardware" / "Controller Code Teensy"

    controllers: list[Controller] = []
    for src in sorted(controllers_root.iterdir()):
        if not src.is_dir():
            continue
        if src.name.endswith("_v8"):
            continue
        naming = src / "controller_naming.h"
        if not naming.exists():
            continue

        controller_id = _controller_id_from_dirname(src.name)
        devices = _parse_devices(naming) or [controller_id]

        dst = controllers_root / _dst_dirname(src.name)
        controllers.append(Controller(src_dir=src, dst_dir=dst, controller_id=controller_id, devices=devices))

    created = 0
    skipped = 0
    for c in controllers:
        if c.dst_dir.exists():
            skipped += 1
            continue
        c.dst_dir.mkdir(parents=True, exist_ok=True)
        ino_path = c.dst_dir / f"{c.dst_dir.name}.ino"
        ino_path.write_text(_render_stub(c), encoding="utf-8")
        created += 1

    print(f"Created {created} v8 stub(s); skipped {skipped} existing.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
