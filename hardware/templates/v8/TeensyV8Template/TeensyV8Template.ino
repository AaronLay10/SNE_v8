// Sentient v8 — Teensy Template
// Demonstrates: presence/LWT, heartbeat, signed command verify, ack publish, retained state.

#include <Arduino.h>
#include <ArduinoJson.h>

#include <SentientV8.h>

// --- Configure these per device ---
#define ROOM_ID "room1"
#define DEVICE_ID "teensy_template_1"

// Per-room broker hostname: mqtt.<room>.sentientengine.ai
#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC key, hex encoded (64 chars).
static const char *DEVICE_HMAC_KEY_HEX = "0000000000000000000000000000000000000000000000000000000000000000";

static sentient_v8::Config cfg = []() {
  sentient_v8::Config c;
  c.brokerIp = IPAddress(0, 0, 0, 0);
  c.brokerHost = MQTT_BROKER_HOST;
  c.brokerPort = MQTT_PORT;
  c.username = MQTT_USERNAME;
  c.password = MQTT_PASSWORD;
  c.roomId = ROOM_ID;
  c.deviceId = DEVICE_ID;
  c.deviceHmacKeyHex = DEVICE_HMAC_KEY_HEX;
  c.keepAliveSeconds = 10;
  c.reconnectDelayMs = 1000;
  c.heartbeatIntervalMs = 1000;
  c.rxJsonCapacity = 2048;
  c.txJsonCapacity = 2048;
  return c;
}();

sentient_v8::Client sne(cfg);

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

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void *ctx) {
  (void)ctx;

  const char *action = cmd["action"] | "";
  if (String(action) != "SET") {
    rejectedAckReason["reason_code"] = "UNSUPPORTED_ACTION";
    return false;
  }

  JsonVariantConst params = cmd["parameters"];
  const char *op = params["op"] | "";
  if (!op || !op[0]) {
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (String(op) != "set_pin") {
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  int pin = params["pin"] | -1;
  bool value = params["value"] | false;
  if (pin < 0) {
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  pinMode(pin, OUTPUT);
  digitalWrite(pin, value ? HIGH : LOW);

  StaticJsonDocument<256> state;
  state["last_op"] = op;
  state["last_set_pin"] = pin;
  state["last_set_value"] = value;
  sne.publishState(state);

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  if (!sne.begin()) {
    while (true) {
      delay(1000);
    }
  }
  sne.setCommandHandler(handleCommand, nullptr);
}

void loop() {
  sne.loop();
}
