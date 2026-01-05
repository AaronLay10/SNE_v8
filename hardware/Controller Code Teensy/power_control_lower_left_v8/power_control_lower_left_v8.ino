// Power Control (Lower Left) — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per relay rail (+ controller device).
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - power_control_lower_left_lever_riddle_cube_24v
// - power_control_lower_left_lever_riddle_cube_12v
// - power_control_lower_left_lever_riddle_cube_5v
// - power_control_lower_left_clock_24v
// - power_control_lower_left_clock_12v
// - power_control_lower_left_clock_5v
// - power_control_lower_left_controller

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
static const char *HMAC_LEVER_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CLOCK_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CLOCK_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CLOCK_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CONTROLLER = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (active HIGH relays)
static const int power_led_pin = 13;
static const int lever_riddle_cube_24v_pin = 33;
static const int lever_riddle_cube_12v_pin = 34;
static const int lever_riddle_cube_5v_pin = 35;
static const int clock_24v_pin = 36;
static const int clock_12v_pin = 37;
static const int clock_5v_pin = 38;

// Current relay states
static bool s_lever_24v = false;
static bool s_lever_12v = false;
static bool s_lever_5v = false;
static bool s_clock_24v = false;
static bool s_clock_12v = false;
static bool s_clock_5v = false;

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
  c.heartbeatIntervalMs = 1000;
  c.rxJsonCapacity = 2048;
  c.txJsonCapacity = 2048;
  return c;
}

static sentient_v8::Client sne_lever_24v(make_cfg("power_control_lower_left_lever_riddle_cube_24v", HMAC_LEVER_24V));
static sentient_v8::Client sne_lever_12v(make_cfg("power_control_lower_left_lever_riddle_cube_12v", HMAC_LEVER_12V));
static sentient_v8::Client sne_lever_5v(make_cfg("power_control_lower_left_lever_riddle_cube_5v", HMAC_LEVER_5V));
static sentient_v8::Client sne_clock_24v(make_cfg("power_control_lower_left_clock_24v", HMAC_CLOCK_24V));
static sentient_v8::Client sne_clock_12v(make_cfg("power_control_lower_left_clock_12v", HMAC_CLOCK_12V));
static sentient_v8::Client sne_clock_5v(make_cfg("power_control_lower_left_clock_5v", HMAC_CLOCK_5V));
static sentient_v8::Client sne_controller(make_cfg("power_control_lower_left_controller", HMAC_CONTROLLER));

enum class DeviceKind : uint8_t { Lever24V, Lever12V, Lever5V, Clock24V, Clock12V, Clock5V, Controller };

struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_lever_24v = {DeviceKind::Lever24V};
static DeviceCtx ctx_lever_12v = {DeviceKind::Lever12V};
static DeviceCtx ctx_lever_5v = {DeviceKind::Lever5V};
static DeviceCtx ctx_clock_24v = {DeviceKind::Clock24V};
static DeviceCtx ctx_clock_12v = {DeviceKind::Clock12V};
static DeviceCtx ctx_clock_5v = {DeviceKind::Clock5V};
static DeviceCtx ctx_controller = {DeviceKind::Controller};

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

static void apply_outputs() {
  digitalWrite(power_led_pin, HIGH);
  digitalWrite(lever_riddle_cube_24v_pin, s_lever_24v ? HIGH : LOW);
  digitalWrite(lever_riddle_cube_12v_pin, s_lever_12v ? HIGH : LOW);
  digitalWrite(lever_riddle_cube_5v_pin, s_lever_5v ? HIGH : LOW);
  digitalWrite(clock_24v_pin, s_clock_24v ? HIGH : LOW);
  digitalWrite(clock_12v_pin, s_clock_12v ? HIGH : LOW);
  digitalWrite(clock_5v_pin, s_clock_5v ? HIGH : LOW);
}

static void publish_state_relay(sentient_v8::Client &c, bool on) {
  StaticJsonDocument<128> st;
  st["on"] = on;
  c.publishState(st);
}

static void publish_state_controller() {
  StaticJsonDocument<256> st;
  st["lever_riddle_cube_24v_on"] = s_lever_24v;
  st["lever_riddle_cube_12v_on"] = s_lever_12v;
  st["lever_riddle_cube_5v_on"] = s_lever_5v;
  st["clock_24v_on"] = s_clock_24v;
  st["clock_12v_on"] = s_clock_12v;
  st["clock_5v_on"] = s_clock_5v;
  sne_controller.publishState(st);
}

static void publish_all_states() {
  publish_state_relay(sne_lever_24v, s_lever_24v);
  publish_state_relay(sne_lever_12v, s_lever_12v);
  publish_state_relay(sne_lever_5v, s_lever_5v);
  publish_state_relay(sne_clock_24v, s_clock_24v);
  publish_state_relay(sne_clock_12v, s_clock_12v);
  publish_state_relay(sne_clock_5v, s_clock_5v);
  publish_state_controller();
}

static void set_all(bool on) {
  s_lever_24v = on;
  s_lever_12v = on;
  s_lever_5v = on;
  s_clock_24v = on;
  s_clock_12v = on;
  s_clock_5v = on;
  apply_outputs();
  publish_all_states();
}

static void emergency_off() {
  set_all(false);
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

  auto handle_relay_set = [&](bool &state_ref, sentient_v8::Client &client) -> bool {
    if (strcmp(op, "set") != 0) return false;
    if (!p.containsKey("on")) return false;
    state_ref = p["on"] | false;
    apply_outputs();
    publish_state_relay(client, state_ref);
    publish_state_controller();
    return true;
  };

  switch (ctx->kind) {
  case DeviceKind::Lever24V:
    if (!handle_relay_set(s_lever_24v, sne_lever_24v)) goto bad;
    return true;
  case DeviceKind::Lever12V:
    if (!handle_relay_set(s_lever_12v, sne_lever_12v)) goto bad;
    return true;
  case DeviceKind::Lever5V:
    if (!handle_relay_set(s_lever_5v, sne_lever_5v)) goto bad;
    return true;
  case DeviceKind::Clock24V:
    if (!handle_relay_set(s_clock_24v, sne_clock_24v)) goto bad;
    return true;
  case DeviceKind::Clock12V:
    if (!handle_relay_set(s_clock_12v, sne_clock_12v)) goto bad;
    return true;
  case DeviceKind::Clock5V:
    if (!handle_relay_set(s_clock_5v, sne_clock_5v)) goto bad;
    return true;
  case DeviceKind::Controller:
    if (strcmp(op, "all_on") == 0) {
      set_all(true);
      return true;
    }
    if (strcmp(op, "all_off") == 0) {
      set_all(false);
      return true;
    }
    if (strcmp(op, "emergency_off") == 0) {
      emergency_off();
      return true;
    }
    if (strcmp(op, "reset") == 0) {
      set_all(false);
      return true;
    }
    if (strcmp(op, "request_status") == 0) {
      publish_all_states();
      return true;
    }
    if (strcmp(op, "noop") == 0) {
      return true;
    }
    goto bad;
  }

bad:
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(power_led_pin, OUTPUT);
  pinMode(lever_riddle_cube_24v_pin, OUTPUT);
  pinMode(lever_riddle_cube_12v_pin, OUTPUT);
  pinMode(lever_riddle_cube_5v_pin, OUTPUT);
  pinMode(clock_24v_pin, OUTPUT);
  pinMode(clock_12v_pin, OUTPUT);
  pinMode(clock_5v_pin, OUTPUT);

  // Safe defaults: power rails OFF
  s_lever_24v = false;
  s_lever_12v = false;
  s_lever_5v = false;
  s_clock_24v = false;
  s_clock_12v = false;
  s_clock_5v = false;
  apply_outputs();

  if (!sne_lever_24v.begin()) while (true) delay(1000);
  if (!sne_lever_12v.begin()) while (true) delay(1000);
  if (!sne_lever_5v.begin()) while (true) delay(1000);
  if (!sne_clock_24v.begin()) while (true) delay(1000);
  if (!sne_clock_12v.begin()) while (true) delay(1000);
  if (!sne_clock_5v.begin()) while (true) delay(1000);
  if (!sne_controller.begin()) while (true) delay(1000);

  sne_lever_24v.setCommandHandler(handleCommand, &ctx_lever_24v);
  sne_lever_12v.setCommandHandler(handleCommand, &ctx_lever_12v);
  sne_lever_5v.setCommandHandler(handleCommand, &ctx_lever_5v);
  sne_clock_24v.setCommandHandler(handleCommand, &ctx_clock_24v);
  sne_clock_12v.setCommandHandler(handleCommand, &ctx_clock_12v);
  sne_clock_5v.setCommandHandler(handleCommand, &ctx_clock_5v);
  sne_controller.setCommandHandler(handleCommand, &ctx_controller);

  publish_all_states();
}

void loop() {
  sne_lever_24v.loop();
  sne_lever_12v.loop();
  sne_lever_5v.loop();
  sne_clock_24v.loop();
  sne_clock_12v.loop();
  sne_clock_5v.loop();
  sne_controller.loop();
}
