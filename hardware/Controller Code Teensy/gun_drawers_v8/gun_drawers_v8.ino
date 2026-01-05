// Gun Drawers Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - gun_drawers_drawer_elegant
// - gun_drawers_drawer_alchemist
// - gun_drawers_drawer_bounty
// - gun_drawers_drawer_mechanic

#include <Arduino.h>
#include <ArduinoJson.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_DRAWER_ELEGANT = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_DRAWER_ALCHEMIST = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_DRAWER_BOUNTY = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_DRAWER_MECHANIC = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (HIGH=locked, LOW=unlocked)
static const int PIN_POWER_LED = 13;
static const int PIN_DRAWER_ELEGANT = 2;
static const int PIN_DRAWER_ALCHEMIST = 3;
static const int PIN_DRAWER_BOUNTY = 4;
static const int PIN_DRAWER_MECHANIC = 5;

static sentient_v8::Config make_cfg(const char *device_id, const char *hmac_key_hex) {
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
}

static sentient_v8::Client sne_drawer_elegant(make_cfg("gun_drawers_drawer_elegant", HMAC_DRAWER_ELEGANT));
static sentient_v8::Client sne_drawer_alchemist(make_cfg("gun_drawers_drawer_alchemist", HMAC_DRAWER_ALCHEMIST));
static sentient_v8::Client sne_drawer_bounty(make_cfg("gun_drawers_drawer_bounty", HMAC_DRAWER_BOUNTY));
static sentient_v8::Client sne_drawer_mechanic(make_cfg("gun_drawers_drawer_mechanic", HMAC_DRAWER_MECHANIC));

static sentient_v8::Client *clients[] = {&sne_drawer_elegant, &sne_drawer_alchemist, &sne_drawer_bounty, &sne_drawer_mechanic};

enum class DrawerKind : uint8_t { Elegant, Alchemist, Bounty, Mechanic };
struct DeviceCtx {
  DrawerKind kind;
};
static DeviceCtx ctx_elegant = {DrawerKind::Elegant};
static DeviceCtx ctx_alchemist = {DrawerKind::Alchemist};
static DeviceCtx ctx_bounty = {DrawerKind::Bounty};
static DeviceCtx ctx_mechanic = {DrawerKind::Mechanic};

static bool drawer_locked[4] = {true, true, true, true};

static void ensure_ethernet_dhcp() {
#if !defined(ESP32)
  static bool started = false;
  if (started) return;
  byte mac[6];
  teensyMAC(mac);
  Ethernet.begin(mac);
  delay(250);
  started = true;
#endif
}

static int kind_index(DrawerKind k) {
  switch (k) {
    case DrawerKind::Elegant: return 0;
    case DrawerKind::Alchemist: return 1;
    case DrawerKind::Bounty: return 2;
    case DrawerKind::Mechanic: return 3;
  }
  return -1;
}

static int pin_for_kind(DrawerKind k) {
  switch (k) {
    case DrawerKind::Elegant: return PIN_DRAWER_ELEGANT;
    case DrawerKind::Alchemist: return PIN_DRAWER_ALCHEMIST;
    case DrawerKind::Bounty: return PIN_DRAWER_BOUNTY;
    case DrawerKind::Mechanic: return PIN_DRAWER_MECHANIC;
  }
  return -1;
}

static void publish_state(DrawerKind k, const char *reason) {
  int idx = kind_index(k);
  if (idx < 0) return;
  sentient_v8::Client *c = clients[idx];
  if (!c) return;

  StaticJsonDocument<192> st;
  st["locked"] = drawer_locked[idx];
  st["reason"] = reason ? reason : "";
  c->publishState(st);
}

static void publish_all(const char *reason) {
  publish_state(DrawerKind::Elegant, reason);
  publish_state(DrawerKind::Alchemist, reason);
  publish_state(DrawerKind::Bounty, reason);
  publish_state(DrawerKind::Mechanic, reason);
}

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void *vctx) {
  DeviceCtx *ctx = (DeviceCtx *)vctx;
  if (!ctx) {
    rejectedAckReason["reason_code"] = "INTERNAL_ERROR";
    return false;
  }

  const char *action = cmd["action"] | "";
  if (strcmp(action, "SET") != 0) {
    rejectedAckReason["reason_code"] = "UNSUPPORTED_ACTION";
    return false;
  }

  JsonVariantConst p = cmd["parameters"];
  const char *op = p["op"] | "";
  if (!op || !op[0]) {
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (strcmp(op, "request_status") == 0) {
    publish_state(ctx->kind, "request_status");
    return true;
  }
  if (strcmp(op, "set_power_led") == 0) {
    if (!p.containsKey("on")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    bool on = p["on"] | false;
    digitalWrite(PIN_POWER_LED, on ? HIGH : LOW);
    return true;
  }

  int idx = kind_index(ctx->kind);
  int pin = pin_for_kind(ctx->kind);
  if (idx < 0 || pin < 0) {
    rejectedAckReason["reason_code"] = "INTERNAL_ERROR";
    return false;
  }

  if (strcmp(op, "unlock") == 0) {
    digitalWrite(pin, LOW);
    drawer_locked[idx] = false;
    publish_state(ctx->kind, "unlock");
    return true;
  }
  if (strcmp(op, "lock") == 0) {
    digitalWrite(pin, HIGH);
    drawer_locked[idx] = true;
    publish_state(ctx->kind, "lock");
    return true;
  }
  if (strcmp(op, "noop") == 0) return true;

  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  pinMode(PIN_DRAWER_ELEGANT, OUTPUT);
  pinMode(PIN_DRAWER_ALCHEMIST, OUTPUT);
  pinMode(PIN_DRAWER_BOUNTY, OUTPUT);
  pinMode(PIN_DRAWER_MECHANIC, OUTPUT);

  // Default locked (HIGH).
  digitalWrite(PIN_DRAWER_ELEGANT, HIGH);
  digitalWrite(PIN_DRAWER_ALCHEMIST, HIGH);
  digitalWrite(PIN_DRAWER_BOUNTY, HIGH);
  digitalWrite(PIN_DRAWER_MECHANIC, HIGH);
  drawer_locked[0] = true;
  drawer_locked[1] = true;
  drawer_locked[2] = true;
  drawer_locked[3] = true;

  if (!sne_drawer_elegant.begin()) while (true) delay(1000);
  if (!sne_drawer_alchemist.begin()) while (true) delay(1000);
  if (!sne_drawer_bounty.begin()) while (true) delay(1000);
  if (!sne_drawer_mechanic.begin()) while (true) delay(1000);

  sne_drawer_elegant.setCommandHandler(handleCommand, &ctx_elegant);
  sne_drawer_alchemist.setCommandHandler(handleCommand, &ctx_alchemist);
  sne_drawer_bounty.setCommandHandler(handleCommand, &ctx_bounty);
  sne_drawer_mechanic.setCommandHandler(handleCommand, &ctx_mechanic);

  publish_all("boot");
}

void loop() {
  static unsigned long last_publish = 0;
  sne_drawer_elegant.loop();
  sne_drawer_alchemist.loop();
  sne_drawer_bounty.loop();
  sne_drawer_mechanic.loop();

  const unsigned long now = millis();
  if (now - last_publish > 60UL * 1000UL) {
    publish_all("periodic");
    last_publish = now;
  }
}
