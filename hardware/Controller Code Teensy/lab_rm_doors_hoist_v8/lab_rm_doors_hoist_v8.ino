// Lab Doors & Hoist Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - lab_rm_doors_hoist_hoist
// - lab_rm_doors_hoist_lab_door_left
// - lab_rm_doors_hoist_lab_door_right
// - lab_rm_doors_hoist_gun_ir_receiver
// - lab_rm_doors_hoist_rope_drop

#include <Arduino.h>
#include <ArduinoJson.h>

// Suppress IRremote begin() error - receiver only.
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>

#include <AccelStepper.h>
#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_HOIST = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEFT_DOOR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RIGHT_DOOR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_IR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_ROPE = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (from v2)
static const int PIN_POWER_LED = 13;

// Hoist sensors
static const int PIN_HOIST_UP_A = 14;
static const int PIN_HOIST_UP_B = 15;
static const int PIN_HOIST_DOWN_A = 16;
static const int PIN_HOIST_DOWN_B = 17;
static const int PIN_HOIST_ENABLE = 20;

// Left lab door sensors
static const int PIN_LEFT_OPEN_A = 37;
static const int PIN_LEFT_OPEN_B = 38;
static const int PIN_LEFT_CLOSED_A = 39;
static const int PIN_LEFT_CLOSED_B = 40;

// Right lab door sensors
static const int PIN_RIGHT_OPEN_A = 33;
static const int PIN_RIGHT_OPEN_B = 34;
static const int PIN_RIGHT_CLOSED_A = 35;
static const int PIN_RIGHT_CLOSED_B = 36;

static const int PIN_LAB_DOORS_ENABLE = 19;

// IR receiver and rope drop
static const int PIN_IR_RECEIVER = 21;
static const int PIN_ROPE_DROP = 23;

// Stepper motor configuration
static const float STEPPER_SPEED = 400.0f;
static const float STEPPER_ACCEL = 200.0f;

static const uint8_t EXPECTED_GUN_IR_CODE = 0x51;
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
  c.heartbeatIntervalMs = 1000;
  c.rxJsonCapacity = 2048;
  c.txJsonCapacity = 2048;
  return c;
}

static sentient_v8::Client sne_hoist(make_cfg("lab_rm_doors_hoist_hoist", HMAC_HOIST));
static sentient_v8::Client sne_left_door(make_cfg("lab_rm_doors_hoist_lab_door_left", HMAC_LEFT_DOOR));
static sentient_v8::Client sne_right_door(make_cfg("lab_rm_doors_hoist_lab_door_right", HMAC_RIGHT_DOOR));
static sentient_v8::Client sne_ir(make_cfg("lab_rm_doors_hoist_gun_ir_receiver", HMAC_IR));
static sentient_v8::Client sne_rope_drop(make_cfg("lab_rm_doors_hoist_rope_drop", HMAC_ROPE));

enum class DeviceKind : uint8_t { Hoist, LeftDoor, RightDoor, IrReceiver, RopeDrop };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_hoist = {DeviceKind::Hoist};
static DeviceCtx ctx_left = {DeviceKind::LeftDoor};
static DeviceCtx ctx_right = {DeviceKind::RightDoor};
static DeviceCtx ctx_ir = {DeviceKind::IrReceiver};
static DeviceCtx ctx_rope = {DeviceKind::RopeDrop};

enum Direction : uint8_t { STOPPED = 0, MOVING = 1 };
static Direction hoist_direction = STOPPED;
static int hoist_target = 0; // 1=up, -1=down
static Direction left_direction = STOPPED;
static int left_target = 0; // 1=open, -1=close
static Direction right_direction = STOPPED;
static int right_target = 0; // 1=open, -1=close

// Hoist steppers (2 motors)
static AccelStepper hoist_stepper_one(AccelStepper::FULL4WIRE, 0, 1, 2, 3);
static AccelStepper hoist_stepper_two(AccelStepper::FULL4WIRE, 5, 6, 7, 8);

// Left door steppers (2 motors)
static AccelStepper left_stepper_one(AccelStepper::FULL4WIRE, 24, 25, 26, 27);
static AccelStepper left_stepper_two(AccelStepper::FULL4WIRE, 28, 29, 30, 31);

// Right door steppers (2 motors)
static AccelStepper right_stepper_one(AccelStepper::FULL4WIRE, 4, 5, 6, 7);
static AccelStepper right_stepper_two(AccelStepper::FULL4WIRE, 9, 10, 11, 12);

static int hoist_up_a_last = -1, hoist_up_b_last = -1, hoist_down_a_last = -1, hoist_down_b_last = -1;
static int left_open_a_last = -1, left_open_b_last = -1, left_closed_a_last = -1, left_closed_b_last = -1;
static int right_open_a_last = -1, right_open_b_last = -1, right_closed_a_last = -1, right_closed_b_last = -1;

static bool rope_drop_on = false;
static uint8_t last_ir_code = 0;
static bool ir_seen = false;

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

static void set_hoist_direction(int target) {
  hoist_target = target;
  if (target == 0) {
    hoist_direction = STOPPED;
    hoist_stepper_one.stop();
    hoist_stepper_two.stop();
    digitalWrite(PIN_HOIST_ENABLE, LOW);
  } else {
    hoist_direction = MOVING;
    digitalWrite(PIN_HOIST_ENABLE, HIGH);
  }
}

static void set_door_direction(bool is_left, int target) {
  if (is_left) {
    left_target = target;
    if (target == 0) {
      left_direction = STOPPED;
      left_stepper_one.stop();
      left_stepper_two.stop();
    } else {
      left_direction = MOVING;
    }
  } else {
    right_target = target;
    if (target == 0) {
      right_direction = STOPPED;
      right_stepper_one.stop();
      right_stepper_two.stop();
    } else {
      right_direction = MOVING;
    }
  }

  // Enable pin shared by both doors.
  if (left_direction != STOPPED || right_direction != STOPPED) {
    digitalWrite(PIN_LAB_DOORS_ENABLE, HIGH);
  } else {
    digitalWrite(PIN_LAB_DOORS_ENABLE, LOW);
  }
}

static void update_motors() {
  if (hoist_direction == MOVING) {
    float speed = (hoist_target > 0) ? STEPPER_SPEED : -STEPPER_SPEED;
    hoist_stepper_one.setSpeed(speed);
    hoist_stepper_two.setSpeed(speed);
    hoist_stepper_one.runSpeed();
    hoist_stepper_two.runSpeed();
  }

  if (left_direction == MOVING) {
    float speed = (left_target > 0) ? STEPPER_SPEED : -STEPPER_SPEED;
    left_stepper_one.setSpeed(speed);
    left_stepper_two.setSpeed(speed);
    left_stepper_one.runSpeed();
    left_stepper_two.runSpeed();
  }

  if (right_direction == MOVING) {
    float speed = (right_target > 0) ? STEPPER_SPEED : -STEPPER_SPEED;
    right_stepper_one.setSpeed(speed);
    right_stepper_two.setSpeed(speed);
    right_stepper_one.runSpeed();
    right_stepper_two.runSpeed();
  }
}

static void publish_all(const char *reason) {
  {
    StaticJsonDocument<256> st;
    st["up_a"] = (digitalRead(PIN_HOIST_UP_A) == HIGH);
    st["up_b"] = (digitalRead(PIN_HOIST_UP_B) == HIGH);
    st["down_a"] = (digitalRead(PIN_HOIST_DOWN_A) == HIGH);
    st["down_b"] = (digitalRead(PIN_HOIST_DOWN_B) == HIGH);
    st["moving"] = (hoist_direction == MOVING);
    st["target"] = hoist_target;
    st["reason"] = reason ? reason : "";
    sne_hoist.publishState(st);
  }
  {
    StaticJsonDocument<256> st;
    st["open_a"] = (digitalRead(PIN_LEFT_OPEN_A) == HIGH);
    st["open_b"] = (digitalRead(PIN_LEFT_OPEN_B) == HIGH);
    st["closed_a"] = (digitalRead(PIN_LEFT_CLOSED_A) == HIGH);
    st["closed_b"] = (digitalRead(PIN_LEFT_CLOSED_B) == HIGH);
    st["moving"] = (left_direction == MOVING);
    st["target"] = left_target;
    st["reason"] = reason ? reason : "";
    sne_left_door.publishState(st);
  }
  {
    StaticJsonDocument<256> st;
    st["open_a"] = (digitalRead(PIN_RIGHT_OPEN_A) == HIGH);
    st["open_b"] = (digitalRead(PIN_RIGHT_OPEN_B) == HIGH);
    st["closed_a"] = (digitalRead(PIN_RIGHT_CLOSED_A) == HIGH);
    st["closed_b"] = (digitalRead(PIN_RIGHT_CLOSED_B) == HIGH);
    st["moving"] = (right_direction == MOVING);
    st["target"] = right_target;
    st["reason"] = reason ? reason : "";
    sne_right_door.publishState(st);
  }
  {
    StaticJsonDocument<256> st;
    st["expected"] = EXPECTED_GUN_IR_CODE;
    if (ir_seen) {
      st["last_code"] = last_ir_code;
      st["match"] = (last_ir_code == EXPECTED_GUN_IR_CODE);
    }
    st["reason"] = reason ? reason : "";
    sne_ir.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["on"] = rope_drop_on;
    st["reason"] = reason ? reason : "";
    sne_rope_drop.publishState(st);
  }
}

static void check_ir_receiver() {
  if (!IrReceiver.decode()) return;

  uint8_t code = (uint8_t)IrReceiver.decodedIRData.command;
  last_ir_code = code;
  ir_seen = true;

  StaticJsonDocument<192> ev;
  ev["code"] = code;
  ev["expected"] = EXPECTED_GUN_IR_CODE;
  ev["match"] = (code == EXPECTED_GUN_IR_CODE);
  ev["ts_ms"] = millis();
  sne_ir.publishTelemetry(ev);

  IrReceiver.resume();
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

  if (ctx->kind == DeviceKind::Hoist) {
    if (strcmp(op, "up") == 0) {
      set_hoist_direction(1);
      publish_all("up");
      return true;
    }
    if (strcmp(op, "down") == 0) {
      set_hoist_direction(-1);
      publish_all("down");
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      set_hoist_direction(0);
      publish_all("stop");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::LeftDoor || ctx->kind == DeviceKind::RightDoor) {
    bool is_left = (ctx->kind == DeviceKind::LeftDoor);
    if (strcmp(op, "open") == 0) {
      set_door_direction(is_left, 1);
      publish_all("open");
      return true;
    }
    if (strcmp(op, "close") == 0) {
      set_door_direction(is_left, -1);
      publish_all("close");
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      set_door_direction(is_left, 0);
      publish_all("stop");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::RopeDrop) {
    if (strcmp(op, "drop") == 0) {
      digitalWrite(PIN_ROPE_DROP, HIGH);
      rope_drop_on = true;
      publish_all("drop");
      return true;
    }
    if (strcmp(op, "reset") == 0) {
      digitalWrite(PIN_ROPE_DROP, LOW);
      rope_drop_on = false;
      publish_all("reset");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // IR receiver is sensor-only.
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  pinMode(PIN_HOIST_UP_A, INPUT_PULLDOWN);
  pinMode(PIN_HOIST_UP_B, INPUT_PULLDOWN);
  pinMode(PIN_HOIST_DOWN_A, INPUT_PULLDOWN);
  pinMode(PIN_HOIST_DOWN_B, INPUT_PULLDOWN);

  pinMode(PIN_LEFT_OPEN_A, INPUT_PULLDOWN);
  pinMode(PIN_LEFT_OPEN_B, INPUT_PULLDOWN);
  pinMode(PIN_LEFT_CLOSED_A, INPUT_PULLDOWN);
  pinMode(PIN_LEFT_CLOSED_B, INPUT_PULLDOWN);

  pinMode(PIN_RIGHT_OPEN_A, INPUT_PULLDOWN);
  pinMode(PIN_RIGHT_OPEN_B, INPUT_PULLDOWN);
  pinMode(PIN_RIGHT_CLOSED_A, INPUT_PULLDOWN);
  pinMode(PIN_RIGHT_CLOSED_B, INPUT_PULLDOWN);

  pinMode(PIN_HOIST_ENABLE, OUTPUT);
  pinMode(PIN_LAB_DOORS_ENABLE, OUTPUT);
  pinMode(PIN_ROPE_DROP, OUTPUT);
  digitalWrite(PIN_HOIST_ENABLE, LOW);
  digitalWrite(PIN_LAB_DOORS_ENABLE, LOW);
  digitalWrite(PIN_ROPE_DROP, LOW);
  rope_drop_on = false;

  IrReceiver.begin(PIN_IR_RECEIVER, DISABLE_LED_FEEDBACK, PIN_POWER_LED);

  hoist_stepper_one.setMaxSpeed(STEPPER_SPEED);
  hoist_stepper_one.setAcceleration(STEPPER_ACCEL);
  hoist_stepper_two.setMaxSpeed(STEPPER_SPEED);
  hoist_stepper_two.setAcceleration(STEPPER_ACCEL);

  left_stepper_one.setMaxSpeed(STEPPER_SPEED);
  left_stepper_one.setAcceleration(STEPPER_ACCEL);
  left_stepper_two.setMaxSpeed(STEPPER_SPEED);
  left_stepper_two.setAcceleration(STEPPER_ACCEL);

  right_stepper_one.setMaxSpeed(STEPPER_SPEED);
  right_stepper_one.setAcceleration(STEPPER_ACCEL);
  right_stepper_two.setMaxSpeed(STEPPER_SPEED);
  right_stepper_two.setAcceleration(STEPPER_ACCEL);

  if (!sne_hoist.begin()) while (true) delay(1000);
  if (!sne_left_door.begin()) while (true) delay(1000);
  if (!sne_right_door.begin()) while (true) delay(1000);
  if (!sne_ir.begin()) while (true) delay(1000);
  if (!sne_rope_drop.begin()) while (true) delay(1000);

  sne_hoist.setCommandHandler(handleCommand, &ctx_hoist);
  sne_left_door.setCommandHandler(handleCommand, &ctx_left);
  sne_right_door.setCommandHandler(handleCommand, &ctx_right);
  sne_ir.setCommandHandler(handleCommand, &ctx_ir);
  sne_rope_drop.setCommandHandler(handleCommand, &ctx_rope);

  hoist_up_a_last = digitalRead(PIN_HOIST_UP_A);
  hoist_up_b_last = digitalRead(PIN_HOIST_UP_B);
  hoist_down_a_last = digitalRead(PIN_HOIST_DOWN_A);
  hoist_down_b_last = digitalRead(PIN_HOIST_DOWN_B);
  left_open_a_last = digitalRead(PIN_LEFT_OPEN_A);
  left_open_b_last = digitalRead(PIN_LEFT_OPEN_B);
  left_closed_a_last = digitalRead(PIN_LEFT_CLOSED_A);
  left_closed_b_last = digitalRead(PIN_LEFT_CLOSED_B);
  right_open_a_last = digitalRead(PIN_RIGHT_OPEN_A);
  right_open_b_last = digitalRead(PIN_RIGHT_OPEN_B);
  right_closed_a_last = digitalRead(PIN_RIGHT_CLOSED_A);
  right_closed_b_last = digitalRead(PIN_RIGHT_CLOSED_B);

  publish_all("boot");
}

void loop() {
  sne_hoist.loop();
  sne_left_door.loop();
  sne_right_door.loop();
  sne_ir.loop();
  sne_rope_drop.loop();

  update_motors();
  check_ir_receiver();

  int hu_a = digitalRead(PIN_HOIST_UP_A);
  int hu_b = digitalRead(PIN_HOIST_UP_B);
  int hd_a = digitalRead(PIN_HOIST_DOWN_A);
  int hd_b = digitalRead(PIN_HOIST_DOWN_B);
  int lo_a = digitalRead(PIN_LEFT_OPEN_A);
  int lo_b = digitalRead(PIN_LEFT_OPEN_B);
  int lc_a = digitalRead(PIN_LEFT_CLOSED_A);
  int lc_b = digitalRead(PIN_LEFT_CLOSED_B);
  int ro_a = digitalRead(PIN_RIGHT_OPEN_A);
  int ro_b = digitalRead(PIN_RIGHT_OPEN_B);
  int rc_a = digitalRead(PIN_RIGHT_CLOSED_A);
  int rc_b = digitalRead(PIN_RIGHT_CLOSED_B);

  bool changed = false;
  if (hu_a != hoist_up_a_last) changed = true;
  if (hu_b != hoist_up_b_last) changed = true;
  if (hd_a != hoist_down_a_last) changed = true;
  if (hd_b != hoist_down_b_last) changed = true;
  if (lo_a != left_open_a_last) changed = true;
  if (lo_b != left_open_b_last) changed = true;
  if (lc_a != left_closed_a_last) changed = true;
  if (lc_b != left_closed_b_last) changed = true;
  if (ro_a != right_open_a_last) changed = true;
  if (ro_b != right_open_b_last) changed = true;
  if (rc_a != right_closed_a_last) changed = true;
  if (rc_b != right_closed_b_last) changed = true;

  static unsigned long last_publish = 0;
  const unsigned long now = millis();
  if (changed || (now - last_publish) > SENSOR_REFRESH_MS) {
    last_publish = now;
    hoist_up_a_last = hu_a;
    hoist_up_b_last = hu_b;
    hoist_down_a_last = hd_a;
    hoist_down_b_last = hd_b;
    left_open_a_last = lo_a;
    left_open_b_last = lo_b;
    left_closed_a_last = lc_a;
    left_closed_b_last = lc_b;
    right_open_a_last = ro_a;
    right_open_b_last = ro_b;
    right_closed_a_last = rc_a;
    right_closed_b_last = rc_b;
    publish_all(changed ? "change" : "periodic");
  }
}
