// floor_v8 — Floor Puzzle Controller (Teensy 4.1)
//
// This v8 port removes embedded v2 sequence/game logic. Firmware is a stateless
// hardware executor + sensor publisher; sequencing/validation lives in core.
//
// Hardware (from v2):
// - 9 floor buttons (INPUT_PULLUP)
// - 9 WS2812B strips (60 LEDs each)
// - 1 DM542 stepper (drawer/lever motor) via differential-ish pins
// - drawer maglock (active LOW), cuckoo solenoid (pulse), drawer COB lights, crystal light
// - 4 proximity sensors for drawer open/close (digital)
// - IR receiver (IRremote), photocell (analog)
// - lever RGB LED (1 WS2812 pixel)

#include <Arduino.h>
#include <ArduinoJson.h>

#include <FastLED.h>
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>

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

static const int PIN_DRAWER_MAGLOCK = 14;      // active LOW
static const int PIN_CUCKOO_SOLENOID = 18;     // active HIGH pulse
static const int PIN_DRAWER_COB_LIGHTS = 15;   // active HIGH
static const int PIN_CRYSTAL_LIGHT = 37;       // active HIGH
static const int PIN_PHOTOCELL = A16;          // pin 40
static const int IR_RECEIVE_PIN = 12;

static const int PIN_DRAWER_OPENED_MAIN = 35;
static const int PIN_DRAWER_OPENED_SUB = 36;
static const int PIN_DRAWER_CLOSED_MAIN = 33;
static const int PIN_DRAWER_CLOSED_SUB = 34;

// Motor pins (custom pulse generator, ported from v2)
static const int PIN_LEVER_RGB_LED = 10; // 1 WS2812 pixel
static const int PIN_MOTOR_PUL_POS = 22;
static const int PIN_MOTOR_PUL_NEG = 23;
static const int PIN_MOTOR_DIR_POS = 21;
static const int PIN_MOTOR_DIR_NEG = 20;

// 9 LED strip pins
static const int STRIP_PINS[9] = {3, 0, 6, 4, 1, 7, 5, 2, 8};
static const int NUM_STRIPS = 9;
static const int LEDS_PER_STRIP = 60;
static const int TOTAL_LEDS = NUM_STRIPS * LEDS_PER_STRIP;
static CRGB leds[TOTAL_LEDS];

static CRGB lever_led[1];

// 9 buttons
static const int BUTTON_PINS[9] = {27, 24, 30, 28, 25, 32, 29, 26, 31};

// --- v8 Clients ---
static sentient_v8::Client sne_floor_buttons(make_cfg("floor_floor_buttons", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_floor_leds(make_cfg("floor_floor_leds", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_drawer_maglock(make_cfg("floor_drawer_maglock", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_cuckoo_solenoid(make_cfg("floor_cuckoo_solenoid", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_drawer_cob(make_cfg("floor_drawer_cob_lights", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_crystal(make_cfg("floor_crystal_light", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_lever_motor(make_cfg("floor_lever_motor", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_lever_rgb(make_cfg("floor_lever_rgb_led", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_ir(make_cfg("floor_ir_sensor", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_photocell(make_cfg("floor_photocell", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_drawer_prox(make_cfg("floor_drawer_proximity", HMAC_KEY_PLACEHOLDER));

static sentient_v8::Client *clients[] = {&sne_floor_buttons, &sne_floor_leds, &sne_drawer_maglock, &sne_cuckoo_solenoid, &sne_drawer_cob,
                                         &sne_crystal, &sne_lever_motor, &sne_lever_rgb, &sne_ir, &sne_photocell, &sne_drawer_prox};

enum class DeviceKind : uint8_t {
  Buttons,
  FloorLeds,
  DrawerMaglock,
  CuckooSolenoid,
  DrawerCobLights,
  CrystalLight,
  LeverMotor,
  LeverRgbLed,
  IrSensor,
  Photocell,
  DrawerProximity,
};

struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_buttons = {DeviceKind::Buttons};
static DeviceCtx ctx_leds = {DeviceKind::FloorLeds};
static DeviceCtx ctx_maglock = {DeviceKind::DrawerMaglock};
static DeviceCtx ctx_solenoid = {DeviceKind::CuckooSolenoid};
static DeviceCtx ctx_cob = {DeviceKind::DrawerCobLights};
static DeviceCtx ctx_crystal = {DeviceKind::CrystalLight};
static DeviceCtx ctx_motor = {DeviceKind::LeverMotor};
static DeviceCtx ctx_lever_rgb = {DeviceKind::LeverRgbLed};
static DeviceCtx ctx_ir = {DeviceKind::IrSensor};
static DeviceCtx ctx_photocell = {DeviceKind::Photocell};
static DeviceCtx ctx_drawer_prox = {DeviceKind::DrawerProximity};

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

// --- Outputs state ---
static bool drawer_maglock_locked = false; // active LOW lock; v2 default HIGH (unlocked)
static bool drawer_cob_on = false;
static bool crystal_on = true; // v2 boot default HIGH
static bool solenoid_active = false;
static unsigned long solenoid_start_ms = 0;
static const unsigned long SOLENOID_DURATION_MS = 500;

// --- Motor state (ported from v2) ---
static bool motor_is_moving = false;
static bool motor_move_direction_open = true;  // true=open, false=close
static unsigned long motor_last_step_us = 0;
static unsigned long motor_step_interval_us = 1000; // 1ms = 1000 steps/sec
static long motor_position = 0;

static bool last_button_pressed[9] = {false};
static uint8_t leds_brightness = 200;
static const char *floor_led_mode = "off";
static const char *floor_led_color = "off";

static uint32_t last_ir_raw = 0;
static uint16_t last_ir_command = 0;
static bool have_ir = false;

static int last_photocell_raw = -1;

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

static void floor_leds_show() { FastLED.show(); }

static void floor_leds_set_all(CRGB c) {
  fill_solid(leds, TOTAL_LEDS, c);
  floor_leds_show();
}

static void floor_leds_set_strip(int strip_idx_0, CRGB c) {
  if (strip_idx_0 < 0 || strip_idx_0 >= NUM_STRIPS) return;
  int start = strip_idx_0 * LEDS_PER_STRIP;
  fill_solid(leds + start, LEDS_PER_STRIP, c);
  floor_leds_show();
}

static void lever_led_set(CRGB c) {
  lever_led[0] = c;
  FastLED.show();
}

static void publish_buttons_state(const char *reason) {
  StaticJsonDocument<512> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  JsonArray pressed = st.createNestedArray("pressed");
  for (int i = 0; i < 9; i++) pressed.add(last_button_pressed[i]);
  sne_floor_buttons.publishState(st);
}

static void publish_floor_leds_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["brightness"] = leds_brightness;
  st["mode"] = floor_led_mode;
  st["color"] = floor_led_color;
  sne_floor_leds.publishState(st);
}

static void publish_maglock_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["locked"] = drawer_maglock_locked;
  sne_drawer_maglock.publishState(st);
}

static void publish_solenoid_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["active"] = solenoid_active;
  sne_cuckoo_solenoid.publishState(st);
}

static void publish_cob_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["on"] = drawer_cob_on;
  sne_drawer_cob.publishState(st);
}

static void publish_crystal_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["on"] = crystal_on;
  sne_crystal.publishState(st);
}

static void publish_motor_state(const char *reason) {
  StaticJsonDocument<384> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["moving"] = motor_is_moving;
  st["direction"] = motor_move_direction_open ? "open" : "close";
  st["step_interval_us"] = motor_step_interval_us;
  st["position"] = motor_position;
  sne_lever_motor.publishState(st);
}

static void publish_lever_rgb_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["color"] = "white";
  sne_lever_rgb.publishState(st);
}

static void publish_ir_state(const char *reason) {
  StaticJsonDocument<320> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["have_code"] = have_ir;
  st["raw"] = (uint32_t)last_ir_raw;
  st["command"] = (uint16_t)last_ir_command;
  sne_ir.publishState(st);
}

static void publish_photocell_state(const char *reason, int raw) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["raw"] = raw;
  sne_photocell.publishState(st);
}

static void publish_drawer_prox_state(const char *reason) {
  bool opened_main = (digitalRead(PIN_DRAWER_OPENED_MAIN) == HIGH);
  bool opened_sub = (digitalRead(PIN_DRAWER_OPENED_SUB) == HIGH);
  bool closed_main = (digitalRead(PIN_DRAWER_CLOSED_MAIN) == HIGH);
  bool closed_sub = (digitalRead(PIN_DRAWER_CLOSED_SUB) == HIGH);

  StaticJsonDocument<256> st;
  st["firmware_version"] = "floor_v8";
  st["reason"] = reason ? reason : "";
  st["opened_main"] = opened_main;
  st["opened_sub"] = opened_sub;
  st["closed_main"] = closed_main;
  st["closed_sub"] = closed_sub;
  sne_drawer_prox.publishState(st);
}

static void publish_all_state(const char *reason) {
  publish_buttons_state(reason);
  publish_floor_leds_state(reason);
  publish_maglock_state(reason);
  publish_solenoid_state(reason);
  publish_cob_state(reason);
  publish_crystal_state(reason);
  publish_motor_state(reason);
  publish_lever_rgb_state(reason);
  publish_ir_state(reason);
  publish_photocell_state(reason, last_photocell_raw);
  publish_drawer_prox_state(reason);
}

static void set_drawer_maglock(bool locked) {
  drawer_maglock_locked = locked;
  digitalWrite(PIN_DRAWER_MAGLOCK, locked ? LOW : HIGH);
}

static void set_cob(bool on) {
  drawer_cob_on = on;
  digitalWrite(PIN_DRAWER_COB_LIGHTS, on ? HIGH : LOW);
}

static void set_crystal(bool on) {
  crystal_on = on;
  digitalWrite(PIN_CRYSTAL_LIGHT, on ? HIGH : LOW);
}

static void start_solenoid_pulse() {
  digitalWrite(PIN_CUCKOO_SOLENOID, HIGH);
  solenoid_start_ms = millis();
  solenoid_active = true;
}

static void stop_solenoid() {
  digitalWrite(PIN_CUCKOO_SOLENOID, LOW);
  solenoid_active = false;
}

static bool proximity_hit_for_direction() {
  bool opened_main = (digitalRead(PIN_DRAWER_OPENED_MAIN) == HIGH);
  bool opened_sub = (digitalRead(PIN_DRAWER_OPENED_SUB) == HIGH);
  bool closed_main = (digitalRead(PIN_DRAWER_CLOSED_MAIN) == HIGH);
  bool closed_sub = (digitalRead(PIN_DRAWER_CLOSED_SUB) == HIGH);
  if (motor_move_direction_open) return opened_main || opened_sub;
  return closed_main || closed_sub;
}

static void motor_step_tick() {
  if (!motor_is_moving) return;
  unsigned long now = micros();
  if (now - motor_last_step_us < motor_step_interval_us) return;

  // Direction pins (ported from v2: reversed logic on DIRPOS)
  digitalWrite(PIN_MOTOR_DIR_POS, motor_move_direction_open ? LOW : HIGH);
  digitalWrite(PIN_MOTOR_DIR_NEG, motor_move_direction_open ? HIGH : LOW);

  digitalWrite(PIN_MOTOR_PUL_POS, HIGH);
  digitalWrite(PIN_MOTOR_PUL_NEG, LOW);
  delayMicroseconds(10);
  digitalWrite(PIN_MOTOR_PUL_POS, LOW);
  digitalWrite(PIN_MOTOR_PUL_NEG, LOW);

  motor_position += motor_move_direction_open ? 1 : -1;
  motor_last_step_us = now;

  if (proximity_hit_for_direction()) {
    motor_is_moving = false;
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

  if (ctx->kind == DeviceKind::DrawerMaglock) {
    if (strcmp(op, "lock") == 0) {
      set_drawer_maglock(true);
      publish_maglock_state(op);
      return true;
    }
    if (strcmp(op, "unlock") == 0) {
      set_drawer_maglock(false);
      publish_maglock_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::CuckooSolenoid) {
    if (strcmp(op, "activate") == 0 || strcmp(op, "pulse") == 0) {
      start_solenoid_pulse();
      publish_solenoid_state(op);
      return true;
    }
    if (strcmp(op, "off") == 0) {
      stop_solenoid();
      publish_solenoid_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::DrawerCobLights) {
    if (strcmp(op, "on") == 0) {
      set_cob(true);
      publish_cob_state(op);
      return true;
    }
    if (strcmp(op, "off") == 0) {
      set_cob(false);
      publish_cob_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::CrystalLight) {
    if (strcmp(op, "on") == 0) {
      set_crystal(true);
      publish_crystal_state(op);
      return true;
    }
    if (strcmp(op, "off") == 0) {
      set_crystal(false);
      publish_crystal_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::LeverMotor) {
    if (strcmp(op, "open") == 0) {
      motor_move_direction_open = true;
      motor_is_moving = true;
      motor_last_step_us = micros();
      publish_motor_state(op);
      return true;
    }
    if (strcmp(op, "close") == 0) {
      motor_move_direction_open = false;
      motor_is_moving = true;
      motor_last_step_us = micros();
      publish_motor_state(op);
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      motor_is_moving = false;
      publish_motor_state(op);
      return true;
    }
    if (strcmp(op, "set_speed_us") == 0) {
      if (!p.containsKey("value")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      long v = p["value"] | 1000;
      if (v < 200) v = 200;
      motor_step_interval_us = (unsigned long)v;
      publish_motor_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::LeverRgbLed) {
    if (strcmp(op, "set_color") == 0) {
      const char *color = p["color"] | "";
      lever_led_set(color_from_name(color));
      publish_lever_rgb_state(op);
      return true;
    }
    if (strcmp(op, "off") == 0) {
      lever_led_set(CRGB::Black);
      publish_lever_rgb_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::FloorLeds) {
    if (strcmp(op, "off") == 0) {
      floor_led_mode = "off";
      floor_led_color = "off";
      floor_leds_set_all(CRGB::Black);
      publish_floor_leds_state(op);
      return true;
    }
    if (strcmp(op, "set_all_color") == 0) {
      const char *color = p["color"] | "";
      floor_led_mode = "solid";
      floor_led_color = color;
      floor_leds_set_all(color_from_name(color));
      publish_floor_leds_state(op);
      return true;
    }
    if (strcmp(op, "set_strip_color") == 0) {
      if (!p.containsKey("strip") || !p.containsKey("color")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      int strip = p["strip"] | 1;
      const char *color = p["color"] | "";
      strip = constrain(strip, 1, 9);
      floor_led_mode = "per_strip";
      floor_led_color = "mixed";
      floor_leds_set_strip(strip - 1, color_from_name(color));
      publish_floor_leds_state(op);
      return true;
    }
    if (strcmp(op, "set_brightness") == 0) {
      if (!p.containsKey("value")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      int v = p["value"] | 0;
      v = constrain(v, 0, 255);
      leds_brightness = (uint8_t)v;
      FastLED.setBrightness(leds_brightness);
      floor_leds_show();
      publish_floor_leds_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Sensor-only devices
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);
  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  // Outputs defaults (match v2)
  pinMode(PIN_DRAWER_MAGLOCK, OUTPUT);
  pinMode(PIN_CUCKOO_SOLENOID, OUTPUT);
  pinMode(PIN_DRAWER_COB_LIGHTS, OUTPUT);
  pinMode(PIN_CRYSTAL_LIGHT, OUTPUT);

  digitalWrite(PIN_CUCKOO_SOLENOID, LOW);
  set_cob(false);
  set_crystal(true);
  set_drawer_maglock(false);

  // Motor pins
  pinMode(PIN_MOTOR_PUL_POS, OUTPUT);
  pinMode(PIN_MOTOR_PUL_NEG, OUTPUT);
  pinMode(PIN_MOTOR_DIR_POS, OUTPUT);
  pinMode(PIN_MOTOR_DIR_NEG, OUTPUT);
  digitalWrite(PIN_MOTOR_PUL_POS, LOW);
  digitalWrite(PIN_MOTOR_PUL_NEG, LOW);
  digitalWrite(PIN_MOTOR_DIR_POS, LOW);
  digitalWrite(PIN_MOTOR_DIR_NEG, HIGH);

  // Sensors
  for (int i = 0; i < 9; i++) pinMode(BUTTON_PINS[i], INPUT_PULLUP);
  pinMode(PIN_DRAWER_OPENED_MAIN, INPUT_PULLUP);
  pinMode(PIN_DRAWER_OPENED_SUB, INPUT_PULLUP);
  pinMode(PIN_DRAWER_CLOSED_MAIN, INPUT_PULLUP);
  pinMode(PIN_DRAWER_CLOSED_SUB, INPUT_PULLUP);

  pinMode(PIN_PHOTOCELL, INPUT);

  // FastLED: 9 strips + lever LED
  FastLED.addLeds<WS2812, 3, GRB>(leds, 0 * LEDS_PER_STRIP, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812, 0, GRB>(leds, 1 * LEDS_PER_STRIP, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812, 6, GRB>(leds, 2 * LEDS_PER_STRIP, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812, 4, GRB>(leds, 3 * LEDS_PER_STRIP, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812, 1, GRB>(leds, 4 * LEDS_PER_STRIP, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812, 7, GRB>(leds, 5 * LEDS_PER_STRIP, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812, 5, GRB>(leds, 6 * LEDS_PER_STRIP, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812, 2, GRB>(leds, 7 * LEDS_PER_STRIP, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812, 8, GRB>(leds, 8 * LEDS_PER_STRIP, LEDS_PER_STRIP);
  FastLED.addLeds<WS2812, PIN_LEVER_RGB_LED, GRB>(lever_led, 1);

  FastLED.setBrightness(leds_brightness);
  floor_leds_set_all(CRGB::Black);
  lever_led_set(CRGB::White);

  // IR
  IrReceiver.begin(IR_RECEIVE_PIN, false);

  // Read initial states
  for (int i = 0; i < 9; i++) last_button_pressed[i] = (digitalRead(BUTTON_PINS[i]) == LOW);
  last_photocell_raw = analogRead(PIN_PHOTOCELL);

  if (!sne_floor_buttons.begin()) while (true) delay(1000);
  if (!sne_floor_leds.begin()) while (true) delay(1000);
  if (!sne_drawer_maglock.begin()) while (true) delay(1000);
  if (!sne_cuckoo_solenoid.begin()) while (true) delay(1000);
  if (!sne_drawer_cob.begin()) while (true) delay(1000);
  if (!sne_crystal.begin()) while (true) delay(1000);
  if (!sne_lever_motor.begin()) while (true) delay(1000);
  if (!sne_lever_rgb.begin()) while (true) delay(1000);
  if (!sne_ir.begin()) while (true) delay(1000);
  if (!sne_photocell.begin()) while (true) delay(1000);
  if (!sne_drawer_prox.begin()) while (true) delay(1000);

  sne_floor_buttons.setCommandHandler(handleCommand, &ctx_buttons);
  sne_floor_leds.setCommandHandler(handleCommand, &ctx_leds);
  sne_drawer_maglock.setCommandHandler(handleCommand, &ctx_maglock);
  sne_cuckoo_solenoid.setCommandHandler(handleCommand, &ctx_solenoid);
  sne_drawer_cob.setCommandHandler(handleCommand, &ctx_cob);
  sne_crystal.setCommandHandler(handleCommand, &ctx_crystal);
  sne_lever_motor.setCommandHandler(handleCommand, &ctx_motor);
  sne_lever_rgb.setCommandHandler(handleCommand, &ctx_lever_rgb);
  sne_ir.setCommandHandler(handleCommand, &ctx_ir);
  sne_photocell.setCommandHandler(handleCommand, &ctx_photocell);
  sne_drawer_prox.setCommandHandler(handleCommand, &ctx_drawer_prox);

  publish_all_state("boot");
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  // Motor stepping + auto-stop by proximity
  motor_step_tick();

  // Solenoid timing
  if (solenoid_active && (millis() - solenoid_start_ms >= SOLENOID_DURATION_MS)) {
    stop_solenoid();
    publish_solenoid_state("timeout");
  }

  // Sensors: buttons
  bool any_button_change = false;
  for (int i = 0; i < 9; i++) {
    bool pressed = (digitalRead(BUTTON_PINS[i]) == LOW);
    if (pressed != last_button_pressed[i]) {
      last_button_pressed[i] = pressed;
      any_button_change = true;
    }
  }
  if (any_button_change) publish_buttons_state("change");

  // IR
  if (IrReceiver.decode()) {
    have_ir = true;
    last_ir_raw = (uint32_t)IrReceiver.decodedIRData.decodedRawData;
    last_ir_command = (uint16_t)IrReceiver.decodedIRData.command;
    publish_ir_state("ir");
    IrReceiver.resume();
  }

  // Photocell
  int raw = analogRead(PIN_PHOTOCELL);
  if (last_photocell_raw < 0 || abs(raw - last_photocell_raw) > 10) {
    last_photocell_raw = raw;
    publish_photocell_state("change", raw);
  }

  // Drawer proximity
  static unsigned long last_prox_publish = 0;
  const unsigned long now = millis();
  if (now - last_prox_publish > 500UL) {
    publish_drawer_prox_state("periodic");
    last_prox_publish = now;
  }

  // Periodic full state
  static unsigned long last_periodic = 0;
  if (now - last_periodic > 60UL * 1000UL) {
    publish_all_state("periodic");
    last_periodic = now;
  }
}
