// Vern Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - vern_output_one .. vern_output_eight
// - vern_power_switch

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
static const char *HMAC_OUTPUT_ONE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_OUTPUT_TWO = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_OUTPUT_THREE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_OUTPUT_FOUR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_OUTPUT_FIVE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_OUTPUT_SIX = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_OUTPUT_SEVEN = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_OUTPUT_EIGHT = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_POWER_SWITCH = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins
static const int PIN_POWER_LED = 13;
static const int PIN_OUTPUT_ONE = 34;
static const int PIN_OUTPUT_TWO = 35;
static const int PIN_OUTPUT_THREE = 36;
static const int PIN_OUTPUT_FOUR = 37;
static const int PIN_OUTPUT_FIVE = 38;
static const int PIN_OUTPUT_SIX = 39;
static const int PIN_OUTPUT_SEVEN = 40;
static const int PIN_OUTPUT_EIGHT = 41;
static const int PIN_POWER_SWITCH = 24;

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

static sentient_v8::Client sne_output_one(make_cfg("vern_output_one", HMAC_OUTPUT_ONE));
static sentient_v8::Client sne_output_two(make_cfg("vern_output_two", HMAC_OUTPUT_TWO));
static sentient_v8::Client sne_output_three(make_cfg("vern_output_three", HMAC_OUTPUT_THREE));
static sentient_v8::Client sne_output_four(make_cfg("vern_output_four", HMAC_OUTPUT_FOUR));
static sentient_v8::Client sne_output_five(make_cfg("vern_output_five", HMAC_OUTPUT_FIVE));
static sentient_v8::Client sne_output_six(make_cfg("vern_output_six", HMAC_OUTPUT_SIX));
static sentient_v8::Client sne_output_seven(make_cfg("vern_output_seven", HMAC_OUTPUT_SEVEN));
static sentient_v8::Client sne_output_eight(make_cfg("vern_output_eight", HMAC_OUTPUT_EIGHT));
static sentient_v8::Client sne_power_switch(make_cfg("vern_power_switch", HMAC_POWER_SWITCH));

static sentient_v8::Client *clients[] = {&sne_output_one, &sne_output_two, &sne_output_three, &sne_output_four,
                                         &sne_output_five, &sne_output_six, &sne_output_seven, &sne_output_eight,
                                         &sne_power_switch};

enum class OutputKind : uint8_t { One, Two, Three, Four, Five, Six, Seven, Eight, PowerSwitch };
struct DeviceCtx {
  OutputKind kind;
};
static DeviceCtx ctx_one = {OutputKind::One};
static DeviceCtx ctx_two = {OutputKind::Two};
static DeviceCtx ctx_three = {OutputKind::Three};
static DeviceCtx ctx_four = {OutputKind::Four};
static DeviceCtx ctx_five = {OutputKind::Five};
static DeviceCtx ctx_six = {OutputKind::Six};
static DeviceCtx ctx_seven = {OutputKind::Seven};
static DeviceCtx ctx_eight = {OutputKind::Eight};
static DeviceCtx ctx_power = {OutputKind::PowerSwitch};

static bool output_on[9] = {false, false, false, false, false, false, false, false, false};

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

static int kind_index(OutputKind k) {
  switch (k) {
    case OutputKind::One: return 0;
    case OutputKind::Two: return 1;
    case OutputKind::Three: return 2;
    case OutputKind::Four: return 3;
    case OutputKind::Five: return 4;
    case OutputKind::Six: return 5;
    case OutputKind::Seven: return 6;
    case OutputKind::Eight: return 7;
    case OutputKind::PowerSwitch: return 8;
  }
  return -1;
}

static int pin_for_kind(OutputKind k) {
  switch (k) {
    case OutputKind::One: return PIN_OUTPUT_ONE;
    case OutputKind::Two: return PIN_OUTPUT_TWO;
    case OutputKind::Three: return PIN_OUTPUT_THREE;
    case OutputKind::Four: return PIN_OUTPUT_FOUR;
    case OutputKind::Five: return PIN_OUTPUT_FIVE;
    case OutputKind::Six: return PIN_OUTPUT_SIX;
    case OutputKind::Seven: return PIN_OUTPUT_SEVEN;
    case OutputKind::Eight: return PIN_OUTPUT_EIGHT;
    case OutputKind::PowerSwitch: return PIN_POWER_SWITCH;
  }
  return -1;
}

static void publish_state(OutputKind k, const char *reason) {
  int idx = kind_index(k);
  if (idx < 0) return;
  sentient_v8::Client *c = clients[idx];
  if (!c) return;

  StaticJsonDocument<160> st;
  st["on"] = output_on[idx];
  st["reason"] = reason ? reason : "";
  c->publishState(st);
}

static void publish_all(const char *reason) {
  publish_state(OutputKind::One, reason);
  publish_state(OutputKind::Two, reason);
  publish_state(OutputKind::Three, reason);
  publish_state(OutputKind::Four, reason);
  publish_state(OutputKind::Five, reason);
  publish_state(OutputKind::Six, reason);
  publish_state(OutputKind::Seven, reason);
  publish_state(OutputKind::Eight, reason);
  publish_state(OutputKind::PowerSwitch, reason);
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

  if (strcmp(op, "set_power_led") == 0) {
    if (!p.containsKey("on")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    bool on = p["on"] | false;
    digitalWrite(PIN_POWER_LED, on ? HIGH : LOW);
    return true;
  }

  if (strcmp(op, "request_status") == 0) {
    publish_state(ctx->kind, "request_status");
    return true;
  }

  int idx = kind_index(ctx->kind);
  int pin = pin_for_kind(ctx->kind);
  if (idx < 0 || pin < 0) {
    rejectedAckReason["reason_code"] = "INTERNAL_ERROR";
    return false;
  }

  if (strcmp(op, "set") == 0) {
    if (!p.containsKey("on")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    bool on = p["on"] | false;
    digitalWrite(pin, on ? HIGH : LOW);
    output_on[idx] = on;
    publish_state(ctx->kind, "set");
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

  pinMode(PIN_OUTPUT_ONE, OUTPUT);
  pinMode(PIN_OUTPUT_TWO, OUTPUT);
  pinMode(PIN_OUTPUT_THREE, OUTPUT);
  pinMode(PIN_OUTPUT_FOUR, OUTPUT);
  pinMode(PIN_OUTPUT_FIVE, OUTPUT);
  pinMode(PIN_OUTPUT_SIX, OUTPUT);
  pinMode(PIN_OUTPUT_SEVEN, OUTPUT);
  pinMode(PIN_OUTPUT_EIGHT, OUTPUT);
  pinMode(PIN_POWER_SWITCH, OUTPUT);

  // Default OFF.
  digitalWrite(PIN_OUTPUT_ONE, LOW);
  digitalWrite(PIN_OUTPUT_TWO, LOW);
  digitalWrite(PIN_OUTPUT_THREE, LOW);
  digitalWrite(PIN_OUTPUT_FOUR, LOW);
  digitalWrite(PIN_OUTPUT_FIVE, LOW);
  digitalWrite(PIN_OUTPUT_SIX, LOW);
  digitalWrite(PIN_OUTPUT_SEVEN, LOW);
  digitalWrite(PIN_OUTPUT_EIGHT, LOW);
  digitalWrite(PIN_POWER_SWITCH, LOW);

  for (int i = 0; i < 9; i++) output_on[i] = false;

  if (!sne_output_one.begin()) while (true) delay(1000);
  if (!sne_output_two.begin()) while (true) delay(1000);
  if (!sne_output_three.begin()) while (true) delay(1000);
  if (!sne_output_four.begin()) while (true) delay(1000);
  if (!sne_output_five.begin()) while (true) delay(1000);
  if (!sne_output_six.begin()) while (true) delay(1000);
  if (!sne_output_seven.begin()) while (true) delay(1000);
  if (!sne_output_eight.begin()) while (true) delay(1000);
  if (!sne_power_switch.begin()) while (true) delay(1000);

  sne_output_one.setCommandHandler(handleCommand, &ctx_one);
  sne_output_two.setCommandHandler(handleCommand, &ctx_two);
  sne_output_three.setCommandHandler(handleCommand, &ctx_three);
  sne_output_four.setCommandHandler(handleCommand, &ctx_four);
  sne_output_five.setCommandHandler(handleCommand, &ctx_five);
  sne_output_six.setCommandHandler(handleCommand, &ctx_six);
  sne_output_seven.setCommandHandler(handleCommand, &ctx_seven);
  sne_output_eight.setCommandHandler(handleCommand, &ctx_eight);
  sne_power_switch.setCommandHandler(handleCommand, &ctx_power);

  publish_all("boot");
}

void loop() {
  static unsigned long last_publish = 0;
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  const unsigned long now = millis();
  if (now - last_publish > 60UL * 1000UL) {
    publish_all("periodic");
    last_publish = now;
  }
}
