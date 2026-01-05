// Gear Puzzle Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - gear_encoder_a
// - gear_encoder_b
// - gear_controller

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
static const char *HMAC_ENCODER_A = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_ENCODER_B = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CONTROLLER = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins
static const int PIN_POWER_LED = 13;
static const int PIN_ENCODER_A_WHITE = 33;
static const int PIN_ENCODER_A_GREEN = 34;
static const int PIN_ENCODER_B_WHITE = 35;
static const int PIN_ENCODER_B_GREEN = 36;

// Encoder counters (volatile for ISR)
static volatile long counter_a = 0;
static volatile long counter_b = 0;

static long last_counter_a = 0;
static long last_counter_b = 0;

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

static sentient_v8::Client sne_encoder_a(make_cfg("gear_encoder_a", HMAC_ENCODER_A));
static sentient_v8::Client sne_encoder_b(make_cfg("gear_encoder_b", HMAC_ENCODER_B));
static sentient_v8::Client sne_controller(make_cfg("gear_controller", HMAC_CONTROLLER));

enum class DeviceKind : uint8_t { EncoderA, EncoderB, Controller };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_encoder_a = {DeviceKind::EncoderA};
static DeviceCtx ctx_encoder_b = {DeviceKind::EncoderB};
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

static void publish_state_encoders(long a, long b) {
  {
    StaticJsonDocument<128> st;
    st["count"] = a;
    sne_encoder_a.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["count"] = b;
    sne_encoder_b.publishState(st);
  }
  {
    StaticJsonDocument<192> st;
    st["encoder_a"] = a;
    st["encoder_b"] = b;
    sne_controller.publishState(st);
  }
}

static void read_and_publish(bool force = false) {
  static unsigned long last_publish = 0;
  unsigned long now = millis();

  noInterrupts();
  long a = counter_a;
  long b = counter_b;
  interrupts();

  bool changed = (a != last_counter_a || b != last_counter_b);
  if (changed) {
    last_counter_a = a;
    last_counter_b = b;
  }

  if (!force && !changed && (now - last_publish) < 5000) return;
  last_publish = now;

  publish_state_encoders(a, b);
}

// Encoder A - White wire interrupt
static void counter_a_white_isr() {
  if (digitalRead(PIN_ENCODER_A_GREEN) == LOW) {
    counter_a++;
  } else {
    counter_a--;
  }
}

// Encoder A - Green wire interrupt
static void counter_a_green_isr() {
  if (digitalRead(PIN_ENCODER_A_WHITE) == LOW) {
    counter_a--;
  } else {
    counter_a++;
  }
}

// Encoder B - White wire interrupt
static void counter_b_white_isr() {
  if (digitalRead(PIN_ENCODER_B_GREEN) == LOW) {
    counter_b++;
  } else {
    counter_b--;
  }
}

// Encoder B - Green wire interrupt
static void counter_b_green_isr() {
  if (digitalRead(PIN_ENCODER_B_WHITE) == LOW) {
    counter_b--;
  } else {
    counter_b++;
  }
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

  if (ctx->kind == DeviceKind::Controller) {
    if (strcmp(op, "reset") == 0) {
      noInterrupts();
      counter_a = 0;
      counter_b = 0;
      interrupts();
      last_counter_a = 0;
      last_counter_b = 0;
      read_and_publish(true);
      return true;
    }
    if (strcmp(op, "request_status") == 0) {
      read_and_publish(true);
      return true;
    }
    if (strcmp(op, "noop") == 0) return true;
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Encoders are input-only.
  if (strcmp(op, "request_status") == 0) {
    read_and_publish(true);
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

  pinMode(PIN_ENCODER_A_WHITE, INPUT_PULLUP);
  pinMode(PIN_ENCODER_A_GREEN, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B_WHITE, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B_GREEN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A_WHITE), counter_a_white_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A_GREEN), counter_a_green_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B_WHITE), counter_b_white_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B_GREEN), counter_b_green_isr, RISING);

  if (!sne_encoder_a.begin()) while (true) delay(1000);
  if (!sne_encoder_b.begin()) while (true) delay(1000);
  if (!sne_controller.begin()) while (true) delay(1000);

  sne_encoder_a.setCommandHandler(handleCommand, &ctx_encoder_a);
  sne_encoder_b.setCommandHandler(handleCommand, &ctx_encoder_b);
  sne_controller.setCommandHandler(handleCommand, &ctx_controller);

  read_and_publish(true);
}

void loop() {
  sne_encoder_a.loop();
  sne_encoder_b.loop();
  sne_controller.loop();
  read_and_publish(false);
}
