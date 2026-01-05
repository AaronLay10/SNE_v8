// Lab Cage B Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - lab_rm_cage_b_door_three
// - lab_rm_cage_b_door_four
// - lab_rm_cage_b_door_five

#include <Arduino.h>
#include <ArduinoJson.h>

#include <AccelStepper.h>
#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_DOOR_THREE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_DOOR_FOUR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_DOOR_FIVE = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (from v2)
static const int PIN_POWER_LED = 13;

// Door 3 sensors
static const int PIN_D3_OPEN_A = 8;
static const int PIN_D3_OPEN_B = 9;
static const int PIN_D3_CLOSED_A = 10;
static const int PIN_D3_CLOSED_B = 11;
static const int PIN_D3_ENABLE = 35;

// Door 4 sensors
static const int PIN_D4_OPEN_A = 0;
static const int PIN_D4_OPEN_B = 1;
static const int PIN_D4_CLOSED_A = 2;
static const int PIN_D4_CLOSED_B = 3;
static const int PIN_D4_ENABLE = 36; // shared with D5

// Door 5 sensors
static const int PIN_D5_OPEN_A = 20;
static const int PIN_D5_OPEN_B = 21;
static const int PIN_D5_CLOSED_A = 22;
static const int PIN_D5_CLOSED_B = 23;
static const int PIN_D5_ENABLE = 36;

static const float STEPPER_SPEED = 400.0f;
static const float STEPPER_ACCEL = 200.0f;

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

static sentient_v8::Client sne_door_three(make_cfg("lab_rm_cage_b_door_three", HMAC_DOOR_THREE));
static sentient_v8::Client sne_door_four(make_cfg("lab_rm_cage_b_door_four", HMAC_DOOR_FOUR));
static sentient_v8::Client sne_door_five(make_cfg("lab_rm_cage_b_door_five", HMAC_DOOR_FIVE));

static sentient_v8::Client *clients[] = {&sne_door_three, &sne_door_four, &sne_door_five};

enum class DeviceKind : uint8_t { DoorThree, DoorFour, DoorFive };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_door_three = {DeviceKind::DoorThree};
static DeviceCtx ctx_door_four = {DeviceKind::DoorFour};
static DeviceCtx ctx_door_five = {DeviceKind::DoorFive};

enum DoorDirection : uint8_t { STOPPED = 0, OPENING = 1, CLOSING = 2 };
static DoorDirection d3_direction = STOPPED;
static DoorDirection d4_direction = STOPPED;
static DoorDirection d5_direction = STOPPED;

// Door 3 steppers (2 motors)
static AccelStepper d3_stepper_one(AccelStepper::FULL4WIRE, 24, 25, 26, 27);
static AccelStepper d3_stepper_two(AccelStepper::FULL4WIRE, 28, 29, 30, 31);
// Door 4 stepper (1 motor)
static AccelStepper d4_stepper_one(AccelStepper::FULL4WIRE, 4, 5, 6, 7);
// Door 5 steppers (2 motors)
static AccelStepper d5_stepper_one(AccelStepper::FULL4WIRE, 16, 17, 18, 19);
static AccelStepper d5_stepper_two(AccelStepper::FULL4WIRE, 38, 39, 40, 41);

static int d3_open_a_last = -1, d3_open_b_last = -1, d3_closed_a_last = -1, d3_closed_b_last = -1;
static int d4_open_a_last = -1, d4_open_b_last = -1, d4_closed_a_last = -1, d4_closed_b_last = -1;
static int d5_open_a_last = -1, d5_open_b_last = -1, d5_closed_a_last = -1, d5_closed_b_last = -1;

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

static const char *dir_str(DoorDirection d) {
  switch (d) {
    case STOPPED: return "stopped";
    case OPENING: return "opening";
    case CLOSING: return "closing";
  }
  return "unknown";
}

static void set_door_direction(int door_num, DoorDirection dir) {
  if (door_num == 3) {
    d3_direction = dir;
    if (dir == STOPPED) {
      d3_stepper_one.stop();
      d3_stepper_two.stop();
      digitalWrite(PIN_D3_ENABLE, LOW);
    } else {
      digitalWrite(PIN_D3_ENABLE, HIGH);
    }
    return;
  }
  if (door_num == 4) {
    d4_direction = dir;
    if (dir == STOPPED) {
      d4_stepper_one.stop();
      if (d5_direction == STOPPED) digitalWrite(PIN_D4_ENABLE, LOW);
    } else {
      digitalWrite(PIN_D4_ENABLE, HIGH);
    }
    return;
  }
  if (door_num == 5) {
    d5_direction = dir;
    if (dir == STOPPED) {
      d5_stepper_one.stop();
      d5_stepper_two.stop();
      if (d4_direction == STOPPED) digitalWrite(PIN_D5_ENABLE, LOW);
    } else {
      digitalWrite(PIN_D5_ENABLE, HIGH);
    }
    return;
  }
}

static void update_door_motors() {
  if (d3_direction == OPENING) {
    d3_stepper_one.setSpeed(STEPPER_SPEED);
    d3_stepper_two.setSpeed(STEPPER_SPEED);
    d3_stepper_one.runSpeed();
    d3_stepper_two.runSpeed();
  } else if (d3_direction == CLOSING) {
    d3_stepper_one.setSpeed(-STEPPER_SPEED);
    d3_stepper_two.setSpeed(-STEPPER_SPEED);
    d3_stepper_one.runSpeed();
    d3_stepper_two.runSpeed();
  }

  if (d4_direction == OPENING) {
    d4_stepper_one.setSpeed(STEPPER_SPEED);
    d4_stepper_one.runSpeed();
  } else if (d4_direction == CLOSING) {
    d4_stepper_one.setSpeed(-STEPPER_SPEED);
    d4_stepper_one.runSpeed();
  }

  if (d5_direction == OPENING) {
    d5_stepper_one.setSpeed(STEPPER_SPEED);
    d5_stepper_two.setSpeed(STEPPER_SPEED);
    d5_stepper_one.runSpeed();
    d5_stepper_two.runSpeed();
  } else if (d5_direction == CLOSING) {
    d5_stepper_one.setSpeed(-STEPPER_SPEED);
    d5_stepper_two.setSpeed(-STEPPER_SPEED);
    d5_stepper_one.runSpeed();
    d5_stepper_two.runSpeed();
  }
}

static void publish_all(const char *reason) {
  {
    StaticJsonDocument<256> st;
    st["open_a"] = (digitalRead(PIN_D3_OPEN_A) == HIGH);
    st["open_b"] = (digitalRead(PIN_D3_OPEN_B) == HIGH);
    st["closed_a"] = (digitalRead(PIN_D3_CLOSED_A) == HIGH);
    st["closed_b"] = (digitalRead(PIN_D3_CLOSED_B) == HIGH);
    st["direction"] = dir_str(d3_direction);
    st["reason"] = reason ? reason : "";
    sne_door_three.publishState(st);
  }
  {
    StaticJsonDocument<256> st;
    st["open_a"] = (digitalRead(PIN_D4_OPEN_A) == HIGH);
    st["open_b"] = (digitalRead(PIN_D4_OPEN_B) == HIGH);
    st["closed_a"] = (digitalRead(PIN_D4_CLOSED_A) == HIGH);
    st["closed_b"] = (digitalRead(PIN_D4_CLOSED_B) == HIGH);
    st["direction"] = dir_str(d4_direction);
    st["reason"] = reason ? reason : "";
    sne_door_four.publishState(st);
  }
  {
    StaticJsonDocument<256> st;
    st["open_a"] = (digitalRead(PIN_D5_OPEN_A) == HIGH);
    st["open_b"] = (digitalRead(PIN_D5_OPEN_B) == HIGH);
    st["closed_a"] = (digitalRead(PIN_D5_CLOSED_A) == HIGH);
    st["closed_b"] = (digitalRead(PIN_D5_CLOSED_B) == HIGH);
    st["direction"] = dir_str(d5_direction);
    st["reason"] = reason ? reason : "";
    sne_door_five.publishState(st);
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

  if (strcmp(op, "request_status") == 0) {
    publish_all("request_status");
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

  int door_num = 0;
  if (ctx->kind == DeviceKind::DoorThree) door_num = 3;
  if (ctx->kind == DeviceKind::DoorFour) door_num = 4;
  if (ctx->kind == DeviceKind::DoorFive) door_num = 5;
  if (door_num == 0) {
    rejectedAckReason["reason_code"] = "INTERNAL_ERROR";
    return false;
  }

  if (strcmp(op, "open") == 0) {
    set_door_direction(door_num, OPENING);
    publish_all("open");
    return true;
  }
  if (strcmp(op, "close") == 0) {
    set_door_direction(door_num, CLOSING);
    publish_all("close");
    return true;
  }
  if (strcmp(op, "stop") == 0) {
    set_door_direction(door_num, STOPPED);
    publish_all("stop");
    return true;
  }

  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  pinMode(PIN_D3_OPEN_A, INPUT_PULLDOWN);
  pinMode(PIN_D3_OPEN_B, INPUT_PULLDOWN);
  pinMode(PIN_D3_CLOSED_A, INPUT_PULLDOWN);
  pinMode(PIN_D3_CLOSED_B, INPUT_PULLDOWN);
  pinMode(PIN_D4_OPEN_A, INPUT_PULLDOWN);
  pinMode(PIN_D4_OPEN_B, INPUT_PULLDOWN);
  pinMode(PIN_D4_CLOSED_A, INPUT_PULLDOWN);
  pinMode(PIN_D4_CLOSED_B, INPUT_PULLDOWN);
  pinMode(PIN_D5_OPEN_A, INPUT_PULLDOWN);
  pinMode(PIN_D5_OPEN_B, INPUT_PULLDOWN);
  pinMode(PIN_D5_CLOSED_A, INPUT_PULLDOWN);
  pinMode(PIN_D5_CLOSED_B, INPUT_PULLDOWN);

  pinMode(PIN_D3_ENABLE, OUTPUT);
  pinMode(PIN_D4_ENABLE, OUTPUT);
  digitalWrite(PIN_D3_ENABLE, LOW);
  digitalWrite(PIN_D4_ENABLE, LOW);

  d3_stepper_one.setMaxSpeed(STEPPER_SPEED);
  d3_stepper_one.setAcceleration(STEPPER_ACCEL);
  d3_stepper_two.setMaxSpeed(STEPPER_SPEED);
  d3_stepper_two.setAcceleration(STEPPER_ACCEL);
  d4_stepper_one.setMaxSpeed(STEPPER_SPEED);
  d4_stepper_one.setAcceleration(STEPPER_ACCEL);
  d5_stepper_one.setMaxSpeed(STEPPER_SPEED);
  d5_stepper_one.setAcceleration(STEPPER_ACCEL);
  d5_stepper_two.setMaxSpeed(STEPPER_SPEED);
  d5_stepper_two.setAcceleration(STEPPER_ACCEL);

  if (!sne_door_three.begin()) while (true) delay(1000);
  if (!sne_door_four.begin()) while (true) delay(1000);
  if (!sne_door_five.begin()) while (true) delay(1000);

  sne_door_three.setCommandHandler(handleCommand, &ctx_door_three);
  sne_door_four.setCommandHandler(handleCommand, &ctx_door_four);
  sne_door_five.setCommandHandler(handleCommand, &ctx_door_five);

  d3_open_a_last = digitalRead(PIN_D3_OPEN_A);
  d3_open_b_last = digitalRead(PIN_D3_OPEN_B);
  d3_closed_a_last = digitalRead(PIN_D3_CLOSED_A);
  d3_closed_b_last = digitalRead(PIN_D3_CLOSED_B);
  d4_open_a_last = digitalRead(PIN_D4_OPEN_A);
  d4_open_b_last = digitalRead(PIN_D4_OPEN_B);
  d4_closed_a_last = digitalRead(PIN_D4_CLOSED_A);
  d4_closed_b_last = digitalRead(PIN_D4_CLOSED_B);
  d5_open_a_last = digitalRead(PIN_D5_OPEN_A);
  d5_open_b_last = digitalRead(PIN_D5_OPEN_B);
  d5_closed_a_last = digitalRead(PIN_D5_CLOSED_A);
  d5_closed_b_last = digitalRead(PIN_D5_CLOSED_B);

  publish_all("boot");
}

void loop() {
  sne_door_three.loop();
  sne_door_four.loop();
  sne_door_five.loop();

  update_door_motors();

  int d3_open_a = digitalRead(PIN_D3_OPEN_A);
  int d3_open_b = digitalRead(PIN_D3_OPEN_B);
  int d3_closed_a = digitalRead(PIN_D3_CLOSED_A);
  int d3_closed_b = digitalRead(PIN_D3_CLOSED_B);
  int d4_open_a = digitalRead(PIN_D4_OPEN_A);
  int d4_open_b = digitalRead(PIN_D4_OPEN_B);
  int d4_closed_a = digitalRead(PIN_D4_CLOSED_A);
  int d4_closed_b = digitalRead(PIN_D4_CLOSED_B);
  int d5_open_a = digitalRead(PIN_D5_OPEN_A);
  int d5_open_b = digitalRead(PIN_D5_OPEN_B);
  int d5_closed_a = digitalRead(PIN_D5_CLOSED_A);
  int d5_closed_b = digitalRead(PIN_D5_CLOSED_B);

  bool changed = false;
  if (d3_open_a != d3_open_a_last) changed = true;
  if (d3_open_b != d3_open_b_last) changed = true;
  if (d3_closed_a != d3_closed_a_last) changed = true;
  if (d3_closed_b != d3_closed_b_last) changed = true;
  if (d4_open_a != d4_open_a_last) changed = true;
  if (d4_open_b != d4_open_b_last) changed = true;
  if (d4_closed_a != d4_closed_a_last) changed = true;
  if (d4_closed_b != d4_closed_b_last) changed = true;
  if (d5_open_a != d5_open_a_last) changed = true;
  if (d5_open_b != d5_open_b_last) changed = true;
  if (d5_closed_a != d5_closed_a_last) changed = true;
  if (d5_closed_b != d5_closed_b_last) changed = true;

  static unsigned long last_publish = 0;
  const unsigned long now = millis();
  if (changed || (now - last_publish) > SENSOR_REFRESH_MS) {
    last_publish = now;
    d3_open_a_last = d3_open_a;
    d3_open_b_last = d3_open_b;
    d3_closed_a_last = d3_closed_a;
    d3_closed_b_last = d3_closed_b;
    d4_open_a_last = d4_open_a;
    d4_open_b_last = d4_open_b;
    d4_closed_a_last = d4_closed_a;
    d4_closed_b_last = d4_closed_b;
    d5_open_a_last = d5_open_a;
    d5_open_b_last = d5_open_b;
    d5_closed_a_last = d5_closed_a;
    d5_closed_b_last = d5_closed_b;
    publish_all(changed ? "change" : "periodic");
  }
}
