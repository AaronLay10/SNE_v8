// study_b_v8 — Study B Controller (Teensy 4.1)
//
// Ported from v2 firmware (stateless executor):
// - 4 stepper-motor drivers (study fan + 3 wall gears) stepped via pulse pins
// - TV power (2), Makservo relay, fog machine power/trigger, fan light, blacklights, nixie LEDs
//
// v8 behavior:
// - Option 2 device identity: one v8 `device_id` per logical output
// - One MQTT connection per `device_id` (correct LWT OFFLINE semantics)
// - Commands are `action="SET"` and require `parameters.op` (string)

#include <Arduino.h>
#include <ArduinoJson.h>

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

// Study fan stepper (DMX542)
static const int PIN_FAN_STEP_POS = 0;
static const int PIN_FAN_STEP_NEG = 1;
static const int PIN_FAN_DIR_POS = 2;
static const int PIN_FAN_DIR_NEG = 3;
static const int PIN_FAN_ENABLE = 7;

// Wall gear steppers (3x DMX542)
static const int PIN_GEAR1_STEP_POS = 38;
static const int PIN_GEAR1_STEP_NEG = 39;
static const int PIN_GEAR1_DIR_POS = 40;
static const int PIN_GEAR1_DIR_NEG = 41;

static const int PIN_GEAR2_STEP_POS = 20;
static const int PIN_GEAR2_STEP_NEG = 21;
static const int PIN_GEAR2_DIR_POS = 22;
static const int PIN_GEAR2_DIR_NEG = 23;

static const int PIN_GEAR3_STEP_POS = 16;
static const int PIN_GEAR3_STEP_NEG = 17;
static const int PIN_GEAR3_DIR_POS = 18;
static const int PIN_GEAR3_DIR_NEG = 19;

static const int PIN_GEARS_ENABLE = 15;
static const int PIN_MOTORS_POWER = 24;

// Other outputs
static const int PIN_TV_1 = 9;
static const int PIN_TV_2 = 10;
static const int PIN_MAKSERVO = 8;
static const int PIN_FOG_POWER = 4;
static const int PIN_FOG_TRIGGER = 5;
static const int PIN_STUDY_FAN_LIGHT = 11;
static const int PIN_BLACKLIGHTS = 36;
static const int PIN_NIXIE_LEDS = 35;

// --- Stepper pulse engine (ported from v2) ---
enum MotorState : uint8_t { STOPPED = 0, RUNNING_SLOW = 1, RUNNING_FAST = 2 };

struct StepperMotor {
  int step_pos_pin;
  int step_neg_pin;
  int dir_pos_pin;
  int dir_neg_pin;
  MotorState state;
  unsigned long last_step_time;
  unsigned long step_interval;  // microseconds
};

static StepperMotor fan_motor = {PIN_FAN_STEP_POS, PIN_FAN_STEP_NEG, PIN_FAN_DIR_POS, PIN_FAN_DIR_NEG, STOPPED, 0, 2000};
static StepperMotor gear1_motor = {PIN_GEAR1_STEP_POS, PIN_GEAR1_STEP_NEG, PIN_GEAR1_DIR_POS, PIN_GEAR1_DIR_NEG, STOPPED, 0, 2000};
static StepperMotor gear2_motor = {PIN_GEAR2_STEP_POS, PIN_GEAR2_STEP_NEG, PIN_GEAR2_DIR_POS, PIN_GEAR2_DIR_NEG, STOPPED, 0, 2000};
static StepperMotor gear3_motor = {PIN_GEAR3_STEP_POS, PIN_GEAR3_STEP_NEG, PIN_GEAR3_DIR_POS, PIN_GEAR3_DIR_NEG, STOPPED, 0, 2000};

static bool tv_1_on = false;
static bool tv_2_on = false;
static bool makservo_on = false;
static bool fog_power_on = false;
static bool fog_trigger_on = false;
static bool study_fan_light_on = false;
static bool blacklights_on = false;
static bool nixie_leds_on = false;

static void step_motor(StepperMotor &motor) {
  if (motor.state == STOPPED) return;

  unsigned long now = micros();
  if (now - motor.last_step_time >= motor.step_interval) {
    digitalWrite(motor.step_pos_pin, HIGH);
    digitalWrite(motor.step_neg_pin, LOW);
    delayMicroseconds(10);
    digitalWrite(motor.step_pos_pin, LOW);
    digitalWrite(motor.step_neg_pin, HIGH);
    motor.last_step_time = now;
  }
}

static void update_motors() {
  step_motor(fan_motor);
  step_motor(gear1_motor);
  step_motor(gear2_motor);
  step_motor(gear3_motor);
}

static bool any_gears_running() {
  return gear1_motor.state != STOPPED || gear2_motor.state != STOPPED || gear3_motor.state != STOPPED;
}
static bool any_motors_running() {
  return fan_motor.state != STOPPED || any_gears_running();
}

static void apply_motor_power_policy() {
  // Safety: keep the global power on only while any motor is running.
  digitalWrite(PIN_MOTORS_POWER, any_motors_running() ? HIGH : LOW);
  digitalWrite(PIN_GEARS_ENABLE, any_gears_running() ? LOW : HIGH);
  digitalWrite(PIN_FAN_ENABLE, fan_motor.state != STOPPED ? LOW : HIGH);
}

// --- v8 Clients (one per sub-device) ---
static sentient_v8::Config cfg_study_b_study_fan = make_cfg("study_b_study_fan", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_wall_gear_1 = make_cfg("study_b_wall_gear_1", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_wall_gear_2 = make_cfg("study_b_wall_gear_2", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_wall_gear_3 = make_cfg("study_b_wall_gear_3", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_tv_1 = make_cfg("study_b_tv_1", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_tv_2 = make_cfg("study_b_tv_2", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_makservo = make_cfg("study_b_makservo", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_fog_machine = make_cfg("study_b_fog_machine", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_study_fan_light = make_cfg("study_b_study_fan_light", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_blacklights = make_cfg("study_b_blacklights", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_study_b_nixie_leds = make_cfg("study_b_nixie_leds", HMAC_KEY_PLACEHOLDER);

static sentient_v8::Client sne_study_b_study_fan(cfg_study_b_study_fan);
static sentient_v8::Client sne_study_b_wall_gear_1(cfg_study_b_wall_gear_1);
static sentient_v8::Client sne_study_b_wall_gear_2(cfg_study_b_wall_gear_2);
static sentient_v8::Client sne_study_b_wall_gear_3(cfg_study_b_wall_gear_3);
static sentient_v8::Client sne_study_b_tv_1(cfg_study_b_tv_1);
static sentient_v8::Client sne_study_b_tv_2(cfg_study_b_tv_2);
static sentient_v8::Client sne_study_b_makservo(cfg_study_b_makservo);
static sentient_v8::Client sne_study_b_fog_machine(cfg_study_b_fog_machine);
static sentient_v8::Client sne_study_b_study_fan_light(cfg_study_b_study_fan_light);
static sentient_v8::Client sne_study_b_blacklights(cfg_study_b_blacklights);
static sentient_v8::Client sne_study_b_nixie_leds(cfg_study_b_nixie_leds);

static sentient_v8::Client *clients[] = {&sne_study_b_study_fan, &sne_study_b_wall_gear_1, &sne_study_b_wall_gear_2,
                                         &sne_study_b_wall_gear_3, &sne_study_b_tv_1, &sne_study_b_tv_2,
                                         &sne_study_b_makservo, &sne_study_b_fog_machine, &sne_study_b_study_fan_light,
                                         &sne_study_b_blacklights, &sne_study_b_nixie_leds};

enum class DeviceKind : uint8_t {
  StudyFan,
  WallGear1,
  WallGear2,
  WallGear3,
  Tv1,
  Tv2,
  Makservo,
  FogMachine,
  StudyFanLight,
  Blacklights,
  NixieLeds,
};

struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_study_fan = {DeviceKind::StudyFan};
static DeviceCtx ctx_wall_gear_1 = {DeviceKind::WallGear1};
static DeviceCtx ctx_wall_gear_2 = {DeviceKind::WallGear2};
static DeviceCtx ctx_wall_gear_3 = {DeviceKind::WallGear3};
static DeviceCtx ctx_tv_1 = {DeviceKind::Tv1};
static DeviceCtx ctx_tv_2 = {DeviceKind::Tv2};
static DeviceCtx ctx_makservo = {DeviceKind::Makservo};
static DeviceCtx ctx_fog_machine = {DeviceKind::FogMachine};
static DeviceCtx ctx_study_fan_light = {DeviceKind::StudyFanLight};
static DeviceCtx ctx_blacklights = {DeviceKind::Blacklights};
static DeviceCtx ctx_nixie_leds = {DeviceKind::NixieLeds};

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

static const char *motor_state_str(MotorState s) {
  switch (s) {
    case STOPPED: return "stopped";
    case RUNNING_SLOW: return "slow";
    case RUNNING_FAST: return "fast";
  }
  return "unknown";
}

static void publish_study_fan_state(const char *reason) {
  StaticJsonDocument<384> st;
  st["firmware_version"] = "study_b_v8";
  st["reason"] = reason ? reason : "";
  st["state"] = motor_state_str(fan_motor.state);
  st["step_interval_us"] = fan_motor.step_interval;
  st["motors_power"] = (digitalRead(PIN_MOTORS_POWER) == HIGH);
  st["enabled"] = (digitalRead(PIN_FAN_ENABLE) == LOW);
  sne_study_b_study_fan.publishState(st);
}

static void publish_gear_state(sentient_v8::Client &client, const StepperMotor &m, int gear_num, const char *reason) {
  StaticJsonDocument<384> st;
  st["firmware_version"] = "study_b_v8";
  st["reason"] = reason ? reason : "";
  st["gear"] = gear_num;
  st["state"] = motor_state_str(m.state);
  st["step_interval_us"] = m.step_interval;
  st["motors_power"] = (digitalRead(PIN_MOTORS_POWER) == HIGH);
  st["gears_enabled"] = (digitalRead(PIN_GEARS_ENABLE) == LOW);
  client.publishState(st);
}

static void publish_tv_state(sentient_v8::Client &client, int tv_num, bool on, const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "study_b_v8";
  st["reason"] = reason ? reason : "";
  st["tv"] = tv_num;
  st["on"] = on;
  client.publishState(st);
}

static void publish_simple_onoff(sentient_v8::Client &client, const char *label, bool on, const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "study_b_v8";
  st["reason"] = reason ? reason : "";
  st[label] = on;
  st["on"] = on;
  client.publishState(st);
}

static void publish_fog_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "study_b_v8";
  st["reason"] = reason ? reason : "";
  st["power_on"] = fog_power_on;
  st["trigger_on"] = fog_trigger_on;
  sne_study_b_fog_machine.publishState(st);
}

static void publish_all_state(const char *reason) {
  publish_study_fan_state(reason);
  publish_gear_state(sne_study_b_wall_gear_1, gear1_motor, 1, reason);
  publish_gear_state(sne_study_b_wall_gear_2, gear2_motor, 2, reason);
  publish_gear_state(sne_study_b_wall_gear_3, gear3_motor, 3, reason);
  publish_tv_state(sne_study_b_tv_1, 1, tv_1_on, reason);
  publish_tv_state(sne_study_b_tv_2, 2, tv_2_on, reason);
  publish_simple_onoff(sne_study_b_makservo, "makservo", makservo_on, reason);
  publish_fog_state(reason);
  publish_simple_onoff(sne_study_b_study_fan_light, "study_fan_light", study_fan_light_on, reason);
  publish_simple_onoff(sne_study_b_blacklights, "blacklights", blacklights_on, reason);
  publish_simple_onoff(sne_study_b_nixie_leds, "nixie_leds", nixie_leds_on, reason);
}

static void set_output(int pin, bool on) {
  digitalWrite(pin, on ? HIGH : LOW);
}

static void set_motor_mode(StepperMotor &motor, MotorState state) {
  motor.state = state;
  if (state == RUNNING_SLOW) motor.step_interval = 2000;
  if (state == RUNNING_FAST) motor.step_interval = 667;
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

  // --- Motors ---
  if (ctx->kind == DeviceKind::StudyFan) {
    if (strcmp(op, "start") == 0 || strcmp(op, "slow") == 0) {
      set_motor_mode(fan_motor, RUNNING_SLOW);
      publish_study_fan_state(op);
      return true;
    }
    if (strcmp(op, "fast") == 0) {
      set_motor_mode(fan_motor, RUNNING_FAST);
      publish_study_fan_state(op);
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      set_motor_mode(fan_motor, STOPPED);
      publish_study_fan_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::WallGear1 || ctx->kind == DeviceKind::WallGear2 || ctx->kind == DeviceKind::WallGear3) {
    StepperMotor *m = (ctx->kind == DeviceKind::WallGear1) ? &gear1_motor : (ctx->kind == DeviceKind::WallGear2) ? &gear2_motor : &gear3_motor;
    sentient_v8::Client *client = (ctx->kind == DeviceKind::WallGear1) ? &sne_study_b_wall_gear_1
                                   : (ctx->kind == DeviceKind::WallGear2) ? &sne_study_b_wall_gear_2
                                                                          : &sne_study_b_wall_gear_3;
    int gear_num = (ctx->kind == DeviceKind::WallGear1) ? 1 : (ctx->kind == DeviceKind::WallGear2) ? 2 : 3;

    if (strcmp(op, "start") == 0 || strcmp(op, "slow") == 0) {
      set_motor_mode(*m, RUNNING_SLOW);
      publish_gear_state(*client, *m, gear_num, op);
      return true;
    }
    if (strcmp(op, "fast") == 0) {
      set_motor_mode(*m, RUNNING_FAST);
      publish_gear_state(*client, *m, gear_num, op);
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      set_motor_mode(*m, STOPPED);
      publish_gear_state(*client, *m, gear_num, op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // --- Simple on/off devices ---
  if (ctx->kind == DeviceKind::Tv1) {
    if (strcmp(op, "on") == 0) tv_1_on = true;
    else if (strcmp(op, "off") == 0) tv_1_on = false;
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    set_output(PIN_TV_1, tv_1_on);
    publish_tv_state(sne_study_b_tv_1, 1, tv_1_on, op);
    return true;
  }
  if (ctx->kind == DeviceKind::Tv2) {
    if (strcmp(op, "on") == 0) tv_2_on = true;
    else if (strcmp(op, "off") == 0) tv_2_on = false;
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    set_output(PIN_TV_2, tv_2_on);
    publish_tv_state(sne_study_b_tv_2, 2, tv_2_on, op);
    return true;
  }
  if (ctx->kind == DeviceKind::Makservo) {
    if (strcmp(op, "on") == 0) makservo_on = true;
    else if (strcmp(op, "off") == 0) makservo_on = false;
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    set_output(PIN_MAKSERVO, makservo_on);
    publish_simple_onoff(sne_study_b_makservo, "makservo", makservo_on, op);
    return true;
  }
  if (ctx->kind == DeviceKind::StudyFanLight) {
    if (strcmp(op, "on") == 0) study_fan_light_on = true;
    else if (strcmp(op, "off") == 0) study_fan_light_on = false;
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    set_output(PIN_STUDY_FAN_LIGHT, study_fan_light_on);
    publish_simple_onoff(sne_study_b_study_fan_light, "study_fan_light", study_fan_light_on, op);
    return true;
  }
  if (ctx->kind == DeviceKind::Blacklights) {
    if (strcmp(op, "on") == 0) blacklights_on = true;
    else if (strcmp(op, "off") == 0) blacklights_on = false;
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    set_output(PIN_BLACKLIGHTS, blacklights_on);
    publish_simple_onoff(sne_study_b_blacklights, "blacklights", blacklights_on, op);
    return true;
  }
  if (ctx->kind == DeviceKind::NixieLeds) {
    if (strcmp(op, "on") == 0) nixie_leds_on = true;
    else if (strcmp(op, "off") == 0) nixie_leds_on = false;
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    set_output(PIN_NIXIE_LEDS, nixie_leds_on);
    publish_simple_onoff(sne_study_b_nixie_leds, "nixie_leds", nixie_leds_on, op);
    return true;
  }

  // --- Fog machine ---
  if (ctx->kind == DeviceKind::FogMachine) {
    if (strcmp(op, "on") == 0) {
      fog_power_on = true;
      set_output(PIN_FOG_POWER, true);
      publish_fog_state(op);
      return true;
    }
    if (strcmp(op, "off") == 0) {
      fog_power_on = false;
      fog_trigger_on = false;
      set_output(PIN_FOG_POWER, false);
      set_output(PIN_FOG_TRIGGER, false);
      publish_fog_state(op);
      return true;
    }
    if (strcmp(op, "trigger") == 0) {
      fog_trigger_on = true;
      set_output(PIN_FOG_TRIGGER, true);
      publish_fog_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
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

  // Stepper pins
  pinMode(PIN_FAN_STEP_POS, OUTPUT);
  pinMode(PIN_FAN_STEP_NEG, OUTPUT);
  pinMode(PIN_FAN_DIR_POS, OUTPUT);
  pinMode(PIN_FAN_DIR_NEG, OUTPUT);
  pinMode(PIN_FAN_ENABLE, OUTPUT);

  pinMode(PIN_GEAR1_STEP_POS, OUTPUT);
  pinMode(PIN_GEAR1_STEP_NEG, OUTPUT);
  pinMode(PIN_GEAR1_DIR_POS, OUTPUT);
  pinMode(PIN_GEAR1_DIR_NEG, OUTPUT);

  pinMode(PIN_GEAR2_STEP_POS, OUTPUT);
  pinMode(PIN_GEAR2_STEP_NEG, OUTPUT);
  pinMode(PIN_GEAR2_DIR_POS, OUTPUT);
  pinMode(PIN_GEAR2_DIR_NEG, OUTPUT);

  pinMode(PIN_GEAR3_STEP_POS, OUTPUT);
  pinMode(PIN_GEAR3_STEP_NEG, OUTPUT);
  pinMode(PIN_GEAR3_DIR_POS, OUTPUT);
  pinMode(PIN_GEAR3_DIR_NEG, OUTPUT);

  pinMode(PIN_GEARS_ENABLE, OUTPUT);
  pinMode(PIN_MOTORS_POWER, OUTPUT);

  // Disable motors initially
  fan_motor.state = STOPPED;
  gear1_motor.state = STOPPED;
  gear2_motor.state = STOPPED;
  gear3_motor.state = STOPPED;
  apply_motor_power_policy();

  // Other outputs
  pinMode(PIN_TV_1, OUTPUT);
  pinMode(PIN_TV_2, OUTPUT);
  pinMode(PIN_MAKSERVO, OUTPUT);
  pinMode(PIN_FOG_POWER, OUTPUT);
  pinMode(PIN_FOG_TRIGGER, OUTPUT);
  pinMode(PIN_STUDY_FAN_LIGHT, OUTPUT);
  pinMode(PIN_BLACKLIGHTS, OUTPUT);
  pinMode(PIN_NIXIE_LEDS, OUTPUT);

  set_output(PIN_TV_1, false);
  set_output(PIN_TV_2, false);
  set_output(PIN_MAKSERVO, false);
  set_output(PIN_FOG_POWER, false);
  set_output(PIN_FOG_TRIGGER, false);
  set_output(PIN_STUDY_FAN_LIGHT, false);
  set_output(PIN_BLACKLIGHTS, false);
  set_output(PIN_NIXIE_LEDS, false);

  if (!sne_study_b_study_fan.begin()) while (true) delay(1000);
  if (!sne_study_b_wall_gear_1.begin()) while (true) delay(1000);
  if (!sne_study_b_wall_gear_2.begin()) while (true) delay(1000);
  if (!sne_study_b_wall_gear_3.begin()) while (true) delay(1000);
  if (!sne_study_b_tv_1.begin()) while (true) delay(1000);
  if (!sne_study_b_tv_2.begin()) while (true) delay(1000);
  if (!sne_study_b_makservo.begin()) while (true) delay(1000);
  if (!sne_study_b_fog_machine.begin()) while (true) delay(1000);
  if (!sne_study_b_study_fan_light.begin()) while (true) delay(1000);
  if (!sne_study_b_blacklights.begin()) while (true) delay(1000);
  if (!sne_study_b_nixie_leds.begin()) while (true) delay(1000);

  sne_study_b_study_fan.setCommandHandler(handleCommand, &ctx_study_fan);
  sne_study_b_wall_gear_1.setCommandHandler(handleCommand, &ctx_wall_gear_1);
  sne_study_b_wall_gear_2.setCommandHandler(handleCommand, &ctx_wall_gear_2);
  sne_study_b_wall_gear_3.setCommandHandler(handleCommand, &ctx_wall_gear_3);
  sne_study_b_tv_1.setCommandHandler(handleCommand, &ctx_tv_1);
  sne_study_b_tv_2.setCommandHandler(handleCommand, &ctx_tv_2);
  sne_study_b_makservo.setCommandHandler(handleCommand, &ctx_makservo);
  sne_study_b_fog_machine.setCommandHandler(handleCommand, &ctx_fog_machine);
  sne_study_b_study_fan_light.setCommandHandler(handleCommand, &ctx_study_fan_light);
  sne_study_b_blacklights.setCommandHandler(handleCommand, &ctx_blacklights);
  sne_study_b_nixie_leds.setCommandHandler(handleCommand, &ctx_nixie_leds);

  publish_all_state("boot");
  last_periodic_publish = millis();
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();
  update_motors();

  const unsigned long now = millis();
  if (now - last_periodic_publish >= STATE_REFRESH_MS) {
    publish_all_state("periodic");
    last_periodic_publish = now;
  }
}
