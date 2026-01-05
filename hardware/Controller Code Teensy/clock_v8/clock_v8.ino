// clock_v8 — Clock Controller (Teensy 4.1)
//
// This v8 port removes embedded v2 game logic. Firmware is a stateless hardware
// executor + sensor publisher; sequencing/validation lives in core.
//
// Hardware (from v2):
// - 3 DM542 steppers (hour/minute/gears) via differential step/dir pins + shared enable
// - 8 analog resistor readers (operator puzzle)
// - 3 rotary encoders (crank puzzle)
// - 2 maglocks, metal door prox sensor
// - 2 actuator outputs (metal door fwd/rwd, exit door fwd/rwd)
// - 4 WS2812B strips (100 LEDs each)
// - fog power (active LOW), blacklight, laser light

#include <Arduino.h>
#include <ArduinoJson.h>

#include <FastLED.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"
#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";
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
  c.rxJsonCapacity = 4096;
  c.txJsonCapacity = 4096;
  return c;
}

// --- Pins (from v2) ---
static const int PIN_POWER_LED = 13;

// Clock steppers (DM542 differential signaling)
static const int CLOCK_HOUR_STEP_POS = 0;
static const int CLOCK_HOUR_STEP_NEG = 1;
static const int CLOCK_HOUR_DIR_POS = 2;
static const int CLOCK_HOUR_DIR_NEG = 3;

static const int CLOCK_GEARS_STEP_POS = 4;
static const int CLOCK_GEARS_STEP_NEG = 5;
static const int CLOCK_GEARS_DIR_POS = 6;
static const int CLOCK_GEARS_DIR_NEG = 7;

static const int CLOCK_MINUTE_STEP_POS = 8;
static const int CLOCK_MINUTE_STEP_NEG = 9;
static const int CLOCK_MINUTE_DIR_POS = 10;
static const int CLOCK_MINUTE_DIR_NEG = 11;

static const int STEPPER_ENABLE = 12; // LOW=enabled, HIGH=disabled

// Operator resistors (analog)
static const int RESISTOR_PINS[8] = {16, 17, 18, 19, 20, 21, 22, 23};

// Crank encoders
static const int ENCODER_BOTTOM_CLK = 24;
static const int ENCODER_BOTTOM_DT = 25;
static const int ENCODER_TOPA_CLK = 38;
static const int ENCODER_TOPA_DT = 39;
static const int ENCODER_TOPB_CLK = 40;
static const int ENCODER_TOPB_DT = 41;

// Maglocks + prox
static const int OPERATOR_MAGLOCK = 26;  // open=LOW, close=HIGH
static const int WOOD_DOOR_MAGLOCK = 27; // open=LOW, close=HIGH
static const int METAL_DOOR_PROX_SENSOR = 28;

// Actuators
static const int METAL_DOOR_FWD = 29;
static const int METAL_DOOR_RWD = 30;
static const int EXIT_DOOR_FWD = 31;
static const int EXIT_DOOR_RWD = 32;

// Other outputs
static const int CLOCKFOGPOWER = 15; // Active LOW
static const int BLACKLIGHT = 14;
static const int LASER_LIGHT = 33;

// LED strips
static const int LED_LEFT_UPPER = 34;
static const int LED_LEFT_LOWER = 35;
static const int LED_RIGHT_UPPER = 36;
static const int LED_RIGHT_LOWER = 37;

static const int NUM_LEDS_PER_STRIP = 100;
static CRGB leds_left_upper[NUM_LEDS_PER_STRIP];
static CRGB leds_left_lower[NUM_LEDS_PER_STRIP];
static CRGB leds_right_upper[NUM_LEDS_PER_STRIP];
static CRGB leds_right_lower[NUM_LEDS_PER_STRIP];

static uint8_t led_brightness = 128;
static const char *led_mode = "solid";
static const char *led_color = "blue";

// --- Stepper state (simplified from v2) ---
static long minutePosition = 0;
static long hourPosition = 0;
static long gearPosition = 0;
static long minuteTarget = 0;
static long hourTarget = 0;
static long gearTarget = 0;
static unsigned long lastStepTimeUs = 0;
static unsigned long stepIntervalUs = 800; // 800us ~ 1250 steps/sec

static void stepMotor(int motor, bool direction) {
  int stepPos, stepNeg, dirPos, dirNeg;
  switch (motor) {
    case 0: stepPos = CLOCK_MINUTE_STEP_POS; stepNeg = CLOCK_MINUTE_STEP_NEG; dirPos = CLOCK_MINUTE_DIR_POS; dirNeg = CLOCK_MINUTE_DIR_NEG; break;
    case 1: stepPos = CLOCK_HOUR_STEP_POS; stepNeg = CLOCK_HOUR_STEP_NEG; dirPos = CLOCK_HOUR_DIR_POS; dirNeg = CLOCK_HOUR_DIR_NEG; break;
    case 2: stepPos = CLOCK_GEARS_STEP_POS; stepNeg = CLOCK_GEARS_STEP_NEG; dirPos = CLOCK_GEARS_DIR_POS; dirNeg = CLOCK_GEARS_DIR_NEG; break;
    default: return;
  }

  // v2 behavior: invert direction for minute motor
  if (motor == 0) {
    digitalWrite(dirPos, direction ? LOW : HIGH);
    digitalWrite(dirNeg, direction ? HIGH : LOW);
  } else {
    digitalWrite(dirPos, direction ? HIGH : LOW);
    digitalWrite(dirNeg, direction ? LOW : HIGH);
  }

  digitalWrite(stepPos, HIGH);
  digitalWrite(stepNeg, LOW);
  delayMicroseconds(10);
  digitalWrite(stepPos, LOW);
  digitalWrite(stepNeg, LOW);
  delayMicroseconds(10);

  if (motor == 0) minutePosition += direction ? 1 : -1;
  if (motor == 1) hourPosition += direction ? 1 : -1;
  if (motor == 2) gearPosition += direction ? 1 : -1;
}

static bool any_stepper_moving() { return minutePosition != minuteTarget || hourPosition != hourTarget || gearPosition != gearTarget; }

static void runSteppers() {
  unsigned long now = micros();
  if (now - lastStepTimeUs < stepIntervalUs) return;
  lastStepTimeUs = now;

  if (!any_stepper_moving()) {
    digitalWrite(STEPPER_ENABLE, HIGH);
    return;
  }

  digitalWrite(STEPPER_ENABLE, LOW);
  if (minutePosition != minuteTarget) stepMotor(0, minuteTarget > minutePosition);
  if (hourPosition != hourTarget) stepMotor(1, hourTarget > hourPosition);
  if (gearPosition != gearTarget) stepMotor(2, gearTarget > gearPosition);
}

// --- Encoder counts (ported from v2) ---
static volatile int encoderBottomCount = 0;
static volatile int encoderTopCount = 0;
static volatile int encoderTopBCount = 0;

static void encoderBottomCLKISR() { encoderBottomCount += (digitalRead(ENCODER_BOTTOM_DT) == LOW) ? 1 : -1; }
static void encoderBottomDTISR() { encoderBottomCount += (digitalRead(ENCODER_BOTTOM_CLK) == LOW) ? -1 : 1; }
static void encoderTopACLKISR() { encoderTopCount += (digitalRead(ENCODER_TOPA_DT) == LOW) ? 1 : -1; }
static void encoderTopADTISR() { encoderTopCount += (digitalRead(ENCODER_TOPA_CLK) == LOW) ? -1 : 1; }
static void encoderTopBCLKISR() { encoderTopBCount += (digitalRead(ENCODER_TOPB_DT) == LOW) ? 1 : -1; }
static void encoderTopBDTISR() { encoderTopBCount += (digitalRead(ENCODER_TOPB_CLK) == LOW) ? -1 : 1; }

// --- Output states ---
static bool operator_maglock_open = false;
static bool wood_maglock_open = false;
static bool metal_door_unlocked = false;
static bool exit_door_unlocked = false;
static bool fog_on = false;
static bool blacklight_on = false;
static bool laser_on = false;

static CRGB color_from_name(const char *color) {
  if (!color) return CRGB::White;
  if (strcmp(color, "red") == 0) return CRGB::Red;
  if (strcmp(color, "green") == 0) return CRGB::Green;
  if (strcmp(color, "blue") == 0) return CRGB::Blue;
  if (strcmp(color, "yellow") == 0) return CRGB::Yellow;
  if (strcmp(color, "purple") == 0) return CRGB::Purple;
  if (strcmp(color, "orange") == 0) return CRGB::Orange;
  if (strcmp(color, "white") == 0) return CRGB::White;
  if (strcmp(color, "off") == 0) return CRGB::Black;
  return CRGB::White;
}

static void set_leds_solid(CRGB c) {
  fill_solid(leds_left_upper, NUM_LEDS_PER_STRIP, c);
  fill_solid(leds_left_lower, NUM_LEDS_PER_STRIP, c);
  fill_solid(leds_right_upper, NUM_LEDS_PER_STRIP, c);
  fill_solid(leds_right_lower, NUM_LEDS_PER_STRIP, c);
  FastLED.show();
}

// --- v8 Clients (one per sub-device) ---
static sentient_v8::Client sne_hour(make_cfg("clock_clock_hour_hand", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_minute(make_cfg("clock_clock_minute_hand", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_gears(make_cfg("clock_clock_gears", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_resistors(make_cfg("clock_operator_resistors", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_encoders(make_cfg("clock_crank_encoders", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_operator_maglock(make_cfg("clock_operator_maglock", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_wood_maglock(make_cfg("clock_wood_door_maglock", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_metal_act(make_cfg("clock_metal_door_actuator", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_exit_act(make_cfg("clock_exit_door_actuator", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_fog(make_cfg("clock_clock_fog_machine", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_blacklight(make_cfg("clock_blacklight", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_laser(make_cfg("clock_laser_light", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_leds(make_cfg("clock_led_strips", HMAC_KEY_PLACEHOLDER));

static sentient_v8::Client *clients[] = {&sne_hour, &sne_minute, &sne_gears, &sne_resistors, &sne_encoders, &sne_operator_maglock,
                                         &sne_wood_maglock, &sne_metal_act, &sne_exit_act, &sne_fog, &sne_blacklight, &sne_laser, &sne_leds};

enum class DeviceKind : uint8_t {
  HourHand,
  MinuteHand,
  Gears,
  OperatorResistors,
  CrankEncoders,
  OperatorMaglock,
  WoodMaglock,
  MetalDoorActuator,
  ExitDoorActuator,
  FogPower,
  Blacklight,
  Laser,
  LedStrips,
};

struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_hour = {DeviceKind::HourHand};
static DeviceCtx ctx_minute = {DeviceKind::MinuteHand};
static DeviceCtx ctx_gears = {DeviceKind::Gears};
static DeviceCtx ctx_resistors = {DeviceKind::OperatorResistors};
static DeviceCtx ctx_encoders = {DeviceKind::CrankEncoders};
static DeviceCtx ctx_operator_maglock = {DeviceKind::OperatorMaglock};
static DeviceCtx ctx_wood_maglock = {DeviceKind::WoodMaglock};
static DeviceCtx ctx_metal = {DeviceKind::MetalDoorActuator};
static DeviceCtx ctx_exit = {DeviceKind::ExitDoorActuator};
static DeviceCtx ctx_fog = {DeviceKind::FogPower};
static DeviceCtx ctx_black = {DeviceKind::Blacklight};
static DeviceCtx ctx_laser = {DeviceKind::Laser};
static DeviceCtx ctx_leds = {DeviceKind::LedStrips};

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

static void publish_stepper_state(sentient_v8::Client &client, const char *name, long pos, long target, const char *reason) {
  StaticJsonDocument<384> st;
  st["firmware_version"] = "clock_v8";
  st["reason"] = reason ? reason : "";
  st["name"] = name;
  st["pos"] = pos;
  st["target"] = target;
  st["enabled"] = (digitalRead(STEPPER_ENABLE) == LOW);
  st["step_interval_us"] = stepIntervalUs;
  client.publishState(st);
}

static void publish_resistors_state(const char *reason) {
  StaticJsonDocument<512> st;
  st["firmware_version"] = "clock_v8";
  st["reason"] = reason ? reason : "";
  JsonArray a = st.createNestedArray("raw");
  for (int i = 0; i < 8; i++) a.add(analogRead(RESISTOR_PINS[i]));
  sne_resistors.publishState(st);
}

static void publish_encoders_state(const char *reason) {
  StaticJsonDocument<320> st;
  st["firmware_version"] = "clock_v8";
  st["reason"] = reason ? reason : "";
  st["bottom"] = encoderBottomCount;
  st["top_a"] = encoderTopCount;
  st["top_b"] = encoderTopBCount;
  sne_encoders.publishState(st);
}

static void publish_maglock_state(sentient_v8::Client &client, const char *name, bool open, const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "clock_v8";
  st["reason"] = reason ? reason : "";
  st["name"] = name;
  st["open"] = open;
  client.publishState(st);
}

static void publish_actuator_state(sentient_v8::Client &client, const char *name, bool unlocked, bool prox, const char *reason) {
  StaticJsonDocument<320> st;
  st["firmware_version"] = "clock_v8";
  st["reason"] = reason ? reason : "";
  st["name"] = name;
  st["unlocked"] = unlocked;
  st["prox"] = prox;
  client.publishState(st);
}

static void publish_simple_onoff(sentient_v8::Client &client, const char *name, bool on, const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "clock_v8";
  st["reason"] = reason ? reason : "";
  st["name"] = name;
  st["on"] = on;
  client.publishState(st);
}

static void publish_leds_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "clock_v8";
  st["reason"] = reason ? reason : "";
  st["brightness"] = led_brightness;
  st["mode"] = led_mode;
  st["color"] = led_color;
  sne_leds.publishState(st);
}

static void publish_all_state(const char *reason) {
  publish_stepper_state(sne_hour, "hour", hourPosition, hourTarget, reason);
  publish_stepper_state(sne_minute, "minute", minutePosition, minuteTarget, reason);
  publish_stepper_state(sne_gears, "gears", gearPosition, gearTarget, reason);
  publish_resistors_state(reason);
  publish_encoders_state(reason);
  publish_maglock_state(sne_operator_maglock, "operator", operator_maglock_open, reason);
  publish_maglock_state(sne_wood_maglock, "wood", wood_maglock_open, reason);
  publish_actuator_state(sne_metal_act, "metal", metal_door_unlocked, (digitalRead(METAL_DOOR_PROX_SENSOR) == HIGH), reason);
  publish_actuator_state(sne_exit_act, "exit", exit_door_unlocked, false, reason);
  publish_simple_onoff(sne_fog, "fog", fog_on, reason);
  publish_simple_onoff(sne_blacklight, "blacklight", blacklight_on, reason);
  publish_simple_onoff(sne_laser, "laser", laser_on, reason);
  publish_leds_state(reason);
}

static void set_maglock(int pin, bool open) { digitalWrite(pin, open ? LOW : HIGH); }
static void set_actuator(int pin_fwd, int pin_rwd, bool unlocked) {
  if (unlocked) {
    digitalWrite(pin_fwd, LOW);
    digitalWrite(pin_rwd, HIGH);
  } else {
    digitalWrite(pin_fwd, HIGH);
    digitalWrite(pin_rwd, LOW);
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

  if (ctx->kind == DeviceKind::HourHand || ctx->kind == DeviceKind::MinuteHand || ctx->kind == DeviceKind::Gears) {
    long *pos = (ctx->kind == DeviceKind::HourHand) ? &hourPosition : (ctx->kind == DeviceKind::MinuteHand) ? &minutePosition : &gearPosition;
    long *target = (ctx->kind == DeviceKind::HourHand) ? &hourTarget : (ctx->kind == DeviceKind::MinuteHand) ? &minuteTarget : &gearTarget;
    sentient_v8::Client *client = (ctx->kind == DeviceKind::HourHand) ? &sne_hour : (ctx->kind == DeviceKind::MinuteHand) ? &sne_minute : &sne_gears;
    const char *name = (ctx->kind == DeviceKind::HourHand) ? "hour" : (ctx->kind == DeviceKind::MinuteHand) ? "minute" : "gears";

    if (strcmp(op, "set_target") == 0) {
      if (!p.containsKey("target_steps")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      *target = (long)(p["target_steps"] | 0);
      publish_stepper_state(*client, name, *pos, *target, op);
      return true;
    }
    if (strcmp(op, "move") == 0) {
      if (!p.containsKey("delta_steps")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      long delta = (long)(p["delta_steps"] | 0);
      *target = *pos + delta;
      publish_stepper_state(*client, name, *pos, *target, op);
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      *target = *pos;
      publish_stepper_state(*client, name, *pos, *target, op);
      return true;
    }
    if (strcmp(op, "set_speed_us") == 0) {
      if (!p.containsKey("value")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      long v = p["value"] | 800;
      if (v < 200) v = 200;
      stepIntervalUs = (unsigned long)v;
      publish_stepper_state(*client, name, *pos, *target, op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::OperatorMaglock) {
    if (strcmp(op, "open") == 0) {
      operator_maglock_open = true;
      set_maglock(OPERATOR_MAGLOCK, true);
      publish_maglock_state(sne_operator_maglock, "operator", operator_maglock_open, op);
      return true;
    }
    if (strcmp(op, "close") == 0) {
      operator_maglock_open = false;
      set_maglock(OPERATOR_MAGLOCK, false);
      publish_maglock_state(sne_operator_maglock, "operator", operator_maglock_open, op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }
  if (ctx->kind == DeviceKind::WoodMaglock) {
    if (strcmp(op, "open") == 0) {
      wood_maglock_open = true;
      set_maglock(WOOD_DOOR_MAGLOCK, true);
      publish_maglock_state(sne_wood_maglock, "wood", wood_maglock_open, op);
      return true;
    }
    if (strcmp(op, "close") == 0) {
      wood_maglock_open = false;
      set_maglock(WOOD_DOOR_MAGLOCK, false);
      publish_maglock_state(sne_wood_maglock, "wood", wood_maglock_open, op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::MetalDoorActuator) {
    if (strcmp(op, "unlock") == 0) {
      metal_door_unlocked = true;
      set_actuator(METAL_DOOR_FWD, METAL_DOOR_RWD, true);
      publish_actuator_state(sne_metal_act, "metal", metal_door_unlocked, (digitalRead(METAL_DOOR_PROX_SENSOR) == HIGH), op);
      return true;
    }
    if (strcmp(op, "lock") == 0) {
      metal_door_unlocked = false;
      set_actuator(METAL_DOOR_FWD, METAL_DOOR_RWD, false);
      publish_actuator_state(sne_metal_act, "metal", metal_door_unlocked, (digitalRead(METAL_DOOR_PROX_SENSOR) == HIGH), op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }
  if (ctx->kind == DeviceKind::ExitDoorActuator) {
    if (strcmp(op, "unlock") == 0) {
      exit_door_unlocked = true;
      set_actuator(EXIT_DOOR_FWD, EXIT_DOOR_RWD, true);
      publish_actuator_state(sne_exit_act, "exit", exit_door_unlocked, false, op);
      return true;
    }
    if (strcmp(op, "lock") == 0) {
      exit_door_unlocked = false;
      set_actuator(EXIT_DOOR_FWD, EXIT_DOOR_RWD, false);
      publish_actuator_state(sne_exit_act, "exit", exit_door_unlocked, false, op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::FogPower) {
    if (strcmp(op, "on") == 0) {
      fog_on = true;
      digitalWrite(CLOCKFOGPOWER, LOW);
      publish_simple_onoff(sne_fog, "fog", fog_on, op);
      return true;
    }
    if (strcmp(op, "off") == 0) {
      fog_on = false;
      digitalWrite(CLOCKFOGPOWER, HIGH);
      publish_simple_onoff(sne_fog, "fog", fog_on, op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }
  if (ctx->kind == DeviceKind::Blacklight) {
    if (strcmp(op, "on") == 0) {
      blacklight_on = true;
      digitalWrite(BLACKLIGHT, HIGH);
      publish_simple_onoff(sne_blacklight, "blacklight", blacklight_on, op);
      return true;
    }
    if (strcmp(op, "off") == 0) {
      blacklight_on = false;
      digitalWrite(BLACKLIGHT, LOW);
      publish_simple_onoff(sne_blacklight, "blacklight", blacklight_on, op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }
  if (ctx->kind == DeviceKind::Laser) {
    if (strcmp(op, "on") == 0) {
      laser_on = true;
      digitalWrite(LASER_LIGHT, HIGH);
      publish_simple_onoff(sne_laser, "laser", laser_on, op);
      return true;
    }
    if (strcmp(op, "off") == 0) {
      laser_on = false;
      digitalWrite(LASER_LIGHT, LOW);
      publish_simple_onoff(sne_laser, "laser", laser_on, op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::LedStrips) {
    if (strcmp(op, "off") == 0) {
      led_mode = "off";
      led_color = "off";
      set_leds_solid(CRGB::Black);
      publish_leds_state(op);
      return true;
    }
    if (strcmp(op, "set_all_color") == 0) {
      const char *color = p["color"] | "";
      led_mode = "solid";
      led_color = color;
      set_leds_solid(color_from_name(color));
      publish_leds_state(op);
      return true;
    }
    if (strcmp(op, "set_brightness") == 0) {
      if (!p.containsKey("value")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      int v = p["value"] | 0;
      v = constrain(v, 0, 255);
      led_brightness = (uint8_t)v;
      FastLED.setBrightness(led_brightness);
      FastLED.show();
      publish_leds_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::CrankEncoders) {
    if (strcmp(op, "reset_counters") == 0) {
      noInterrupts();
      encoderBottomCount = 0;
      encoderTopCount = 0;
      encoderTopBCount = 0;
      interrupts();
      publish_encoders_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Sensor-only: operator_resistors
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
  pinMode(CLOCK_MINUTE_STEP_POS, OUTPUT);
  pinMode(CLOCK_MINUTE_STEP_NEG, OUTPUT);
  pinMode(CLOCK_MINUTE_DIR_POS, OUTPUT);
  pinMode(CLOCK_MINUTE_DIR_NEG, OUTPUT);
  pinMode(CLOCK_HOUR_STEP_POS, OUTPUT);
  pinMode(CLOCK_HOUR_STEP_NEG, OUTPUT);
  pinMode(CLOCK_HOUR_DIR_POS, OUTPUT);
  pinMode(CLOCK_HOUR_DIR_NEG, OUTPUT);
  pinMode(CLOCK_GEARS_STEP_POS, OUTPUT);
  pinMode(CLOCK_GEARS_STEP_NEG, OUTPUT);
  pinMode(CLOCK_GEARS_DIR_POS, OUTPUT);
  pinMode(CLOCK_GEARS_DIR_NEG, OUTPUT);
  pinMode(STEPPER_ENABLE, OUTPUT);
  digitalWrite(STEPPER_ENABLE, HIGH);

  // Initialize DM542 idle state (v2)
  digitalWrite(CLOCK_MINUTE_STEP_POS, LOW);
  digitalWrite(CLOCK_MINUTE_STEP_NEG, LOW);
  digitalWrite(CLOCK_MINUTE_DIR_POS, LOW);
  digitalWrite(CLOCK_MINUTE_DIR_NEG, HIGH);
  digitalWrite(CLOCK_HOUR_STEP_POS, LOW);
  digitalWrite(CLOCK_HOUR_STEP_NEG, LOW);
  digitalWrite(CLOCK_HOUR_DIR_POS, LOW);
  digitalWrite(CLOCK_HOUR_DIR_NEG, HIGH);
  digitalWrite(CLOCK_GEARS_STEP_POS, LOW);
  digitalWrite(CLOCK_GEARS_STEP_NEG, LOW);
  digitalWrite(CLOCK_GEARS_DIR_POS, LOW);
  digitalWrite(CLOCK_GEARS_DIR_NEG, HIGH);

  // Sensors
  for (int i = 0; i < 8; i++) pinMode(RESISTOR_PINS[i], INPUT);
  pinMode(ENCODER_BOTTOM_CLK, INPUT_PULLUP);
  pinMode(ENCODER_BOTTOM_DT, INPUT_PULLUP);
  pinMode(ENCODER_TOPA_CLK, INPUT_PULLUP);
  pinMode(ENCODER_TOPA_DT, INPUT_PULLUP);
  pinMode(ENCODER_TOPB_CLK, INPUT_PULLUP);
  pinMode(ENCODER_TOPB_DT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_BOTTOM_CLK), encoderBottomCLKISR, RISING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_BOTTOM_DT), encoderBottomDTISR, RISING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_TOPA_CLK), encoderTopACLKISR, RISING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_TOPA_DT), encoderTopADTISR, RISING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_TOPB_CLK), encoderTopBCLKISR, RISING);
  attachInterrupt(digitalPinToInterrupt(ENCODER_TOPB_DT), encoderTopBDTISR, RISING);

  pinMode(METAL_DOOR_PROX_SENSOR, INPUT);

  // Outputs
  pinMode(OPERATOR_MAGLOCK, OUTPUT);
  pinMode(WOOD_DOOR_MAGLOCK, OUTPUT);
  set_maglock(OPERATOR_MAGLOCK, false);
  set_maglock(WOOD_DOOR_MAGLOCK, false);

  pinMode(EXIT_DOOR_FWD, OUTPUT);
  pinMode(EXIT_DOOR_RWD, OUTPUT);
  pinMode(METAL_DOOR_FWD, OUTPUT);
  pinMode(METAL_DOOR_RWD, OUTPUT);
  set_actuator(EXIT_DOOR_FWD, EXIT_DOOR_RWD, false);
  set_actuator(METAL_DOOR_FWD, METAL_DOOR_RWD, false);

  pinMode(CLOCKFOGPOWER, OUTPUT);
  digitalWrite(CLOCKFOGPOWER, HIGH);
  pinMode(BLACKLIGHT, OUTPUT);
  digitalWrite(BLACKLIGHT, LOW);
  pinMode(LASER_LIGHT, OUTPUT);
  digitalWrite(LASER_LIGHT, LOW);

  // LEDs
  FastLED.addLeds<WS2812B, LED_LEFT_UPPER, GRB>(leds_left_upper, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_LEFT_LOWER, GRB>(leds_left_lower, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_RIGHT_UPPER, GRB>(leds_right_upper, NUM_LEDS_PER_STRIP);
  FastLED.addLeds<WS2812B, LED_RIGHT_LOWER, GRB>(leds_right_lower, NUM_LEDS_PER_STRIP);
  FastLED.setBrightness(led_brightness);
  set_leds_solid(CRGB::Blue);

  if (!sne_hour.begin()) while (true) delay(1000);
  if (!sne_minute.begin()) while (true) delay(1000);
  if (!sne_gears.begin()) while (true) delay(1000);
  if (!sne_resistors.begin()) while (true) delay(1000);
  if (!sne_encoders.begin()) while (true) delay(1000);
  if (!sne_operator_maglock.begin()) while (true) delay(1000);
  if (!sne_wood_maglock.begin()) while (true) delay(1000);
  if (!sne_metal_act.begin()) while (true) delay(1000);
  if (!sne_exit_act.begin()) while (true) delay(1000);
  if (!sne_fog.begin()) while (true) delay(1000);
  if (!sne_blacklight.begin()) while (true) delay(1000);
  if (!sne_laser.begin()) while (true) delay(1000);
  if (!sne_leds.begin()) while (true) delay(1000);

  sne_hour.setCommandHandler(handleCommand, &ctx_hour);
  sne_minute.setCommandHandler(handleCommand, &ctx_minute);
  sne_gears.setCommandHandler(handleCommand, &ctx_gears);
  sne_resistors.setCommandHandler(handleCommand, &ctx_resistors);
  sne_encoders.setCommandHandler(handleCommand, &ctx_encoders);
  sne_operator_maglock.setCommandHandler(handleCommand, &ctx_operator_maglock);
  sne_wood_maglock.setCommandHandler(handleCommand, &ctx_wood_maglock);
  sne_metal_act.setCommandHandler(handleCommand, &ctx_metal);
  sne_exit_act.setCommandHandler(handleCommand, &ctx_exit);
  sne_fog.setCommandHandler(handleCommand, &ctx_fog);
  sne_blacklight.setCommandHandler(handleCommand, &ctx_black);
  sne_laser.setCommandHandler(handleCommand, &ctx_laser);
  sne_leds.setCommandHandler(handleCommand, &ctx_leds);

  publish_all_state("boot");
  lastStepTimeUs = micros();
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  runSteppers();

  static unsigned long last_publish = 0;
  const unsigned long now = millis();
  if (now - last_publish >= 1000UL) {
    publish_stepper_state(sne_hour, "hour", hourPosition, hourTarget, "periodic");
    publish_stepper_state(sne_minute, "minute", minutePosition, minuteTarget, "periodic");
    publish_stepper_state(sne_gears, "gears", gearPosition, gearTarget, "periodic");
    publish_resistors_state("periodic");
    publish_encoders_state("periodic");
    publish_actuator_state(sne_metal_act, "metal", metal_door_unlocked, (digitalRead(METAL_DOOR_PROX_SENSOR) == HIGH), "periodic");
    publish_leds_state("periodic");
    last_publish = now;
  }
}
