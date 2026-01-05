// study_d_v8 — Study D Controller (Teensy 4.1)
//
// Ported from v2 firmware (stateless executor):
// - 2 motors (left/right) with up/down/stop
// - 8 proximity sensors (input-only)
// - DMX fog machine via TeensyDMX (3 channels)
//
// v8 behavior:
// - Option 2 device identity: one v8 `device_id` per logical device
// - One MQTT connection per `device_id` (correct LWT OFFLINE semantics)
// - Commands are `action="SET"` and require `parameters.op` (string)

#include <Arduino.h>
#include <ArduinoJson.h>

#include <TeensyDMX.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC key, hex encoded (64 chars). Replace per device during provisioning.
static const char *HMAC_KEY_PLACEHOLDER = "0000000000000000000000000000000000000000000000000000000000000000";

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

// --- Pins (ported from v2 firmware) ---
static const int PIN_POWER_LED = 13;

// Motor left (dual step/dir pins)
static const int PIN_MOTOR_LEFT_STEP_1 = 15;
static const int PIN_MOTOR_LEFT_STEP_2 = 16;
static const int PIN_MOTOR_LEFT_DIR_1 = 17;
static const int PIN_MOTOR_LEFT_DIR_2 = 18;

// Motor right (dual step/dir pins)
static const int PIN_MOTOR_RIGHT_STEP_1 = 20;
static const int PIN_MOTOR_RIGHT_STEP_2 = 21;
static const int PIN_MOTOR_RIGHT_DIR_1 = 22;
static const int PIN_MOTOR_RIGHT_DIR_2 = 23;

static const int PIN_MOTORS_ENABLE = 14;
static const int PIN_MOTORS_POWER = 1;

// Proximity sensors (8 total)
static const int PIN_LEFT_TOP_1 = 39;
static const int PIN_LEFT_TOP_2 = 40;
static const int PIN_LEFT_BOTTOM_1 = 38;
static const int PIN_LEFT_BOTTOM_2 = 41;
static const int PIN_RIGHT_TOP_1 = 35;
static const int PIN_RIGHT_TOP_2 = 36;
static const int PIN_RIGHT_BOTTOM_1 = 34;
static const int PIN_RIGHT_BOTTOM_2 = 37;

// DMX fog machine (Serial7)
static const int PIN_DMX_TX = 29;
static const int PIN_DMX_RX = 28;
static const int PIN_DMX_ENABLE = 30;
static const int FOG_DMX_CHANNEL_VOLUME = 1;
static const int FOG_DMX_CHANNEL_TIMER = 2;
static const int FOG_DMX_CHANNEL_FAN_SPEED = 3;

// --- Motor stepping (ported from v2) ---
enum MotorDirection : uint8_t { MOTOR_STOPPED = 0, MOTOR_UP = 1, MOTOR_DOWN = 2 };

struct Motor {
  int step_pin_1;
  int step_pin_2;
  int dir_pin_1;
  int dir_pin_2;
  MotorDirection direction;
  unsigned long last_step_time;
  unsigned long step_interval;  // microseconds
};

static Motor motor_left = {PIN_MOTOR_LEFT_STEP_1, PIN_MOTOR_LEFT_STEP_2, PIN_MOTOR_LEFT_DIR_1, PIN_MOTOR_LEFT_DIR_2, MOTOR_STOPPED, 0, 1000};
static Motor motor_right = {PIN_MOTOR_RIGHT_STEP_1, PIN_MOTOR_RIGHT_STEP_2, PIN_MOTOR_RIGHT_DIR_1, PIN_MOTOR_RIGHT_DIR_2, MOTOR_STOPPED, 0, 1000};

static void step_motor(Motor &motor) {
  if (motor.direction == MOTOR_STOPPED) return;
  unsigned long now = micros();
  if (now - motor.last_step_time >= motor.step_interval) {
    digitalWrite(motor.step_pin_1, HIGH);
    digitalWrite(motor.step_pin_2, LOW);
    delayMicroseconds(10);
    digitalWrite(motor.step_pin_1, LOW);
    digitalWrite(motor.step_pin_2, HIGH);
    motor.last_step_time = now;
  }
}

static void update_motors() {
  step_motor(motor_left);
  step_motor(motor_right);
}

static bool any_motor_running() { return motor_left.direction != MOTOR_STOPPED || motor_right.direction != MOTOR_STOPPED; }

static void apply_motor_power_policy() {
  if (any_motor_running()) {
    digitalWrite(PIN_MOTORS_POWER, HIGH);
    digitalWrite(PIN_MOTORS_ENABLE, LOW);
  } else {
    digitalWrite(PIN_MOTORS_ENABLE, HIGH);
    digitalWrite(PIN_MOTORS_POWER, LOW);
  }
}

// --- Proximity sensors ---
struct ProximitySensors {
  int left_top_1;
  int left_top_2;
  int left_bottom_1;
  int left_bottom_2;
  int right_top_1;
  int right_top_2;
  int right_bottom_1;
  int right_bottom_2;
};

static ProximitySensors sensor_last = {-1, -1, -1, -1, -1, -1, -1, -1};

static ProximitySensors read_sensors() {
  ProximitySensors s;
  s.left_top_1 = digitalRead(PIN_LEFT_TOP_1);
  s.left_top_2 = digitalRead(PIN_LEFT_TOP_2);
  s.left_bottom_1 = digitalRead(PIN_LEFT_BOTTOM_1);
  s.left_bottom_2 = digitalRead(PIN_LEFT_BOTTOM_2);
  s.right_top_1 = digitalRead(PIN_RIGHT_TOP_1);
  s.right_top_2 = digitalRead(PIN_RIGHT_TOP_2);
  s.right_bottom_1 = digitalRead(PIN_RIGHT_BOTTOM_1);
  s.right_bottom_2 = digitalRead(PIN_RIGHT_BOTTOM_2);
  return s;
}

static bool sensors_equal(const ProximitySensors &a, const ProximitySensors &b) {
  return a.left_top_1 == b.left_top_1 && a.left_top_2 == b.left_top_2 && a.left_bottom_1 == b.left_bottom_1 &&
         a.left_bottom_2 == b.left_bottom_2 && a.right_top_1 == b.right_top_1 && a.right_top_2 == b.right_top_2 &&
         a.right_bottom_1 == b.right_bottom_1 && a.right_bottom_2 == b.right_bottom_2;
}

// --- DMX ---
static TeensyDMX dmxTx(Serial7, PIN_DMX_ENABLE);
static uint8_t fog_volume = 0;
static uint8_t fog_timer = 0;
static uint8_t fog_fan_speed = 0;

static void apply_dmx() {
  dmxTx.set(FOG_DMX_CHANNEL_VOLUME, fog_volume);
  dmxTx.set(FOG_DMX_CHANNEL_TIMER, fog_timer);
  dmxTx.set(FOG_DMX_CHANNEL_FAN_SPEED, fog_fan_speed);
}

// --- v8 Clients ---
static sentient_v8::Config cfg_motor_left = make_cfg("study_d_motor_left", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_motor_right = make_cfg("study_d_motor_right", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_prox = make_cfg("study_d_proximity_sensors", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_fog = make_cfg("study_d_fog_dmx", HMAC_KEY_PLACEHOLDER);

static sentient_v8::Client sne_motor_left(cfg_motor_left);
static sentient_v8::Client sne_motor_right(cfg_motor_right);
static sentient_v8::Client sne_prox(cfg_prox);
static sentient_v8::Client sne_fog(cfg_fog);

static sentient_v8::Client *clients[] = {&sne_motor_left, &sne_motor_right, &sne_prox, &sne_fog};

enum class DeviceKind : uint8_t { MotorLeft, MotorRight, Proximity, FogDmx };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_left = {DeviceKind::MotorLeft};
static DeviceCtx ctx_right = {DeviceKind::MotorRight};
static DeviceCtx ctx_prox = {DeviceKind::Proximity};
static DeviceCtx ctx_fog = {DeviceKind::FogDmx};

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

static const unsigned long STATE_REFRESH_MS = 60UL * 1000UL;
static unsigned long last_periodic_publish = 0;

static const char *dir_str(MotorDirection d) {
  switch (d) {
    case MOTOR_STOPPED: return "stopped";
    case MOTOR_UP: return "up";
    case MOTOR_DOWN: return "down";
  }
  return "unknown";
}

static void publish_motor_state(sentient_v8::Client &client, const Motor &m, const char *label, const char *reason) {
  StaticJsonDocument<320> st;
  st["firmware_version"] = "study_d_v8";
  st["reason"] = reason ? reason : "";
  st["motor"] = label;
  st["direction"] = dir_str(m.direction);
  st["step_interval_us"] = m.step_interval;
  st["enabled"] = (digitalRead(PIN_MOTORS_ENABLE) == LOW);
  st["motors_power"] = (digitalRead(PIN_MOTORS_POWER) == HIGH);
  client.publishState(st);
}

static void publish_prox_state(const ProximitySensors &s, const char *reason) {
  StaticJsonDocument<384> st;
  st["firmware_version"] = "study_d_v8";
  st["reason"] = reason ? reason : "";
  st["left_top_1"] = s.left_top_1;
  st["left_top_2"] = s.left_top_2;
  st["left_bottom_1"] = s.left_bottom_1;
  st["left_bottom_2"] = s.left_bottom_2;
  st["right_top_1"] = s.right_top_1;
  st["right_top_2"] = s.right_top_2;
  st["right_bottom_1"] = s.right_bottom_1;
  st["right_bottom_2"] = s.right_bottom_2;
  sne_prox.publishState(st);
}

static void publish_fog_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "study_d_v8";
  st["reason"] = reason ? reason : "";
  st["volume"] = fog_volume;
  st["timer"] = fog_timer;
  st["fan_speed"] = fog_fan_speed;
  sne_fog.publishState(st);
}

static void publish_all_state(const char *reason) {
  publish_motor_state(sne_motor_left, motor_left, "left", reason);
  publish_motor_state(sne_motor_right, motor_right, "right", reason);
  publish_prox_state(read_sensors(), reason);
  publish_fog_state(reason);
}

static void set_motor_direction(Motor &m, MotorDirection dir) {
  m.direction = dir;
  if (dir == MOTOR_UP) {
    digitalWrite(m.dir_pin_1, HIGH);
    digitalWrite(m.dir_pin_2, LOW);
  } else if (dir == MOTOR_DOWN) {
    digitalWrite(m.dir_pin_1, LOW);
    digitalWrite(m.dir_pin_2, HIGH);
  }
  apply_motor_power_policy();
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
    publish_all_state("request_status");
    sensor_last = read_sensors();
    return true;
  }
  if (strcmp(op, "set_power_led") == 0) {
    if (!p.containsKey("on")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    digitalWrite(PIN_POWER_LED, (p["on"] | false) ? HIGH : LOW);
    return true;
  }
  if (strcmp(op, "noop") == 0) return true;

  if (ctx->kind == DeviceKind::MotorLeft) {
    if (strcmp(op, "up") == 0) {
      set_motor_direction(motor_left, MOTOR_UP);
      publish_motor_state(sne_motor_left, motor_left, "left", op);
      return true;
    }
    if (strcmp(op, "down") == 0) {
      set_motor_direction(motor_left, MOTOR_DOWN);
      publish_motor_state(sne_motor_left, motor_left, "left", op);
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      set_motor_direction(motor_left, MOTOR_STOPPED);
      publish_motor_state(sne_motor_left, motor_left, "left", op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::MotorRight) {
    if (strcmp(op, "up") == 0) {
      set_motor_direction(motor_right, MOTOR_UP);
      publish_motor_state(sne_motor_right, motor_right, "right", op);
      return true;
    }
    if (strcmp(op, "down") == 0) {
      set_motor_direction(motor_right, MOTOR_DOWN);
      publish_motor_state(sne_motor_right, motor_right, "right", op);
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      set_motor_direction(motor_right, MOTOR_STOPPED);
      publish_motor_state(sne_motor_right, motor_right, "right", op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::FogDmx) {
    if (!p.containsKey("value")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    int value = p["value"] | 0;
    value = constrain(value, 0, 255);

    if (strcmp(op, "set_volume") == 0) {
      fog_volume = (uint8_t)value;
      apply_dmx();
      publish_fog_state(op);
      return true;
    }
    if (strcmp(op, "set_timer") == 0) {
      fog_timer = (uint8_t)value;
      apply_dmx();
      publish_fog_state(op);
      return true;
    }
    if (strcmp(op, "set_fan_speed") == 0) {
      fog_fan_speed = (uint8_t)value;
      apply_dmx();
      publish_fog_state(op);
      return true;
    }

    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Proximity device is input-only.
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  // Motors
  pinMode(PIN_MOTOR_LEFT_STEP_1, OUTPUT);
  pinMode(PIN_MOTOR_LEFT_STEP_2, OUTPUT);
  pinMode(PIN_MOTOR_LEFT_DIR_1, OUTPUT);
  pinMode(PIN_MOTOR_LEFT_DIR_2, OUTPUT);

  pinMode(PIN_MOTOR_RIGHT_STEP_1, OUTPUT);
  pinMode(PIN_MOTOR_RIGHT_STEP_2, OUTPUT);
  pinMode(PIN_MOTOR_RIGHT_DIR_1, OUTPUT);
  pinMode(PIN_MOTOR_RIGHT_DIR_2, OUTPUT);

  pinMode(PIN_MOTORS_ENABLE, OUTPUT);
  pinMode(PIN_MOTORS_POWER, OUTPUT);
  motor_left.direction = MOTOR_STOPPED;
  motor_right.direction = MOTOR_STOPPED;
  apply_motor_power_policy();

  // Proximity inputs
  pinMode(PIN_LEFT_TOP_1, INPUT);
  pinMode(PIN_LEFT_TOP_2, INPUT);
  pinMode(PIN_LEFT_BOTTOM_1, INPUT);
  pinMode(PIN_LEFT_BOTTOM_2, INPUT);
  pinMode(PIN_RIGHT_TOP_1, INPUT);
  pinMode(PIN_RIGHT_TOP_2, INPUT);
  pinMode(PIN_RIGHT_BOTTOM_1, INPUT);
  pinMode(PIN_RIGHT_BOTTOM_2, INPUT);
  sensor_last = read_sensors();

  // DMX
  pinMode(PIN_DMX_TX, OUTPUT);
  pinMode(PIN_DMX_RX, INPUT);
  pinMode(PIN_DMX_ENABLE, OUTPUT);
  digitalWrite(PIN_DMX_ENABLE, HIGH);
  Serial7.begin(250000);
  dmxTx.begin();
  dmxTx.setPacketSize(3);
  apply_dmx();

  if (!sne_motor_left.begin()) while (true) delay(1000);
  if (!sne_motor_right.begin()) while (true) delay(1000);
  if (!sne_prox.begin()) while (true) delay(1000);
  if (!sne_fog.begin()) while (true) delay(1000);

  sne_motor_left.setCommandHandler(handleCommand, &ctx_left);
  sne_motor_right.setCommandHandler(handleCommand, &ctx_right);
  sne_prox.setCommandHandler(handleCommand, &ctx_prox);
  sne_fog.setCommandHandler(handleCommand, &ctx_fog);

  publish_all_state("boot");
  last_periodic_publish = millis();
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  update_motors();

  ProximitySensors cur = read_sensors();
  const unsigned long now = millis();
  bool force = (now - last_periodic_publish) >= STATE_REFRESH_MS;
  if (force || !sensors_equal(cur, sensor_last)) {
    publish_prox_state(cur, force ? "periodic" : "change");
    sensor_last = cur;
  }

  if (force) {
    publish_motor_state(sne_motor_left, motor_left, "left", "periodic");
    publish_motor_state(sne_motor_right, motor_right, "right", "periodic");
    publish_fog_state("periodic");
    last_periodic_publish = now;
  }
}
