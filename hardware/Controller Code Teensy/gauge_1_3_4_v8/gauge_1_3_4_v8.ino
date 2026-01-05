// gauge_1_3_4_v8 — Gauge 1/3/4 Controller (Teensy 4.1)
//
// v8 behavior:
// - Option 2 device identity: one v8 `device_id` per logical gauge
// - One MQTT connection per `device_id` (correct LWT OFFLINE semantics)
// - Commands are `action="SET"` and require `parameters.op` (string)
//
// Hardware behavior (ported from v2):
// - When ACTIVE: all gauges track their valve potentiometer PSI
// - When INACTIVE: all gauges move to zero
// - EEPROM stores last known stepper positions

#include <Arduino.h>
#include <ArduinoJson.h>

#include <AccelStepper.h>
#include <EEPROM.h>

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

// Gauge 1
static const int PIN_GAUGE_1_DIR = 7;
static const int PIN_GAUGE_1_STEP = 6;
static const int PIN_GAUGE_1_ENABLE = 8;
static const int PIN_VALVE_1_POT = A10;

// Gauge 3
static const int PIN_GAUGE_3_DIR = 11;
static const int PIN_GAUGE_3_STEP = 10;
static const int PIN_GAUGE_3_ENABLE = 12;
static const int PIN_VALVE_3_POT = A12;

// Gauge 4
static const int PIN_GAUGE_4_DIR = 3;
static const int PIN_GAUGE_4_STEP = 2;
static const int PIN_GAUGE_4_ENABLE = 4;
static const int PIN_VALVE_4_POT = A11;

// --- Gauge configuration (ported from v2) ---
static const int GAUGE_MIN_STEPS = 0;
static const int GAUGE_MAX_STEPS = 2300;
static const int PSI_MIN = 0;
static const int PSI_MAX = 125;

static const int STEPPER_MAX_SPEED = 700;
static const int STEPPER_ACCEL = 350;

static const int VALVE_1_ZERO = 10;
static const int VALVE_1_MAX = 750;
static const int VALVE_3_ZERO = 15;
static const int VALVE_3_MAX = 896;
static const int VALVE_4_ZERO = 10;
static const int VALVE_4_MAX = 960;

static const int NUM_ANALOG_READINGS = 3;
static const float FILTER_ALPHA = 0.25f;
static const int PSI_DEADBAND = 1;
static const unsigned long MOVEMENT_DELAY_MS = 75;

static const unsigned long SENSOR_REFRESH_MS = 60UL * 1000UL;

// EEPROM addresses
static const int EEPROM_ADDR_GAUGE1 = 0;
static const int EEPROM_ADDR_GAUGE3 = 4;
static const int EEPROM_ADDR_GAUGE4 = 8;

static AccelStepper stepper_1(AccelStepper::DRIVER, PIN_GAUGE_1_STEP, PIN_GAUGE_1_DIR);
static AccelStepper stepper_3(AccelStepper::DRIVER, PIN_GAUGE_3_STEP, PIN_GAUGE_3_DIR);
static AccelStepper stepper_4(AccelStepper::DRIVER, PIN_GAUGE_4_STEP, PIN_GAUGE_4_DIR);

static bool gauges_active = false;

static int valve_1_psi = 0;
static int valve_3_psi = 0;
static int valve_4_psi = 0;
static int gauge_1_psi = 0;
static int gauge_3_psi = 0;
static int gauge_4_psi = 0;

static float filtered_analog_1 = 0.0f;
static float filtered_analog_3 = 0.0f;
static float filtered_analog_4 = 0.0f;
static bool filters_initialized = false;

static int previous_target_psi_1 = -1;
static int previous_target_psi_3 = -1;
static int previous_target_psi_4 = -1;
static unsigned long last_move_time_1 = 0;
static unsigned long last_move_time_3 = 0;
static unsigned long last_move_time_4 = 0;

static int last_published_valve_1_psi = -1;
static int last_published_valve_3_psi = -1;
static int last_published_valve_4_psi = -1;

// --- v8 Clients (one per gauge) ---
static sentient_v8::Config cfg_gauge_1 = make_cfg("gauge_1_3_4_gauge_1", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_gauge_3 = make_cfg("gauge_1_3_4_gauge_3", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_gauge_4 = make_cfg("gauge_1_3_4_gauge_4", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Client sne_gauge_1(cfg_gauge_1);
static sentient_v8::Client sne_gauge_3(cfg_gauge_3);
static sentient_v8::Client sne_gauge_4(cfg_gauge_4);

static sentient_v8::Client *clients[] = {&sne_gauge_1, &sne_gauge_3, &sne_gauge_4};

enum class GaugeNum : uint8_t { G1 = 1, G3 = 3, G4 = 4 };
struct DeviceCtx {
  GaugeNum gauge;
};
static DeviceCtx ctx_g1 = {GaugeNum::G1};
static DeviceCtx ctx_g3 = {GaugeNum::G3};
static DeviceCtx ctx_g4 = {GaugeNum::G4};

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

static void save_gauge_positions() {
  int p1 = stepper_1.currentPosition();
  int p3 = stepper_3.currentPosition();
  int p4 = stepper_4.currentPosition();
  EEPROM.put(EEPROM_ADDR_GAUGE1, p1);
  EEPROM.put(EEPROM_ADDR_GAUGE3, p3);
  EEPROM.put(EEPROM_ADDR_GAUGE4, p4);
}

static void load_gauge_positions() {
  int p1 = 0, p3 = 0, p4 = 0;
  EEPROM.get(EEPROM_ADDR_GAUGE1, p1);
  EEPROM.get(EEPROM_ADDR_GAUGE3, p3);
  EEPROM.get(EEPROM_ADDR_GAUGE4, p4);
  if (p1 < GAUGE_MIN_STEPS || p1 > GAUGE_MAX_STEPS) p1 = 0;
  if (p3 < GAUGE_MIN_STEPS || p3 > GAUGE_MAX_STEPS) p3 = 0;
  if (p4 < GAUGE_MIN_STEPS || p4 > GAUGE_MAX_STEPS) p4 = 0;
  stepper_1.setCurrentPosition(p1);
  stepper_3.setCurrentPosition(p3);
  stepper_4.setCurrentPosition(p4);
}

static void read_valve_positions() {
  int sum1 = 0, sum3 = 0, sum4 = 0;
  for (int i = 0; i < NUM_ANALOG_READINGS; i++) {
    sum1 += analogRead(PIN_VALVE_1_POT);
    sum3 += analogRead(PIN_VALVE_3_POT);
    sum4 += analogRead(PIN_VALVE_4_POT);
  }

  float raw1 = sum1 / (float)NUM_ANALOG_READINGS;
  float raw3 = sum3 / (float)NUM_ANALOG_READINGS;
  float raw4 = sum4 / (float)NUM_ANALOG_READINGS;

  if (!filters_initialized) {
    filtered_analog_1 = raw1;
    filtered_analog_3 = raw3;
    filtered_analog_4 = raw4;
    filters_initialized = true;
  }

  filtered_analog_1 = (FILTER_ALPHA * raw1) + ((1.0f - FILTER_ALPHA) * filtered_analog_1);
  filtered_analog_3 = (FILTER_ALPHA * raw3) + ((1.0f - FILTER_ALPHA) * filtered_analog_3);
  filtered_analog_4 = (FILTER_ALPHA * raw4) + ((1.0f - FILTER_ALPHA) * filtered_analog_4);

  valve_1_psi = map((int)filtered_analog_1, VALVE_1_ZERO, VALVE_1_MAX, PSI_MIN, PSI_MAX);
  valve_3_psi = map((int)filtered_analog_3, VALVE_3_ZERO, VALVE_3_MAX, PSI_MIN, PSI_MAX);
  valve_4_psi = map((int)filtered_analog_4, VALVE_4_ZERO, VALVE_4_MAX, PSI_MIN, PSI_MAX);

  valve_1_psi = constrain(valve_1_psi, PSI_MIN, PSI_MAX);
  valve_3_psi = constrain(valve_3_psi, PSI_MIN, PSI_MAX);
  valve_4_psi = constrain(valve_4_psi, PSI_MIN, PSI_MAX);
}

static void move_gauges() {
  if (!gauges_active) return;
  const unsigned long now = millis();

  if ((abs(valve_1_psi - previous_target_psi_1) >= PSI_DEADBAND || previous_target_psi_1 == -1) &&
      (now - last_move_time_1 >= MOVEMENT_DELAY_MS || previous_target_psi_1 == -1)) {
    stepper_1.moveTo(map(valve_1_psi, PSI_MIN, PSI_MAX, GAUGE_MIN_STEPS, GAUGE_MAX_STEPS));
    previous_target_psi_1 = valve_1_psi;
    last_move_time_1 = now;
  }
  if ((abs(valve_3_psi - previous_target_psi_3) >= PSI_DEADBAND || previous_target_psi_3 == -1) &&
      (now - last_move_time_3 >= MOVEMENT_DELAY_MS || previous_target_psi_3 == -1)) {
    stepper_3.moveTo(map(valve_3_psi, PSI_MIN, PSI_MAX, GAUGE_MIN_STEPS, GAUGE_MAX_STEPS));
    previous_target_psi_3 = valve_3_psi;
    last_move_time_3 = now;
  }
  if ((abs(valve_4_psi - previous_target_psi_4) >= PSI_DEADBAND || previous_target_psi_4 == -1) &&
      (now - last_move_time_4 >= MOVEMENT_DELAY_MS || previous_target_psi_4 == -1)) {
    stepper_4.moveTo(map(valve_4_psi, PSI_MIN, PSI_MAX, GAUGE_MIN_STEPS, GAUGE_MAX_STEPS));
    previous_target_psi_4 = valve_4_psi;
    last_move_time_4 = now;
  }

  gauge_1_psi = map(stepper_1.currentPosition(), GAUGE_MIN_STEPS, GAUGE_MAX_STEPS, PSI_MIN, PSI_MAX);
  gauge_3_psi = map(stepper_3.currentPosition(), GAUGE_MIN_STEPS, GAUGE_MAX_STEPS, PSI_MIN, PSI_MAX);
  gauge_4_psi = map(stepper_4.currentPosition(), GAUGE_MIN_STEPS, GAUGE_MAX_STEPS, PSI_MIN, PSI_MAX);
}

static void publish_gauge_state(sentient_v8::Client &client, int gauge_num, const char *reason) {
  StaticJsonDocument<512> st;
  st["firmware_version"] = "gauge_1_3_4_v8";
  st["gauges_active"] = gauges_active;
  st["gauge"] = gauge_num;
  st["reason"] = reason ? reason : "";

  if (gauge_num == 1) {
    st["valve_psi"] = valve_1_psi;
    st["gauge_psi"] = gauge_1_psi;
    st["stepper_pos"] = stepper_1.currentPosition();
    st["target_pos"] = stepper_1.targetPosition();
  } else if (gauge_num == 3) {
    st["valve_psi"] = valve_3_psi;
    st["gauge_psi"] = gauge_3_psi;
    st["stepper_pos"] = stepper_3.currentPosition();
    st["target_pos"] = stepper_3.targetPosition();
  } else if (gauge_num == 4) {
    st["valve_psi"] = valve_4_psi;
    st["gauge_psi"] = gauge_4_psi;
    st["stepper_pos"] = stepper_4.currentPosition();
    st["target_pos"] = stepper_4.targetPosition();
  }

  client.publishState(st);
}

static void publish_all_state(const char *reason) {
  publish_gauge_state(sne_gauge_1, 1, reason);
  publish_gauge_state(sne_gauge_3, 3, reason);
  publish_gauge_state(sne_gauge_4, 4, reason);
}

static void activate_all() {
  digitalWrite(PIN_GAUGE_1_ENABLE, LOW);
  digitalWrite(PIN_GAUGE_3_ENABLE, LOW);
  digitalWrite(PIN_GAUGE_4_ENABLE, LOW);
  gauges_active = true;
}

static void deactivate_all() {
  gauges_active = false;
  stepper_1.moveTo(GAUGE_MIN_STEPS);
  stepper_3.moveTo(GAUGE_MIN_STEPS);
  stepper_4.moveTo(GAUGE_MIN_STEPS);
  previous_target_psi_1 = -1;
  previous_target_psi_3 = -1;
  previous_target_psi_4 = -1;
  filters_initialized = false;
}

static int ctx_default_gauge_num(const DeviceCtx *ctx) {
  if (!ctx) return 0;
  return (int)ctx->gauge;
}

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void *vctx) {
  DeviceCtx *ctx = (DeviceCtx *)vctx;

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

  if (strcmp(op, "activate") == 0) {
    activate_all();
    publish_all_state("activate");
    return true;
  }
  if (strcmp(op, "deactivate") == 0) {
    deactivate_all();
    publish_all_state("deactivate");
    return true;
  }
  if (strcmp(op, "reset") == 0) {
    deactivate_all();
    publish_all_state("reset");
    return true;
  }
  if (strcmp(op, "adjust_zero") == 0) {
    if (!p.containsKey("steps")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    int gauge_num = p.containsKey("gauge") ? (int)(p["gauge"] | 0) : ctx_default_gauge_num(ctx);
    int steps = p["steps"] | 0;
    if (gauge_num == 1) stepper_1.move(steps);
    else if (gauge_num == 3) stepper_3.move(steps);
    else if (gauge_num == 4) stepper_4.move(steps);
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    publish_all_state("adjust_zero");
    return true;
  }
  if (strcmp(op, "set_current_as_zero") == 0) {
    int gauge_num = p.containsKey("gauge") ? (int)(p["gauge"] | 0) : ctx_default_gauge_num(ctx);
    if (gauge_num == 1) stepper_1.setCurrentPosition(GAUGE_MIN_STEPS);
    else if (gauge_num == 3) stepper_3.setCurrentPosition(GAUGE_MIN_STEPS);
    else if (gauge_num == 4) stepper_4.setCurrentPosition(GAUGE_MIN_STEPS);
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    save_gauge_positions();
    publish_all_state("set_current_as_zero");
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

  pinMode(PIN_GAUGE_1_ENABLE, OUTPUT);
  pinMode(PIN_GAUGE_3_ENABLE, OUTPUT);
  pinMode(PIN_GAUGE_4_ENABLE, OUTPUT);
  digitalWrite(PIN_GAUGE_1_ENABLE, HIGH);
  digitalWrite(PIN_GAUGE_3_ENABLE, HIGH);
  digitalWrite(PIN_GAUGE_4_ENABLE, HIGH);

  stepper_1.setPinsInverted(true, false, false);
  stepper_3.setPinsInverted(true, false, false);
  stepper_4.setPinsInverted(false, false, false);

  stepper_1.setMaxSpeed(STEPPER_MAX_SPEED);
  stepper_1.setAcceleration(STEPPER_ACCEL);
  stepper_3.setMaxSpeed(STEPPER_MAX_SPEED);
  stepper_3.setAcceleration(STEPPER_ACCEL);
  stepper_4.setMaxSpeed(STEPPER_MAX_SPEED);
  stepper_4.setAcceleration(STEPPER_ACCEL);

  pinMode(PIN_VALVE_1_POT, INPUT);
  pinMode(PIN_VALVE_3_POT, INPUT);
  pinMode(PIN_VALVE_4_POT, INPUT);

  load_gauge_positions();

  // Auto-zero on boot (matches v2 intent): enable briefly, move to 0, then remain disabled until activate.
  activate_all();
  stepper_1.moveTo(GAUGE_MIN_STEPS);
  stepper_3.moveTo(GAUGE_MIN_STEPS);
  stepper_4.moveTo(GAUGE_MIN_STEPS);
  while (stepper_1.distanceToGo() != 0 || stepper_3.distanceToGo() != 0 || stepper_4.distanceToGo() != 0) {
    stepper_1.run();
    stepper_3.run();
    stepper_4.run();
  }
  gauges_active = false;
  digitalWrite(PIN_GAUGE_1_ENABLE, HIGH);
  digitalWrite(PIN_GAUGE_3_ENABLE, HIGH);
  digitalWrite(PIN_GAUGE_4_ENABLE, HIGH);
  save_gauge_positions();

  if (!sne_gauge_1.begin()) while (true) delay(1000);
  if (!sne_gauge_3.begin()) while (true) delay(1000);
  if (!sne_gauge_4.begin()) while (true) delay(1000);

  sne_gauge_1.setCommandHandler(handleCommand, &ctx_g1);
  sne_gauge_3.setCommandHandler(handleCommand, &ctx_g3);
  sne_gauge_4.setCommandHandler(handleCommand, &ctx_g4);

  read_valve_positions();
  move_gauges();
  publish_all_state("boot");
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  stepper_1.run();
  stepper_3.run();
  stepper_4.run();

  read_valve_positions();
  move_gauges();

  const unsigned long now = millis();
  static unsigned long last_publish = 0;
  bool force_publish = (now - last_publish) >= SENSOR_REFRESH_MS;

  bool any_change = force_publish;
  if (valve_1_psi != last_published_valve_1_psi) any_change = true;
  if (valve_3_psi != last_published_valve_3_psi) any_change = true;
  if (valve_4_psi != last_published_valve_4_psi) any_change = true;

  if (any_change) {
    publish_all_state(force_publish ? "periodic" : "change");
    save_gauge_positions();
    last_publish = now;
    last_published_valve_1_psi = valve_1_psi;
    last_published_valve_3_psi = valve_3_psi;
    last_published_valve_4_psi = valve_4_psi;
  }
}
