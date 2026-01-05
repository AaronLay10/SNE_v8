// Music Puzzle Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string). (Buttons are input-only.)
//
// Devices (room-unique v8 device_ids):
// - music_button_1 .. music_button_6

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
static const char *HMAC_BUTTON_1 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BUTTON_2 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BUTTON_3 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BUTTON_4 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BUTTON_5 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BUTTON_6 = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (INPUT_PULLUP, active LOW)
static const int PIN_POWER_LED = 13;
static const int BUTTON_PINS[6] = {0, 1, 2, 3, 4, 5};

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

static sentient_v8::Client sne_button_1(make_cfg("music_button_1", HMAC_BUTTON_1));
static sentient_v8::Client sne_button_2(make_cfg("music_button_2", HMAC_BUTTON_2));
static sentient_v8::Client sne_button_3(make_cfg("music_button_3", HMAC_BUTTON_3));
static sentient_v8::Client sne_button_4(make_cfg("music_button_4", HMAC_BUTTON_4));
static sentient_v8::Client sne_button_5(make_cfg("music_button_5", HMAC_BUTTON_5));
static sentient_v8::Client sne_button_6(make_cfg("music_button_6", HMAC_BUTTON_6));

static sentient_v8::Client *clients[] = {&sne_button_1, &sne_button_2, &sne_button_3, &sne_button_4, &sne_button_5, &sne_button_6};

struct DeviceCtx {
  int idx;
};
static DeviceCtx ctx[6] = {{0}, {1}, {2}, {3}, {4}, {5}};

static bool button_state[6] = {false, false, false, false, false, false};
static bool last_reading[6] = {false, false, false, false, false, false};
static unsigned long last_debounce[6] = {0, 0, 0, 0, 0, 0};
static bool last_published[6] = {false, false, false, false, false, false};

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

static void publish_button_state(int idx, const char *reason) {
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
    publish_button_state(idx, "request_status");
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

  for (int i = 0; i < 6; i++) {
    bool reading = !digitalRead(BUTTON_PINS[i]); // active LOW
    if (reading != last_reading[i]) {
      last_debounce[i] = now;
      last_reading[i] = reading;
    }
    if ((now - last_debounce[i]) > DEBOUNCE_DELAY_MS) {
      if (reading != button_state[i]) {
        button_state[i] = reading;
      }
    }
  }
}

static void maybe_publish_buttons(bool force = false) {
  static bool initialized = false;
  static unsigned long last_publish = 0;
  const unsigned long now = millis();

  bool time_due = (now - last_publish) >= SENSOR_REFRESH_MS;
  if (!initialized) force = true;
  if (!force && !time_due) {
    bool any_changed = false;
    for (int i = 0; i < 6; i++) {
      if (button_state[i] != last_published[i]) {
        any_changed = true;
        break;
      }
    }
    if (!any_changed) return;
  }

  last_publish = now;
  initialized = true;

  for (int i = 0; i < 6; i++) {
    if (force || time_due || button_state[i] != last_published[i]) {
      publish_button_state(i, (force ? "boot" : (time_due ? "periodic" : "change")));
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  for (int i = 0; i < 6; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  }

  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) {
    if (!clients[i]->begin()) {
      while (true) delay(1000);
    }
  }

  sne_button_1.setCommandHandler(handleCommand, &ctx[0]);
  sne_button_2.setCommandHandler(handleCommand, &ctx[1]);
  sne_button_3.setCommandHandler(handleCommand, &ctx[2]);
  sne_button_4.setCommandHandler(handleCommand, &ctx[3]);
  sne_button_5.setCommandHandler(handleCommand, &ctx[4]);
  sne_button_6.setCommandHandler(handleCommand, &ctx[5]);

  read_buttons();
  maybe_publish_buttons(true);
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) {
    clients[i]->loop();
  }

  read_buttons();
  maybe_publish_buttons(false);
}
