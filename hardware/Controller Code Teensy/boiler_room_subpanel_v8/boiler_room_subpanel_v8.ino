// Boiler Room Subpanel Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device (+ controller device).
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - boiler_room_subpanel_intro_tv_power
// - boiler_room_subpanel_intro_tv_lift
// - boiler_room_subpanel_fog_power
// - boiler_room_subpanel_fog_trigger
// - boiler_room_subpanel_fog_ultrasonic
// - boiler_room_subpanel_boiler_room_barrel
// - boiler_room_subpanel_ir_sensor
// - boiler_room_subpanel_study_door
// - boiler_room_subpanel_gauge_progress_chest
// - boiler_room_subpanel_controller

#include <Arduino.h>
#include <ArduinoJson.h>

#include <FastLED.h>
#include <IRremote.hpp>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_INTRO_TV_POWER = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_INTRO_TV_LIFT = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FOG_POWER = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FOG_TRIGGER = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FOG_ULTRASONIC = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BARREL = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_IR_SENSOR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_STUDY_DOOR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GAUGE_CHEST = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CONTROLLER = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins
static const int power_led_pin = 13;

// TV Lift System
static const int tv_power_pin = 32;
static const int tv_lift_up_pin = 2;
static const int tv_lift_down_pin = 3;

// Fog Machine
static const int fog_power_pin = 38;
static const int fog_trigger_pin = 33;
static const int ultrasonic_water_pin = 34;

// Maglocks (HIGH = locked, LOW = unlocked)
static const int barrel_maglock_pin = 36;
static const int study_door_maglock_top_pin = 29;
static const int study_door_maglock_bottom_a_pin = 30;
static const int study_door_maglock_bottom_b_pin = 31;

// IR Sensor
static const int ir_sensor_pin = 35;

// Gauge LEDs
static const int gauge_lights_pin = 27;
static const int num_leds = 60;
static CRGB leds[num_leds];

// Expected gun id (from v2)
static const uint32_t expected_gun_id = 0x51;

// Hardware state
static bool tv_power_on = false;
static int tv_lift_state = 0; // 0=stopped, 1=up, -1=down
static bool fog_power_on = false;
static bool fog_trigger_on = false;
static bool ultrasonic_water_on = false;
static bool barrel_maglock_locked = true;
static bool study_door_top_locked = true;
static bool study_door_bottom_a_locked = false;
static bool study_door_bottom_b_locked = false;
static int gauge_progress_level = 0; // 0-3
static bool ir_sensor_active = true;

// IR duplicate tracking
static uint32_t last_ir_raw_data = 0;
static unsigned long last_ir_timestamp = 0;

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
  c.rxJsonCapacity = 4096;
  c.txJsonCapacity = 4096;
  return c;
}

static sentient_v8::Client sne_tv_power(make_cfg("boiler_room_subpanel_intro_tv_power", HMAC_INTRO_TV_POWER));
static sentient_v8::Client sne_tv_lift(make_cfg("boiler_room_subpanel_intro_tv_lift", HMAC_INTRO_TV_LIFT));
static sentient_v8::Client sne_fog_power(make_cfg("boiler_room_subpanel_fog_power", HMAC_FOG_POWER));
static sentient_v8::Client sne_fog_trigger(make_cfg("boiler_room_subpanel_fog_trigger", HMAC_FOG_TRIGGER));
static sentient_v8::Client sne_fog_ultrasonic(make_cfg("boiler_room_subpanel_fog_ultrasonic", HMAC_FOG_ULTRASONIC));
static sentient_v8::Client sne_barrel(make_cfg("boiler_room_subpanel_boiler_room_barrel", HMAC_BARREL));
static sentient_v8::Client sne_ir(make_cfg("boiler_room_subpanel_ir_sensor", HMAC_IR_SENSOR));
static sentient_v8::Client sne_study_door(make_cfg("boiler_room_subpanel_study_door", HMAC_STUDY_DOOR));
static sentient_v8::Client sne_gauge(make_cfg("boiler_room_subpanel_gauge_progress_chest", HMAC_GAUGE_CHEST));
static sentient_v8::Client sne_controller(make_cfg("boiler_room_subpanel_controller", HMAC_CONTROLLER));

enum class DeviceKind : uint8_t { TvPower, TvLift, FogPower, FogTrigger, FogUltrasonic, Barrel, IrSensor, StudyDoor, Gauge, Controller };

struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_tv_power = {DeviceKind::TvPower};
static DeviceCtx ctx_tv_lift = {DeviceKind::TvLift};
static DeviceCtx ctx_fog_power = {DeviceKind::FogPower};
static DeviceCtx ctx_fog_trigger = {DeviceKind::FogTrigger};
static DeviceCtx ctx_fog_ultrasonic = {DeviceKind::FogUltrasonic};
static DeviceCtx ctx_barrel = {DeviceKind::Barrel};
static DeviceCtx ctx_ir = {DeviceKind::IrSensor};
static DeviceCtx ctx_study_door = {DeviceKind::StudyDoor};
static DeviceCtx ctx_gauge = {DeviceKind::Gauge};
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

static void apply_outputs() {
  digitalWrite(power_led_pin, HIGH);

  digitalWrite(tv_power_pin, tv_power_on ? HIGH : LOW);

  if (tv_lift_state == 1) {
    digitalWrite(tv_lift_up_pin, HIGH);
    digitalWrite(tv_lift_down_pin, LOW);
  } else if (tv_lift_state == -1) {
    digitalWrite(tv_lift_up_pin, LOW);
    digitalWrite(tv_lift_down_pin, HIGH);
  } else {
    digitalWrite(tv_lift_up_pin, LOW);
    digitalWrite(tv_lift_down_pin, LOW);
  }

  digitalWrite(fog_power_pin, fog_power_on ? HIGH : LOW);
  digitalWrite(fog_trigger_pin, fog_trigger_on ? HIGH : LOW);
  digitalWrite(ultrasonic_water_pin, ultrasonic_water_on ? HIGH : LOW);

  digitalWrite(barrel_maglock_pin, barrel_maglock_locked ? HIGH : LOW);
  digitalWrite(study_door_maglock_top_pin, study_door_top_locked ? HIGH : LOW);
  digitalWrite(study_door_maglock_bottom_a_pin, study_door_bottom_a_locked ? HIGH : LOW);
  digitalWrite(study_door_maglock_bottom_b_pin, study_door_bottom_b_locked ? HIGH : LOW);
}

static void set_gauge_level(int level) {
  if (level < 0) level = 0;
  if (level > 3) level = 3;
  gauge_progress_level = level;

  if (level == 0) {
    fill_solid(leds, num_leds, CRGB::Black);
  } else if (level == 1) {
    fill_solid(leds, 15, CRGB::Green);
    fill_solid(leds + 15, num_leds - 15, CRGB::Black);
  } else if (level == 2) {
    fill_solid(leds, 30, CRGB::Green);
    fill_solid(leds + 30, num_leds - 30, CRGB::Black);
  } else {
    fill_solid(leds, 45, CRGB::Green);
    fill_solid(leds + 45, num_leds - 45, CRGB::Black);
  }
  FastLED.show();
}

static void publish_state_tv_power() {
  StaticJsonDocument<192> st;
  st["on"] = tv_power_on;
  sne_tv_power.publishState(st);
}

static void publish_state_tv_lift() {
  StaticJsonDocument<192> st;
  st["state"] = tv_lift_state;
  st["kind"] = (tv_lift_state == 1) ? "UP" : (tv_lift_state == -1) ? "DOWN" : "STOPPED";
  sne_tv_lift.publishState(st);
}

static void publish_state_fog_power() {
  StaticJsonDocument<192> st;
  st["on"] = fog_power_on;
  sne_fog_power.publishState(st);
}

static void publish_state_fog_trigger() {
  StaticJsonDocument<192> st;
  st["on"] = fog_trigger_on;
  sne_fog_trigger.publishState(st);
}

static void publish_state_fog_ultrasonic() {
  StaticJsonDocument<192> st;
  st["on"] = ultrasonic_water_on;
  sne_fog_ultrasonic.publishState(st);
}

static void publish_state_barrel() {
  StaticJsonDocument<192> st;
  st["locked"] = barrel_maglock_locked;
  sne_barrel.publishState(st);
}

static void publish_state_ir() {
  StaticJsonDocument<192> st;
  st["active"] = ir_sensor_active;
  sne_ir.publishState(st);
}

static void publish_state_study_door() {
  StaticJsonDocument<256> st;
  st["top_locked"] = study_door_top_locked;
  st["bottom_a_locked"] = study_door_bottom_a_locked;
  st["bottom_b_locked"] = study_door_bottom_b_locked;
  sne_study_door.publishState(st);
}

static void publish_state_gauge() {
  StaticJsonDocument<192> st;
  st["level"] = gauge_progress_level;
  sne_gauge.publishState(st);
}

static void publish_state_controller(bool shutdown_ready = false) {
  StaticJsonDocument<512> st;
  st["tv_power_on"] = tv_power_on;
  st["tv_lift_state"] = tv_lift_state;
  st["fog_power_on"] = fog_power_on;
  st["fog_trigger_on"] = fog_trigger_on;
  st["ultrasonic_water_on"] = ultrasonic_water_on;
  st["barrel_maglock_locked"] = barrel_maglock_locked;
  st["study_top_locked"] = study_door_top_locked;
  st["study_bottom_a_locked"] = study_door_bottom_a_locked;
  st["study_bottom_b_locked"] = study_door_bottom_b_locked;
  st["gauge_progress_level"] = gauge_progress_level;
  st["ir_sensor_active"] = ir_sensor_active;
  if (shutdown_ready) st["shutdown_ready"] = true;
  sne_controller.publishState(st);
}

static void publish_all_states() {
  publish_state_tv_power();
  publish_state_tv_lift();
  publish_state_fog_power();
  publish_state_fog_trigger();
  publish_state_fog_ultrasonic();
  publish_state_barrel();
  publish_state_ir();
  publish_state_study_door();
  publish_state_gauge();
  publish_state_controller(false);
}

static void power_off_sequence() {
  tv_power_on = false;
  tv_lift_state = 0;
  fog_trigger_on = false;
  ultrasonic_water_on = false;
  fog_power_on = false;
  barrel_maglock_locked = true;
  study_door_top_locked = true;
  study_door_bottom_a_locked = true;
  study_door_bottom_b_locked = true;
  set_gauge_level(0);
  ir_sensor_active = false;
  apply_outputs();
  publish_all_states();
  publish_state_controller(true);
}

static void check_ir_sensor() {
  if (!ir_sensor_active) return;
  if (!IrReceiver.decode()) return;

  bool is_duplicate = (IrReceiver.decodedIRData.decodedRawData == last_ir_raw_data && (millis() - last_ir_timestamp) < 500);
  bool is_weak_signal = (IrReceiver.decodedIRData.protocol == UNKNOWN || IrReceiver.decodedIRData.protocol == 2);

  if (!is_duplicate && !is_weak_signal) {
    uint32_t gun_code = IrReceiver.decodedIRData.command;

    StaticJsonDocument<256> t;
    t["gun_code"] = gun_code;
    t["expected_code"] = expected_gun_id;
    t["is_correct"] = (gun_code == expected_gun_id);
    t["timestamp_ms"] = (uint64_t)millis();
    sne_barrel.publishTelemetry(t);
  }

  last_ir_raw_data = IrReceiver.decodedIRData.decodedRawData;
  last_ir_timestamp = millis();
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

  switch (ctx->kind) {
  case DeviceKind::TvPower:
    if (strcmp(op, "set") != 0 || !p.containsKey("on")) goto bad;
    tv_power_on = p["on"] | false;
    apply_outputs();
    publish_state_tv_power();
    publish_state_controller(false);
    return true;

  case DeviceKind::TvLift:
    if (strcmp(op, "lift_up") == 0) tv_lift_state = 1;
    else if (strcmp(op, "lift_down") == 0) tv_lift_state = -1;
    else if (strcmp(op, "lift_stop") == 0) tv_lift_state = 0;
    else goto bad;
    apply_outputs();
    publish_state_tv_lift();
    publish_state_controller(false);
    return true;

  case DeviceKind::FogPower:
    if (strcmp(op, "set") != 0 || !p.containsKey("on")) goto bad;
    fog_power_on = p["on"] | false;
    apply_outputs();
    publish_state_fog_power();
    publish_state_controller(false);
    return true;

  case DeviceKind::FogTrigger: {
    if (strcmp(op, "trigger") != 0) goto bad;
    long durationMs = p["duration_ms"] | (p["duration"] | 500);
    if (durationMs < 100) durationMs = 100;
    if (durationMs > 3000) durationMs = 3000;
    fog_trigger_on = true;
    apply_outputs();
    publish_state_fog_trigger();
    publish_state_controller(false);
    delay((unsigned long)durationMs);
    fog_trigger_on = false;
    apply_outputs();
    publish_state_fog_trigger();
    publish_state_controller(false);
    return true;
  }

  case DeviceKind::FogUltrasonic:
    if (strcmp(op, "set") != 0 || !p.containsKey("on")) goto bad;
    ultrasonic_water_on = p["on"] | false;
    apply_outputs();
    publish_state_fog_ultrasonic();
    publish_state_controller(false);
    return true;

  case DeviceKind::Barrel:
    if (strcmp(op, "unlock") == 0) barrel_maglock_locked = false;
    else if (strcmp(op, "lock") == 0) barrel_maglock_locked = true;
    else goto bad;
    apply_outputs();
    publish_state_barrel();
    publish_state_controller(false);
    return true;

  case DeviceKind::IrSensor:
    if (strcmp(op, "activate") == 0) ir_sensor_active = true;
    else if (strcmp(op, "deactivate") == 0) ir_sensor_active = false;
    else goto bad;
    publish_state_ir();
    publish_state_controller(false);
    return true;

  case DeviceKind::StudyDoor:
    if (strcmp(op, "lock") == 0) {
      study_door_top_locked = true;
      study_door_bottom_a_locked = true;
      study_door_bottom_b_locked = true;
    } else if (strcmp(op, "unlock") == 0) {
      study_door_top_locked = false;
      study_door_bottom_a_locked = false;
      study_door_bottom_b_locked = false;
    } else {
      goto bad;
    }
    apply_outputs();
    publish_state_study_door();
    publish_state_controller(false);
    return true;

  case DeviceKind::Gauge:
    if (strcmp(op, "clear") == 0) set_gauge_level(0);
    else if (strcmp(op, "solved_1") == 0) set_gauge_level(1);
    else if (strcmp(op, "solved_2") == 0) set_gauge_level(2);
    else if (strcmp(op, "solved_3") == 0) set_gauge_level(3);
    else if (strcmp(op, "set_level") == 0 && p.containsKey("level")) set_gauge_level(p["level"] | 0);
    else goto bad;
    publish_state_gauge();
    publish_state_controller(false);
    return true;

  case DeviceKind::Controller:
    if (strcmp(op, "power_off_sequence") == 0) {
      power_off_sequence();
      return true;
    }
    if (strcmp(op, "request_status") == 0) {
      publish_all_states();
      return true;
    }
    if (strcmp(op, "noop") == 0) return true;
    goto bad;
  }

bad:
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(power_led_pin, OUTPUT);
  pinMode(tv_power_pin, OUTPUT);
  pinMode(tv_lift_up_pin, OUTPUT);
  pinMode(tv_lift_down_pin, OUTPUT);
  pinMode(fog_power_pin, OUTPUT);
  pinMode(fog_trigger_pin, OUTPUT);
  pinMode(ultrasonic_water_pin, OUTPUT);
  pinMode(barrel_maglock_pin, OUTPUT);
  pinMode(study_door_maglock_top_pin, OUTPUT);
  pinMode(study_door_maglock_bottom_a_pin, OUTPUT);
  pinMode(study_door_maglock_bottom_b_pin, OUTPUT);

  FastLED.addLeds<WS2812B, gauge_lights_pin, GRB>(leds, num_leds);
  FastLED.setBrightness(255);
  fill_solid(leds, num_leds, CRGB::Black);
  FastLED.show();

  IrReceiver.begin(ir_sensor_pin, false);

  // Safe defaults as in v2 (locks locked, tv/fog off, study bottom initially unlocked).
  tv_power_on = false;
  tv_lift_state = 0;
  fog_power_on = false;
  fog_trigger_on = false;
  ultrasonic_water_on = false;
  barrel_maglock_locked = true;
  study_door_top_locked = true;
  study_door_bottom_a_locked = false;
  study_door_bottom_b_locked = false;
  set_gauge_level(0);
  ir_sensor_active = true;

  apply_outputs();

  if (!sne_tv_power.begin()) while (true) delay(1000);
  if (!sne_tv_lift.begin()) while (true) delay(1000);
  if (!sne_fog_power.begin()) while (true) delay(1000);
  if (!sne_fog_trigger.begin()) while (true) delay(1000);
  if (!sne_fog_ultrasonic.begin()) while (true) delay(1000);
  if (!sne_barrel.begin()) while (true) delay(1000);
  if (!sne_ir.begin()) while (true) delay(1000);
  if (!sne_study_door.begin()) while (true) delay(1000);
  if (!sne_gauge.begin()) while (true) delay(1000);
  if (!sne_controller.begin()) while (true) delay(1000);

  sne_tv_power.setCommandHandler(handleCommand, &ctx_tv_power);
  sne_tv_lift.setCommandHandler(handleCommand, &ctx_tv_lift);
  sne_fog_power.setCommandHandler(handleCommand, &ctx_fog_power);
  sne_fog_trigger.setCommandHandler(handleCommand, &ctx_fog_trigger);
  sne_fog_ultrasonic.setCommandHandler(handleCommand, &ctx_fog_ultrasonic);
  sne_barrel.setCommandHandler(handleCommand, &ctx_barrel);
  sne_ir.setCommandHandler(handleCommand, &ctx_ir);
  sne_study_door.setCommandHandler(handleCommand, &ctx_study_door);
  sne_gauge.setCommandHandler(handleCommand, &ctx_gauge);
  sne_controller.setCommandHandler(handleCommand, &ctx_controller);

  publish_all_states();
}

void loop() {
  sne_tv_power.loop();
  sne_tv_lift.loop();
  sne_fog_power.loop();
  sne_fog_trigger.loop();
  sne_fog_ultrasonic.loop();
  sne_barrel.loop();
  sne_ir.loop();
  sne_study_door.loop();
  sne_gauge.loop();
  sne_controller.loop();

  check_ir_sensor();
}
