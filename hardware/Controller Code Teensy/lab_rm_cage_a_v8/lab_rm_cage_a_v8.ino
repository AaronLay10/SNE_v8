// Lab Cage A Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - lab_rm_cage_a_door_one
// - lab_rm_cage_a_door_two
// - lab_rm_cage_a_canister_charging

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
static const char *HMAC_DOOR_ONE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_DOOR_TWO = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CHARGING = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (from v2)
static const int PIN_POWER_LED = 13;

// Door 1 sensors
static const int PIN_D1_OPEN_A = 9;
static const int PIN_D1_OPEN_B = 10;
static const int PIN_D1_CLOSED_A = 11;
static const int PIN_D1_CLOSED_B = 12;
static const int PIN_D1_ENABLE = 35;

// Door 2 sensors
static const int PIN_D2_OPEN_A = 0;
static const int PIN_D2_OPEN_B = 1;
static const int PIN_D2_CLOSED_A = 2;
static const int PIN_D2_CLOSED_B = 3;
static const int PIN_D2_ENABLE = 36;

// Canister charging output
static const int PIN_CANISTER_CHARGING = 41;

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

static sentient_v8::Client sne_door_one(make_cfg("lab_rm_cage_a_door_one", HMAC_DOOR_ONE));
static sentient_v8::Client sne_door_two(make_cfg("lab_rm_cage_a_door_two", HMAC_DOOR_TWO));
static sentient_v8::Client sne_canister_charging(make_cfg("lab_rm_cage_a_canister_charging", HMAC_CHARGING));

static sentient_v8::Client *clients[] = {&sne_door_one, &sne_door_two, &sne_canister_charging};

enum class DeviceKind : uint8_t { DoorOne, DoorTwo, Charging };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_door_one = {DeviceKind::DoorOne};
static DeviceCtx ctx_door_two = {DeviceKind::DoorTwo};
static DeviceCtx ctx_charging = {DeviceKind::Charging};

enum DoorDirection : uint8_t { STOPPED = 0, OPENING = 1, CLOSING = 2 };
static DoorDirection d1_direction = STOPPED;
static DoorDirection d2_direction = STOPPED;

// Door 1 steppers (pins: Pul+, Pul-, Dir+, Dir-)
static AccelStepper d1_stepper_one(AccelStepper::FULL4WIRE, 24, 25, 26, 27);
static AccelStepper d1_stepper_two(AccelStepper::FULL4WIRE, 28, 29, 30, 31);
// Door 2 stepper
static AccelStepper d2_stepper_one(AccelStepper::FULL4WIRE, 4, 5, 6, 7);

static bool canister_charging_on = false;

static int d1_open_a_last = -1;
static int d1_open_b_last = -1;
static int d1_closed_a_last = -1;
static int d1_closed_b_last = -1;
static int d2_open_a_last = -1;
static int d2_open_b_last = -1;
static int d2_closed_a_last = -1;
static int d2_closed_b_last = -1;

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
  if (door_num == 1) {
    d1_direction = dir;
    if (dir == STOPPED) {
      d1_stepper_one.stop();
      d1_stepper_two.stop();
      digitalWrite(PIN_D1_ENABLE, LOW);
    } else {
      digitalWrite(PIN_D1_ENABLE, HIGH);
    }
    return;
  }
  if (door_num == 2) {
    d2_direction = dir;
    if (dir == STOPPED) {
      d2_stepper_one.stop();
      digitalWrite(PIN_D2_ENABLE, LOW);
    } else {
      digitalWrite(PIN_D2_ENABLE, HIGH);
    }
    return;
  }
}

static void update_door_motors() {
  if (d1_direction == OPENING) {
    d1_stepper_one.setSpeed(STEPPER_SPEED);
    d1_stepper_two.setSpeed(STEPPER_SPEED);
    d1_stepper_one.runSpeed();
    d1_stepper_two.runSpeed();
  } else if (d1_direction == CLOSING) {
    d1_stepper_one.setSpeed(-STEPPER_SPEED);
    d1_stepper_two.setSpeed(-STEPPER_SPEED);
    d1_stepper_one.runSpeed();
    d1_stepper_two.runSpeed();
  }

  if (d2_direction == OPENING) {
    d2_stepper_one.setSpeed(STEPPER_SPEED);
    d2_stepper_one.runSpeed();
  } else if (d2_direction == CLOSING) {
    d2_stepper_one.setSpeed(-STEPPER_SPEED);
    d2_stepper_one.runSpeed();
  }
}

static void publish_door_states(const char *reason) {
  {
    StaticJsonDocument<256> st;
    st["open_a"] = (digitalRead(PIN_D1_OPEN_A) == HIGH);
    st["open_b"] = (digitalRead(PIN_D1_OPEN_B) == HIGH);
    st["closed_a"] = (digitalRead(PIN_D1_CLOSED_A) == HIGH);
    st["closed_b"] = (digitalRead(PIN_D1_CLOSED_B) == HIGH);
    st["direction"] = dir_str(d1_direction);
    st["reason"] = reason ? reason : "";
    sne_door_one.publishState(st);
  }
  {
    StaticJsonDocument<256> st;
    st["open_a"] = (digitalRead(PIN_D2_OPEN_A) == HIGH);
    st["open_b"] = (digitalRead(PIN_D2_OPEN_B) == HIGH);
    st["closed_a"] = (digitalRead(PIN_D2_CLOSED_A) == HIGH);
    st["closed_b"] = (digitalRead(PIN_D2_CLOSED_B) == HIGH);
    st["direction"] = dir_str(d2_direction);
    st["reason"] = reason ? reason : "";
    sne_door_two.publishState(st);
  }
}

static void publish_charging_state(const char *reason) {
  StaticJsonDocument<128> st;
  st["on"] = canister_charging_on;
  st["reason"] = reason ? reason : "";
  sne_canister_charging.publishState(st);
}

static void publish_all(const char *reason) {
  publish_door_states(reason);
  publish_charging_state(reason);
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

  if (ctx->kind == DeviceKind::Charging) {
    if (strcmp(op, "set") == 0) {
      if (!p.containsKey("on")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      bool on = p["on"] | false;
      digitalWrite(PIN_CANISTER_CHARGING, on ? HIGH : LOW);
      canister_charging_on = on;
      publish_charging_state("set");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Doors
  int door_num = (ctx->kind == DeviceKind::DoorOne) ? 1 : 2;
  if (strcmp(op, "open") == 0) {
    set_door_direction(door_num, OPENING);
    publish_door_states("open");
    return true;
  }
  if (strcmp(op, "close") == 0) {
    set_door_direction(door_num, CLOSING);
    publish_door_states("close");
    return true;
  }
  if (strcmp(op, "stop") == 0) {
    set_door_direction(door_num, STOPPED);
    publish_door_states("stop");
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

  pinMode(PIN_D1_OPEN_A, INPUT_PULLDOWN);
  pinMode(PIN_D1_OPEN_B, INPUT_PULLDOWN);
  pinMode(PIN_D1_CLOSED_A, INPUT_PULLDOWN);
  pinMode(PIN_D1_CLOSED_B, INPUT_PULLDOWN);
  pinMode(PIN_D2_OPEN_A, INPUT_PULLDOWN);
  pinMode(PIN_D2_OPEN_B, INPUT_PULLDOWN);
  pinMode(PIN_D2_CLOSED_A, INPUT_PULLDOWN);
  pinMode(PIN_D2_CLOSED_B, INPUT_PULLDOWN);

  pinMode(PIN_D1_ENABLE, OUTPUT);
  pinMode(PIN_D2_ENABLE, OUTPUT);
  pinMode(PIN_CANISTER_CHARGING, OUTPUT);
  digitalWrite(PIN_D1_ENABLE, LOW);
  digitalWrite(PIN_D2_ENABLE, LOW);
  digitalWrite(PIN_CANISTER_CHARGING, LOW);
  canister_charging_on = false;

  d1_stepper_one.setMaxSpeed(STEPPER_SPEED);
  d1_stepper_one.setAcceleration(STEPPER_ACCEL);
  d1_stepper_two.setMaxSpeed(STEPPER_SPEED);
  d1_stepper_two.setAcceleration(STEPPER_ACCEL);
  d2_stepper_one.setMaxSpeed(STEPPER_SPEED);
  d2_stepper_one.setAcceleration(STEPPER_ACCEL);

  if (!sne_door_one.begin()) while (true) delay(1000);
  if (!sne_door_two.begin()) while (true) delay(1000);
  if (!sne_canister_charging.begin()) while (true) delay(1000);

  sne_door_one.setCommandHandler(handleCommand, &ctx_door_one);
  sne_door_two.setCommandHandler(handleCommand, &ctx_door_two);
  sne_canister_charging.setCommandHandler(handleCommand, &ctx_charging);

  d1_open_a_last = digitalRead(PIN_D1_OPEN_A);
  d1_open_b_last = digitalRead(PIN_D1_OPEN_B);
  d1_closed_a_last = digitalRead(PIN_D1_CLOSED_A);
  d1_closed_b_last = digitalRead(PIN_D1_CLOSED_B);
  d2_open_a_last = digitalRead(PIN_D2_OPEN_A);
  d2_open_b_last = digitalRead(PIN_D2_OPEN_B);
  d2_closed_a_last = digitalRead(PIN_D2_CLOSED_A);
  d2_closed_b_last = digitalRead(PIN_D2_CLOSED_B);

  publish_all("boot");
}

void loop() {
  sne_door_one.loop();
  sne_door_two.loop();
  sne_canister_charging.loop();

  update_door_motors();

  int d1_open_a = digitalRead(PIN_D1_OPEN_A);
  int d1_open_b = digitalRead(PIN_D1_OPEN_B);
  int d1_closed_a = digitalRead(PIN_D1_CLOSED_A);
  int d1_closed_b = digitalRead(PIN_D1_CLOSED_B);
  int d2_open_a = digitalRead(PIN_D2_OPEN_A);
  int d2_open_b = digitalRead(PIN_D2_OPEN_B);
  int d2_closed_a = digitalRead(PIN_D2_CLOSED_A);
  int d2_closed_b = digitalRead(PIN_D2_CLOSED_B);

  bool changed = false;
  if (d1_open_a != d1_open_a_last) changed = true;
  if (d1_open_b != d1_open_b_last) changed = true;
  if (d1_closed_a != d1_closed_a_last) changed = true;
  if (d1_closed_b != d1_closed_b_last) changed = true;
  if (d2_open_a != d2_open_a_last) changed = true;
  if (d2_open_b != d2_open_b_last) changed = true;
  if (d2_closed_a != d2_closed_a_last) changed = true;
  if (d2_closed_b != d2_closed_b_last) changed = true;

  static unsigned long last_publish = 0;
  const unsigned long now = millis();
  if (changed || (now - last_publish) > SENSOR_REFRESH_MS) {
    last_publish = now;
    d1_open_a_last = d1_open_a;
    d1_open_b_last = d1_open_b;
    d1_closed_a_last = d1_closed_a;
    d1_closed_b_last = d1_closed_b;
    d2_open_a_last = d2_open_a;
    d2_open_b_last = d2_open_b;
    d2_closed_a_last = d2_closed_a;
    d2_closed_b_last = d2_closed_b;
    publish_door_states(changed ? "change" : "periodic");
  }
}
