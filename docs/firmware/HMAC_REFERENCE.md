# HMAC Command Auth — Firmware Reference (v8)

This file is a practical reference for implementing `docs/protocol/AUTH_HMAC.md` in firmware (C/C++/Arduino) or Raspberry Pi agents (Python).

The v8 MAC is:

- Algorithm: HMAC-SHA256
- Output: hex lowercase (recommended)
- Input: UTF-8 bytes of the canonical signing string

---

## 1) Canonical Signing String (v1)

Build the signing string using `\n` separators in this exact order:

1. `schema=<schema>`
2. `room_id=<room_id>`
3. `device_id=<device_id>`
4. `command_id=<uuid>`
5. `correlation_id=<uuid>`
6. `sequence=<u64>`
7. `issued_at_unix_ms=<u64>`
8. `action=<action>` (enum string as sent, ex: `OPEN`)
9. `safety_class=<safety_class>` (ex: `CRITICAL`)
10. `parameters=<canonical_parameters_json>`

If `parameters` is absent, treat it as `{}`.

---

## 2) Canonical Parameters JSON

Canonicalize the `parameters` object by:

- Sorting object keys lexicographically at every level
- Keeping array order unchanged
- Serializing with no extra whitespace

Example:

Input:

```json
{"b":2,"a":{"d":4,"c":3}}
```

Canonical JSON:

```json
{"a":{"c":3,"d":4},"b":2}
```

---

## 3) Arduino/C++ Pseudocode

```cpp
String canonical_parameters_json(const JsonVariantConst& params);
String signing_string(const CommandEnvelope& cmd) {
  String p = canonical_parameters_json(cmd.parameters);
  String s;
  s += "schema=" + cmd.schema + "\n";
  s += "room_id=" + cmd.room_id + "\n";
  s += "device_id=" + cmd.device_id + "\n";
  s += "command_id=" + cmd.command_id + "\n";
  s += "correlation_id=" + cmd.correlation_id + "\n";
  s += "sequence=" + String(cmd.sequence) + "\n";
  s += "issued_at_unix_ms=" + String(cmd.issued_at_unix_ms) + "\n";
  s += "action=" + cmd.action + "\n";
  s += "safety_class=" + cmd.safety_class + "\n";
  s += "parameters=" + p;
  return s;
}

bool verify_hmac(const CommandEnvelope& cmd, const uint8_t* key, size_t key_len) {
  if (!cmd.auth_present) return false;
  if (cmd.auth_alg != "HMAC-SHA256") return false;
  String s = signing_string(cmd);
  uint8_t mac[32];
  hmac_sha256(key, key_len, (uint8_t*)s.c_str(), s.length(), mac);
  String mac_hex = to_hex_lower(mac, 32);
  return constant_time_eq_hex(mac_hex, cmd.auth_mac_hex);
}
```

Implementation notes:

- Use a real HMAC-SHA256 implementation (mbedTLS, BearSSL, Crypto library).
- Avoid `String` allocations in tight loops; a fixed buffer is better.
- Use constant-time compare for MAC hex.

---

## 4) Python Reference

```py
import hmac, hashlib, json

def canonicalize_json(value):
    if isinstance(value, dict):
        return {k: canonicalize_json(value[k]) for k in sorted(value.keys())}
    if isinstance(value, list):
        return [canonicalize_json(v) for v in value]
    return value

def canonical_parameters_json(parameters):
    canon = canonicalize_json(parameters or {})
    return json.dumps(canon, separators=(",", ":"), ensure_ascii=False)

def signing_string(cmd):
    p = canonical_parameters_json(cmd.get("parameters"))
    parts = [
        f"schema={cmd['schema']}",
        f"room_id={cmd['room_id']}",
        f"device_id={cmd['device_id']}",
        f"command_id={cmd['command_id']}",
        f"correlation_id={cmd['correlation_id']}",
        f"sequence={cmd['sequence']}",
        f"issued_at_unix_ms={cmd['issued_at_unix_ms']}",
        f"action={cmd['action']}",
        f"safety_class={cmd['safety_class']}",
        f"parameters={p}",
    ]
    return "\n".join(parts).encode("utf-8")

def verify_cmd(cmd, key_bytes):
    auth = cmd.get("auth") or {}
    if auth.get("alg") != "HMAC-SHA256":
        return False
    expected = hmac.new(key_bytes, signing_string(cmd), hashlib.sha256).hexdigest()
    return hmac.compare_digest(expected, auth.get("mac_hex", ""))
```

