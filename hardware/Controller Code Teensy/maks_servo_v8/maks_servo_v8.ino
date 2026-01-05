// Maks Servo Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - maks_servo_servo

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PWMServo.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC key, hex encoded (64 chars).
static const char *HMAC_SERVO = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins
static const int PIN_POWER_LED = 13;
static const int PIN_SERVO = 1;

// Positions
static const int OPEN_POSITION = 60;
static const int CLOSED_POSITION = 0;

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

static sentient_v8::Client sne_servo(make_cfg("maks_servo_servo", HMAC_SERVO));

static PWMServo servo;
static int current_position = CLOSED_POSITION;

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

static void publish_state(const char *last_op) {
  StaticJsonDocument<192> st;
  st["position"] = current_position;
  st["is_open"] = (current_position == OPEN_POSITION);
  st["last_op"] = last_op ? last_op : "";
  sne_servo.publishState(st);
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
  if (strcmp(op, "open") == 0) {
    current_position = OPEN_POSITION;
    servo.write(current_position);
    publish_state("open");
    return true;
  }
  if (strcmp(op, "close") == 0) {
    current_position = CLOSED_POSITION;
    servo.write(current_position);
    publish_state("close");
    return true;
  }
  if (strcmp(op, "set_position") == 0) {
    if (!p.containsKey("position")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    int position = p["position"] | 0;
    if (position < 0 || position > 180) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    current_position = position;
    servo.write(current_position);
    publish_state("set_position");
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

  servo.attach(PIN_SERVO);
  current_position = CLOSED_POSITION;
  servo.write(current_position);

  if (!sne_servo.begin()) while (true) delay(1000);
  sne_servo.setCommandHandler(handleCommand, nullptr);

  publish_state("boot");
}

void loop() {
  static unsigned long last_publish = 0;
  sne_servo.loop();

  const unsigned long now = millis();
  if (now - last_publish > 60UL * 1000UL) {
    publish_state("periodic");
    last_publish = now;
  }
}
