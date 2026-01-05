// gauge_2_5_7_v8 — Gauge 2/5/7 Controller (Teensy 4.1)
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

// Gauge 2
static const int PIN_GAUGE_2_DIR = 3;
static const int PIN_GAUGE_2_STEP = 2;
static const int PIN_GAUGE_2_ENABLE = 4;
static const int PIN_VALVE_2_POT = A11;

// Gauge 5
static const int PIN_GAUGE_5_DIR = 11;
static const int PIN_GAUGE_5_STEP = 10;
static const int PIN_GAUGE_5_ENABLE = 12;
static const int PIN_VALVE_5_POT = A12;

// Gauge 7
static const int PIN_GAUGE_7_DIR = 7;
static const int PIN_GAUGE_7_STEP = 6;
static const int PIN_GAUGE_7_ENABLE = 8;
static const int PIN_VALVE_7_POT = A10;

// --- Gauge configuration (ported from v2) ---
static const int GAUGE_MIN_STEPS = 0;
static const int GAUGE_MAX_STEPS = 2300;
static const int PSI_MIN = 0;
static const int PSI_MAX = 125;

static const int STEPPER_MAX_SPEED = 700;
static const int STEPPER_ACCEL = 350;

static const int VALVE_2_ZERO = 9;
static const int VALVE_2_MAX = 600;
static const int VALVE_5_ZERO = 35;
static const int VALVE_5_MAX = 730;
static const int VALVE_7_ZERO = 5;
static const int VALVE_7_MAX = 932;

static const int NUM_ANALOG_READINGS = 3;
static const float FILTER_ALPHA = 0.25f;
static const int PSI_DEADBAND = 1;
static const unsigned long MOVEMENT_DELAY_MS = 75;

static const unsigned long SENSOR_REFRESH_MS = 60UL * 1000UL;

// EEPROM addresses
static const int EEPROM_ADDR_GAUGE2 = 0;
static const int EEPROM_ADDR_GAUGE5 = 4;
static const int EEPROM_ADDR_GAUGE7 = 8;

static AccelStepper stepper_2(AccelStepper::DRIVER, PIN_GAUGE_2_STEP, PIN_GAUGE_2_DIR);
static AccelStepper stepper_5(AccelStepper::DRIVER, PIN_GAUGE_5_STEP, PIN_GAUGE_5_DIR);
static AccelStepper stepper_7(AccelStepper::DRIVER, PIN_GAUGE_7_STEP, PIN_GAUGE_7_DIR);

static bool gauges_active = false;

static int valve_2_psi = 0;
static int valve_5_psi = 0;
static int valve_7_psi = 0;
static int gauge_2_psi = 0;
static int gauge_5_psi = 0;
static int gauge_7_psi = 0;

static float filtered_analog_2 = 0.0f;
static float filtered_analog_5 = 0.0f;
static float filtered_analog_7 = 0.0f;
static bool filters_initialized = false;

static int previous_target_psi_2 = -1;
static int previous_target_psi_5 = -1;
static int previous_target_psi_7 = -1;
static unsigned long last_move_time_2 = 0;
static unsigned long last_move_time_5 = 0;
static unsigned long last_move_time_7 = 0;

static int last_published_valve_2_psi = -1;
static int last_published_valve_5_psi = -1;
static int last_published_valve_7_psi = -1;

// --- v8 Clients (one per gauge) ---
static sentient_v8::Config cfg_gauge_2 = make_cfg("gauge_2_5_7_gauge_2", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_gauge_5 = make_cfg("gauge_2_5_7_gauge_5", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_gauge_7 = make_cfg("gauge_2_5_7_gauge_7", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Client sne_gauge_2(cfg_gauge_2);
static sentient_v8::Client sne_gauge_5(cfg_gauge_5);
static sentient_v8::Client sne_gauge_7(cfg_gauge_7);

static sentient_v8::Client *clients[] = {&sne_gauge_2, &sne_gauge_5, &sne_gauge_7};

enum class GaugeNum : uint8_t { G2 = 2, G5 = 5, G7 = 7 };
struct DeviceCtx {
  GaugeNum gauge;
};
static DeviceCtx ctx_g2 = {GaugeNum::G2};
static DeviceCtx ctx_g5 = {GaugeNum::G5};
static DeviceCtx ctx_g7 = {GaugeNum::G7};

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
  int p2 = stepper_2.currentPosition();
  int p5 = stepper_5.currentPosition();
  int p7 = stepper_7.currentPosition();
  EEPROM.put(EEPROM_ADDR_GAUGE2, p2);
  EEPROM.put(EEPROM_ADDR_GAUGE5, p5);
  EEPROM.put(EEPROM_ADDR_GAUGE7, p7);
}

static void load_gauge_positions() {
  int p2 = 0, p5 = 0, p7 = 0;
  EEPROM.get(EEPROM_ADDR_GAUGE2, p2);
  EEPROM.get(EEPROM_ADDR_GAUGE5, p5);
  EEPROM.get(EEPROM_ADDR_GAUGE7, p7);
  if (p2 < GAUGE_MIN_STEPS || p2 > GAUGE_MAX_STEPS) p2 = 0;
  if (p5 < GAUGE_MIN_STEPS || p5 > GAUGE_MAX_STEPS) p5 = 0;
  if (p7 < GAUGE_MIN_STEPS || p7 > GAUGE_MAX_STEPS) p7 = 0;
  stepper_2.setCurrentPosition(p2);
  stepper_5.setCurrentPosition(p5);
  stepper_7.setCurrentPosition(p7);
}

static void read_valve_positions() {
  int sum2 = 0, sum5 = 0, sum7 = 0;
  for (int i = 0; i < NUM_ANALOG_READINGS; i++) {
    sum2 += analogRead(PIN_VALVE_2_POT);
    sum5 += analogRead(PIN_VALVE_5_POT);
    sum7 += analogRead(PIN_VALVE_7_POT);
  }

  float raw2 = sum2 / (float)NUM_ANALOG_READINGS;
  float raw5 = sum5 / (float)NUM_ANALOG_READINGS;
  float raw7 = sum7 / (float)NUM_ANALOG_READINGS;

  if (!filters_initialized) {
    filtered_analog_2 = raw2;
    filtered_analog_5 = raw5;
    filtered_analog_7 = raw7;
    filters_initialized = true;
  }

  filtered_analog_2 = (FILTER_ALPHA * raw2) + ((1.0f - FILTER_ALPHA) * filtered_analog_2);
  filtered_analog_5 = (FILTER_ALPHA * raw5) + ((1.0f - FILTER_ALPHA) * filtered_analog_5);
  filtered_analog_7 = (FILTER_ALPHA * raw7) + ((1.0f - FILTER_ALPHA) * filtered_analog_7);

  valve_2_psi = map((int)filtered_analog_2, VALVE_2_ZERO, VALVE_2_MAX, PSI_MIN, PSI_MAX);
  valve_5_psi = map((int)filtered_analog_5, VALVE_5_ZERO, VALVE_5_MAX, PSI_MIN, PSI_MAX);
  valve_7_psi = map((int)filtered_analog_7, VALVE_7_ZERO, VALVE_7_MAX, PSI_MIN, PSI_MAX);

  valve_2_psi = constrain(valve_2_psi, PSI_MIN, PSI_MAX);
  valve_5_psi = constrain(valve_5_psi, PSI_MIN, PSI_MAX);
  valve_7_psi = constrain(valve_7_psi, PSI_MIN, PSI_MAX);
}

static void move_gauges() {
  if (!gauges_active) return;
  const unsigned long now = millis();

  if ((abs(valve_2_psi - previous_target_psi_2) >= PSI_DEADBAND || previous_target_psi_2 == -1) &&
      (now - last_move_time_2 >= MOVEMENT_DELAY_MS || previous_target_psi_2 == -1)) {
    stepper_2.moveTo(map(valve_2_psi, PSI_MIN, PSI_MAX, GAUGE_MIN_STEPS, GAUGE_MAX_STEPS));
    previous_target_psi_2 = valve_2_psi;
    last_move_time_2 = now;
  }
  if ((abs(valve_5_psi - previous_target_psi_5) >= PSI_DEADBAND || previous_target_psi_5 == -1) &&
      (now - last_move_time_5 >= MOVEMENT_DELAY_MS || previous_target_psi_5 == -1)) {
    stepper_5.moveTo(map(valve_5_psi, PSI_MIN, PSI_MAX, GAUGE_MIN_STEPS, GAUGE_MAX_STEPS));
    previous_target_psi_5 = valve_5_psi;
    last_move_time_5 = now;
  }
  if ((abs(valve_7_psi - previous_target_psi_7) >= PSI_DEADBAND || previous_target_psi_7 == -1) &&
      (now - last_move_time_7 >= MOVEMENT_DELAY_MS || previous_target_psi_7 == -1)) {
    stepper_7.moveTo(map(valve_7_psi, PSI_MIN, PSI_MAX, GAUGE_MIN_STEPS, GAUGE_MAX_STEPS));
    previous_target_psi_7 = valve_7_psi;
    last_move_time_7 = now;
  }

  gauge_2_psi = map(stepper_2.currentPosition(), GAUGE_MIN_STEPS, GAUGE_MAX_STEPS, PSI_MIN, PSI_MAX);
  gauge_5_psi = map(stepper_5.currentPosition(), GAUGE_MIN_STEPS, GAUGE_MAX_STEPS, PSI_MIN, PSI_MAX);
  gauge_7_psi = map(stepper_7.currentPosition(), GAUGE_MIN_STEPS, GAUGE_MAX_STEPS, PSI_MIN, PSI_MAX);
}

static void publish_gauge_state(sentient_v8::Client &client, int gauge_num, const char *reason) {
  StaticJsonDocument<512> st;
  st["firmware_version"] = "gauge_2_5_7_v8";
  st["gauges_active"] = gauges_active;
  st["gauge"] = gauge_num;
  st["reason"] = reason ? reason : "";

  if (gauge_num == 2) {
    st["valve_psi"] = valve_2_psi;
    st["gauge_psi"] = gauge_2_psi;
    st["stepper_pos"] = stepper_2.currentPosition();
    st["target_pos"] = stepper_2.targetPosition();
  } else if (gauge_num == 5) {
    st["valve_psi"] = valve_5_psi;
    st["gauge_psi"] = gauge_5_psi;
    st["stepper_pos"] = stepper_5.currentPosition();
    st["target_pos"] = stepper_5.targetPosition();
  } else if (gauge_num == 7) {
    st["valve_psi"] = valve_7_psi;
    st["gauge_psi"] = gauge_7_psi;
    st["stepper_pos"] = stepper_7.currentPosition();
    st["target_pos"] = stepper_7.targetPosition();
  }

  client.publishState(st);
}

static void publish_all_state(const char *reason) {
  publish_gauge_state(sne_gauge_2, 2, reason);
  publish_gauge_state(sne_gauge_5, 5, reason);
  publish_gauge_state(sne_gauge_7, 7, reason);
}

static void activate_all() {
  digitalWrite(PIN_GAUGE_2_ENABLE, LOW);
  digitalWrite(PIN_GAUGE_5_ENABLE, LOW);
  digitalWrite(PIN_GAUGE_7_ENABLE, LOW);
  gauges_active = true;
}

static void deactivate_all() {
  gauges_active = false;
  stepper_2.moveTo(GAUGE_MIN_STEPS);
  stepper_5.moveTo(GAUGE_MIN_STEPS);
  stepper_7.moveTo(GAUGE_MIN_STEPS);
  previous_target_psi_2 = -1;
  previous_target_psi_5 = -1;
  previous_target_psi_7 = -1;
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
    if (gauge_num == 2) stepper_2.move(steps);
    else if (gauge_num == 5) stepper_5.move(steps);
    else if (gauge_num == 7) stepper_7.move(steps);
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    publish_all_state("adjust_zero");
    return true;
  }
  if (strcmp(op, "set_current_as_zero") == 0) {
    int gauge_num = p.containsKey("gauge") ? (int)(p["gauge"] | 0) : ctx_default_gauge_num(ctx);
    if (gauge_num == 2) stepper_2.setCurrentPosition(GAUGE_MIN_STEPS);
    else if (gauge_num == 5) stepper_5.setCurrentPosition(GAUGE_MIN_STEPS);
    else if (gauge_num == 7) stepper_7.setCurrentPosition(GAUGE_MIN_STEPS);
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

  pinMode(PIN_GAUGE_2_ENABLE, OUTPUT);
  pinMode(PIN_GAUGE_5_ENABLE, OUTPUT);
  pinMode(PIN_GAUGE_7_ENABLE, OUTPUT);
  digitalWrite(PIN_GAUGE_2_ENABLE, HIGH);
  digitalWrite(PIN_GAUGE_5_ENABLE, HIGH);
  digitalWrite(PIN_GAUGE_7_ENABLE, HIGH);

  stepper_2.setPinsInverted(false, false, false);
  stepper_5.setPinsInverted(true, false, false);
  stepper_7.setPinsInverted(true, false, false);

  stepper_2.setMaxSpeed(STEPPER_MAX_SPEED);
  stepper_2.setAcceleration(STEPPER_ACCEL);
  stepper_5.setMaxSpeed(STEPPER_MAX_SPEED);
  stepper_5.setAcceleration(STEPPER_ACCEL);
  stepper_7.setMaxSpeed(STEPPER_MAX_SPEED);
  stepper_7.setAcceleration(STEPPER_ACCEL);

  pinMode(PIN_VALVE_2_POT, INPUT);
  pinMode(PIN_VALVE_5_POT, INPUT);
  pinMode(PIN_VALVE_7_POT, INPUT);

  load_gauge_positions();

  // Auto-zero on boot (matches v2 intent): enable briefly, move to 0, then remain disabled until activate.
  activate_all();
  stepper_2.moveTo(GAUGE_MIN_STEPS);
  stepper_5.moveTo(GAUGE_MIN_STEPS);
  stepper_7.moveTo(GAUGE_MIN_STEPS);
  while (stepper_2.distanceToGo() != 0 || stepper_5.distanceToGo() != 0 || stepper_7.distanceToGo() != 0) {
    stepper_2.run();
    stepper_5.run();
    stepper_7.run();
  }
  gauges_active = false;
  digitalWrite(PIN_GAUGE_2_ENABLE, HIGH);
  digitalWrite(PIN_GAUGE_5_ENABLE, HIGH);
  digitalWrite(PIN_GAUGE_7_ENABLE, HIGH);
  save_gauge_positions();

  if (!sne_gauge_2.begin()) while (true) delay(1000);
  if (!sne_gauge_5.begin()) while (true) delay(1000);
  if (!sne_gauge_7.begin()) while (true) delay(1000);

  sne_gauge_2.setCommandHandler(handleCommand, &ctx_g2);
  sne_gauge_5.setCommandHandler(handleCommand, &ctx_g5);
  sne_gauge_7.setCommandHandler(handleCommand, &ctx_g7);

  read_valve_positions();
  move_gauges();
  publish_all_state("boot");
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  stepper_2.run();
  stepper_5.run();
  stepper_7.run();

  read_valve_positions();
  move_gauges();

  const unsigned long now = millis();
  static unsigned long last_publish = 0;
  bool force_publish = (now - last_publish) >= SENSOR_REFRESH_MS;

  bool any_change = force_publish;
  if (valve_2_psi != last_published_valve_2_psi) any_change = true;
  if (valve_5_psi != last_published_valve_5_psi) any_change = true;
  if (valve_7_psi != last_published_valve_7_psi) any_change = true;

  if (any_change) {
    publish_all_state(force_publish ? "periodic" : "change");
    save_gauge_positions();
    last_publish = now;
    last_published_valve_2_psi = valve_2_psi;
    last_published_valve_5_psi = valve_5_psi;
    last_published_valve_7_psi = valve_7_psi;
  }
}
