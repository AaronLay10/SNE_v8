// Pilaster Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per button.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string). (Buttons are input-only.)
//
// Devices (room-unique v8 device_ids):
// - pilaster_green_button
// - pilaster_silver_button
// - pilaster_red_button
// - pilaster_black_button

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
static const char *HMAC_GREEN = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_SILVER = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RED = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BLACK = "0000000000000000000000000000000000000000000000000000000000000000";

static const int PIN_POWER_LED = 13;

// Buttons (from v2) - active HIGH, INPUT_PULLDOWN
static const int PIN_GREEN = 12;
static const int PIN_SILVER = 10;
static const int PIN_RED = 9;
static const int PIN_BLACK = 11;

static const unsigned long DEBOUNCE_DELAY_MS = 50;
static const unsigned long SENSOR_REFRESH_MS = 60UL * 1000UL;

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

static sentient_v8::Client sne_green(make_cfg("pilaster_green_button", HMAC_GREEN));
static sentient_v8::Client sne_silver(make_cfg("pilaster_silver_button", HMAC_SILVER));
static sentient_v8::Client sne_red(make_cfg("pilaster_red_button", HMAC_RED));
static sentient_v8::Client sne_black(make_cfg("pilaster_black_button", HMAC_BLACK));

static sentient_v8::Client *clients[] = {&sne_green, &sne_silver, &sne_red, &sne_black};

struct DeviceCtx {
  int idx;
};
static DeviceCtx ctx[4] = {{0}, {1}, {2}, {3}};

static bool button_state[4] = {false, false, false, false};
static bool last_reading[4] = {false, false, false, false};
static bool last_published[4] = {false, false, false, false};
static unsigned long last_debounce[4] = {0, 0, 0, 0};

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

static int pin_for_idx(int idx) {
  switch (idx) {
    case 0: return PIN_GREEN;
    case 1: return PIN_SILVER;
    case 2: return PIN_RED;
    case 3: return PIN_BLACK;
  }
  return -1;
}

static void publish_state(int idx, const char *reason) {
  if (idx < 0) return;
  if ((size_t)idx >= (sizeof(clients) / sizeof(clients[0]))) return;

  StaticJsonDocument<128> st;
  st["pressed"] = button_state[idx];
  st["reason"] = reason ? reason : "";
  clients[idx]->publishState(st);
  last_published[idx] = button_state[idx];
}

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void *vctx) {
  DeviceCtx *dctx = (DeviceCtx *)vctx;
  int idx = dctx ? dctx->idx : -1;
  if (idx < 0) {
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
    publish_state(idx, "request_status");
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
  if (strcmp(op, "noop") == 0) return true;

  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

static void read_buttons() {
  const unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    int pin = pin_for_idx(i);
    if (pin < 0) continue;

    bool reading = (digitalRead(pin) == HIGH);
    if (reading != last_reading[i]) {
      last_debounce[i] = now;
      last_reading[i] = reading;
    }
    if ((now - last_debounce[i]) > DEBOUNCE_DELAY_MS) {
      if (reading != button_state[i]) button_state[i] = reading;
    }
  }
}

static void maybe_publish(bool force = false) {
  static unsigned long last_publish = 0;
  const unsigned long now = millis();
  bool periodic_due = (now - last_publish) >= SENSOR_REFRESH_MS;

  if (!force && !periodic_due) {
    bool any_changed = false;
    for (int i = 0; i < 4; i++) {
      if (button_state[i] != last_published[i]) {
        any_changed = true;
        break;
      }
    }
    if (!any_changed) return;
  }

  last_publish = now;
  for (int i = 0; i < 4; i++) {
    if (force || periodic_due || button_state[i] != last_published[i]) {
      publish_state(i, force ? "boot" : (periodic_due ? "periodic" : "change"));
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  pinMode(PIN_GREEN, INPUT_PULLDOWN);
  pinMode(PIN_SILVER, INPUT_PULLDOWN);
  pinMode(PIN_RED, INPUT_PULLDOWN);
  pinMode(PIN_BLACK, INPUT_PULLDOWN);

  if (!sne_green.begin()) while (true) delay(1000);
  if (!sne_silver.begin()) while (true) delay(1000);
  if (!sne_red.begin()) while (true) delay(1000);
  if (!sne_black.begin()) while (true) delay(1000);

  sne_green.setCommandHandler(handleCommand, &ctx[0]);
  sne_silver.setCommandHandler(handleCommand, &ctx[1]);
  sne_red.setCommandHandler(handleCommand, &ctx[2]);
  sne_black.setCommandHandler(handleCommand, &ctx[3]);

  read_buttons();
  maybe_publish(true);
}

void loop() {
  sne_green.loop();
  sne_silver.loop();
  sne_red.loop();
  sne_black.loop();

  read_buttons();
  maybe_publish(false);
}
