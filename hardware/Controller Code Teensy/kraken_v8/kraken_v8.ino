// Kraken Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 identity: one v8 device_id for this logical device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Device (room-unique v8 device_id):
// - kraken_controller

#include <Arduino.h>
#include <ArduinoJson.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC key, hex encoded (64 chars).
static const char *HMAC_CONTROLLER = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (from v2)
static const int PIN_POWER_LED = 13;

// Throttle handle (INPUT_PULLUP, active LOW)
static const int PIN_FWD1 = 6;
static const int PIN_FWD2 = 7;
static const int PIN_FWD3 = 5;
static const int PIN_NEUTRAL = 2;
static const int PIN_REV1 = 3;
static const int PIN_REV2 = 4;
static const int PIN_REV3 = 1;

// Captain wheel encoder
static const int PIN_WHEELA = 8; // white
static const int PIN_WHEELB = 9; // green

static volatile long counterA = 0;
static volatile long counterB = 0;
static long last_counterA = 0;
static int last_throttle = 0;

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

static sentient_v8::Client sne_controller(make_cfg("kraken_controller", HMAC_CONTROLLER));

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

static int read_throttle() {
  if (digitalRead(PIN_NEUTRAL) == LOW) return 0;
  if (digitalRead(PIN_FWD1) == LOW) return 1;
  if (digitalRead(PIN_FWD2) == LOW) return 2;
  if (digitalRead(PIN_FWD3) == LOW) return 3;
  if (digitalRead(PIN_REV1) == LOW) return -1;
  if (digitalRead(PIN_REV2) == LOW) return -2;
  if (digitalRead(PIN_REV3) == LOW) return -3;
  return 0;
}

static void publish_state(const char *reason) {
  noInterrupts();
  long a = counterA;
  long b = counterB;
  interrupts();

  StaticJsonDocument<256> st;
  st["wheel_count"] = a;
  st["wheel_count_b"] = b;
  st["throttle"] = read_throttle();
  st["reason"] = reason ? reason : "";
  sne_controller.publishState(st);
}

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void * /*vctx*/) {
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
    publish_state("request_status");
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
  if (strcmp(op, "reset_counter") == 0) {
    noInterrupts();
    counterA = 0;
    counterB = 0;
    interrupts();
    last_counterA = 0;
    publish_state("reset_counter");
    return true;
  }
  if (strcmp(op, "noop") == 0) return true;

  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

static void CounterA_ISR() {
  if (digitalRead(PIN_WHEELA) == LOW) {
    counterA++;
  } else {
    counterA--;
  }
}

static void CounterB_ISR() {
  if (digitalRead(PIN_WHEELB) == LOW) {
    counterA--;
  } else {
    counterA++;
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  pinMode(PIN_WHEELA, INPUT_PULLUP);
  pinMode(PIN_WHEELB, INPUT_PULLUP);
  pinMode(PIN_FWD1, INPUT_PULLUP);
  pinMode(PIN_FWD2, INPUT_PULLUP);
  pinMode(PIN_FWD3, INPUT_PULLUP);
  pinMode(PIN_NEUTRAL, INPUT_PULLUP);
  pinMode(PIN_REV1, INPUT_PULLUP);
  pinMode(PIN_REV2, INPUT_PULLUP);
  pinMode(PIN_REV3, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_WHEELB), CounterA_ISR, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_WHEELA), CounterB_ISR, RISING);

  if (!sne_controller.begin()) while (true) delay(1000);
  sne_controller.setCommandHandler(handleCommand, nullptr);

  publish_state("boot");
}

void loop() {
  static unsigned long last_publish = 0;
  sne_controller.loop();

  noInterrupts();
  long a = counterA;
  interrupts();
  int throttle = read_throttle();

  const unsigned long now = millis();
  bool changed = (a != last_counterA) || (throttle != last_throttle);

  if (changed || (now - last_publish) > 1000UL) {
    last_publish = now;
    last_counterA = a;
    last_throttle = throttle;
    publish_state(changed ? "change" : "periodic");
  }
}
