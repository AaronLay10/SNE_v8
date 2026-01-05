// riddle_v8 — Riddle Puzzle Controller (Teensy 4.1)
//
// This v8 port removes embedded v2 game logic. Firmware is a stateless
// hardware executor + sensor publisher; sequencing/validation lives in core.
//
// Hardware (from v2):
// - 2x FULL4WIRE steppers (door lift) + 4 endstops
// - 1 maglock (lever)
// - 50px NeoPixel strip (knob LEDs)
// - knob puzzle input matrix (pullups)
// - 3 clue buttons (pullups)
//
// v8 behavior:
// - Option 2 device identity: one v8 `device_id` per logical device
// - One MQTT connection per `device_id`
// - Commands are `action="SET"` and require `parameters.op` (string)

#include <Arduino.h>
#include <ArduinoJson.h>

#include <Adafruit_NeoPixel.h>
#include <AccelStepper.h>

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
static const int PIN_LED_STRIP = 29;
static const int NUM_LEDS = 50;

// Knob pins
static const int PIN_KNOB_A2 = 20;
static const int PIN_KNOB_A3 = 19;
static const int PIN_B1 = 2;
static const int PIN_B2 = 4;
static const int PIN_B3 = 5;
static const int PIN_B4 = 3;
static const int PIN_C1 = 16;
static const int PIN_C2 = 17;
static const int PIN_C3 = 18;
static const int PIN_C4 = 15;
static const int PIN_D1 = 40;
static const int PIN_D2 = 39;
static const int PIN_D3 = 37;
static const int PIN_D4 = 38;
static const int PIN_E1 = 1;
static const int PIN_E2 = 7;
static const int PIN_E3 = 0;
static const int PIN_E4 = 6;
static const int PIN_F1 = 35;
static const int PIN_F2 = 33;
static const int PIN_F3 = 34;
static const int PIN_F4 = 36;
static const int PIN_G1 = 22;
static const int PIN_G4 = 23;

// Buttons
static const int PIN_BUTTON_1 = 31;
static const int PIN_BUTTON_2 = 30;
static const int PIN_BUTTON_3 = 32;

// Endstops
static const int PIN_ENDSTOP_UP_R = 28;
static const int PIN_ENDSTOP_UP_L = 8;
static const int PIN_ENDSTOP_DN_R = 41;
static const int PIN_ENDSTOP_DN_L = 21;

// Maglock
static const int PIN_MAGLOCK = 42;

// Stepper pins
static const int PIN_STEPPER1_1 = 24;
static const int PIN_STEPPER1_2 = 25;
static const int PIN_STEPPER1_3 = 26;
static const int PIN_STEPPER1_4 = 27;
static const int PIN_STEPPER2_1 = 9;
static const int PIN_STEPPER2_2 = 10;
static const int PIN_STEPPER2_3 = 11;
static const int PIN_STEPPER2_4 = 12;

// --- Hardware objects ---
static Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED_STRIP, NEO_GRB + NEO_KHZ800);
static AccelStepper stepper_one(AccelStepper::FULL4WIRE, PIN_STEPPER1_1, PIN_STEPPER1_2, PIN_STEPPER1_3, PIN_STEPPER1_4);
static AccelStepper stepper_two(AccelStepper::FULL4WIRE, PIN_STEPPER2_1, PIN_STEPPER2_2, PIN_STEPPER2_3, PIN_STEPPER2_4);

static bool maglock_locked = true;
static int door_direction = 0;  // 0=stopped, 1=up, -1=down
static bool motors_running = false;
static bool motor_speed_set = false;
static int door_location = 0;  // 1=closed, 2=open, 3=between

static uint8_t leds_brightness = 50;
static bool leds_on = true;

// --- v8 Clients ---
static sentient_v8::Client sne_riddle_door_lift(make_cfg("riddle_door_lift", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_riddle_lever_maglock(make_cfg("riddle_lever_maglock", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_riddle_knob_leds(make_cfg("riddle_knob_leds", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_riddle_knob_puzzle(make_cfg("riddle_knob_puzzle", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client sne_riddle_clue_buttons(make_cfg("riddle_clue_buttons", HMAC_KEY_PLACEHOLDER));
static sentient_v8::Client *clients[] = {&sne_riddle_door_lift, &sne_riddle_lever_maglock, &sne_riddle_knob_leds, &sne_riddle_knob_puzzle, &sne_riddle_clue_buttons};

enum class DeviceKind : uint8_t { Door, Maglock, KnobLeds, KnobPuzzle, Buttons };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_door = {DeviceKind::Door};
static DeviceCtx ctx_maglock = {DeviceKind::Maglock};
static DeviceCtx ctx_knob_leds = {DeviceKind::KnobLeds};
static DeviceCtx ctx_knob_puzzle = {DeviceKind::KnobPuzzle};
static DeviceCtx ctx_buttons = {DeviceKind::Buttons};

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

static void update_door_location() {
  bool endstop_one_up = (digitalRead(PIN_ENDSTOP_UP_R) == LOW);
  bool endstop_one_dn = (digitalRead(PIN_ENDSTOP_DN_R) == LOW);
  bool endstop_two_up = (digitalRead(PIN_ENDSTOP_UP_L) == LOW);
  bool endstop_two_dn = (digitalRead(PIN_ENDSTOP_DN_L) == LOW);

  if (!endstop_one_up && !endstop_one_dn && !endstop_two_up && !endstop_two_dn) door_location = 3;
  else if (endstop_one_dn || endstop_two_dn) door_location = 1;
  else if (endstop_one_up || endstop_two_up) door_location = 2;
}

static void run_motors() {
  bool endstop_one_up = (digitalRead(PIN_ENDSTOP_UP_R) == LOW);
  bool endstop_one_dn = (digitalRead(PIN_ENDSTOP_DN_R) == LOW);
  bool endstop_two_up = (digitalRead(PIN_ENDSTOP_UP_L) == LOW);
  bool endstop_two_dn = (digitalRead(PIN_ENDSTOP_DN_L) == LOW);

  if (door_direction != 0 && !motors_running) {
    if (door_direction == 1 && !endstop_one_up && !endstop_two_up) {
      motors_running = true;
      motor_speed_set = false;
    } else if (door_direction == -1 && !endstop_one_dn && !endstop_two_dn) {
      motors_running = true;
      motor_speed_set = false;
    } else {
      door_direction = 0;
    }
  }

  if (motors_running) {
    if (door_direction == 1 && (endstop_one_up || endstop_two_up)) {
      stepper_one.stop();
      stepper_two.stop();
      motors_running = false;
      motor_speed_set = false;
      door_direction = 0;
    } else if (door_direction == -1 && (endstop_one_dn || endstop_two_dn)) {
      stepper_one.stop();
      stepper_two.stop();
      motors_running = false;
      motor_speed_set = false;
      door_direction = 0;
    } else {
      if (!motor_speed_set) {
        stepper_one.setSpeed(door_direction == 1 ? 1000 : -1000);
        stepper_two.setSpeed(door_direction == 1 ? 1000 : -1000);
        motor_speed_set = true;
      }
      stepper_one.runSpeed();
      stepper_two.runSpeed();
    }
  }
}

static void publish_door_state(const char *reason) {
  update_door_location();
  StaticJsonDocument<384> st;
  st["firmware_version"] = "riddle_v8";
  st["reason"] = reason ? reason : "";
  st["door_direction"] = door_direction;
  st["motors_running"] = motors_running;
  st["door_location"] = door_location;
  st["endstop_up_r"] = (digitalRead(PIN_ENDSTOP_UP_R) == LOW);
  st["endstop_up_l"] = (digitalRead(PIN_ENDSTOP_UP_L) == LOW);
  st["endstop_dn_r"] = (digitalRead(PIN_ENDSTOP_DN_R) == LOW);
  st["endstop_dn_l"] = (digitalRead(PIN_ENDSTOP_DN_L) == LOW);
  sne_riddle_door_lift.publishState(st);
}

static void publish_maglock_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "riddle_v8";
  st["reason"] = reason ? reason : "";
  st["locked"] = maglock_locked;
  sne_riddle_lever_maglock.publishState(st);
}

static void publish_knob_leds_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "riddle_v8";
  st["reason"] = reason ? reason : "";
  st["on"] = leds_on;
  st["brightness"] = leds_brightness;
  sne_riddle_knob_leds.publishState(st);
}

static void publish_knob_puzzle_state(const char *reason) {
  StaticJsonDocument<768> st;
  st["firmware_version"] = "riddle_v8";
  st["reason"] = reason ? reason : "";
  JsonObject pins = st.createNestedObject("pins");
  pins["a2"] = (digitalRead(PIN_KNOB_A2) == LOW);
  pins["a3"] = (digitalRead(PIN_KNOB_A3) == LOW);
  pins["b1"] = (digitalRead(PIN_B1) == LOW);
  pins["b2"] = (digitalRead(PIN_B2) == LOW);
  pins["b3"] = (digitalRead(PIN_B3) == LOW);
  pins["b4"] = (digitalRead(PIN_B4) == LOW);
  pins["c1"] = (digitalRead(PIN_C1) == LOW);
  pins["c2"] = (digitalRead(PIN_C2) == LOW);
  pins["c3"] = (digitalRead(PIN_C3) == LOW);
  pins["c4"] = (digitalRead(PIN_C4) == LOW);
  pins["d1"] = (digitalRead(PIN_D1) == LOW);
  pins["d2"] = (digitalRead(PIN_D2) == LOW);
  pins["d3"] = (digitalRead(PIN_D3) == LOW);
  pins["d4"] = (digitalRead(PIN_D4) == LOW);
  pins["e1"] = (digitalRead(PIN_E1) == LOW);
  pins["e2"] = (digitalRead(PIN_E2) == LOW);
  pins["e3"] = (digitalRead(PIN_E3) == LOW);
  pins["e4"] = (digitalRead(PIN_E4) == LOW);
  pins["f1"] = (digitalRead(PIN_F1) == LOW);
  pins["f2"] = (digitalRead(PIN_F2) == LOW);
  pins["f3"] = (digitalRead(PIN_F3) == LOW);
  pins["f4"] = (digitalRead(PIN_F4) == LOW);
  pins["g1"] = (digitalRead(PIN_G1) == LOW);
  pins["g4"] = (digitalRead(PIN_G4) == LOW);
  sne_riddle_knob_puzzle.publishState(st);
}

static void publish_buttons_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "riddle_v8";
  st["reason"] = reason ? reason : "";
  st["button_1"] = (digitalRead(PIN_BUTTON_1) == LOW);
  st["button_2"] = (digitalRead(PIN_BUTTON_2) == LOW);
  st["button_3"] = (digitalRead(PIN_BUTTON_3) == LOW);
  sne_riddle_clue_buttons.publishState(st);
}

static void publish_all_state(const char *reason) {
  publish_door_state(reason);
  publish_maglock_state(reason);
  publish_knob_leds_state(reason);
  publish_knob_puzzle_state(reason);
  publish_buttons_state(reason);
}

static void leds_fill(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, r, g, b);
  strip.show();
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

  if (ctx->kind == DeviceKind::Door) {
    if (strcmp(op, "lift") == 0) {
      door_direction = 1;
      publish_door_state(op);
      return true;
    }
    if (strcmp(op, "lower") == 0) {
      door_direction = -1;
      publish_door_state(op);
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      stepper_one.stop();
      stepper_two.stop();
      motors_running = false;
      motor_speed_set = false;
      door_direction = 0;
      publish_door_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::Maglock) {
    if (strcmp(op, "lock") == 0) {
      maglock_locked = true;
      digitalWrite(PIN_MAGLOCK, HIGH);
      publish_maglock_state(op);
      return true;
    }
    if (strcmp(op, "unlock") == 0) {
      maglock_locked = false;
      digitalWrite(PIN_MAGLOCK, LOW);
      publish_maglock_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::KnobLeds) {
    if (strcmp(op, "on") == 0) {
      leds_on = true;
      strip.setBrightness(leds_brightness);
      strip.show();
      publish_knob_leds_state(op);
      return true;
    }
    if (strcmp(op, "off") == 0) {
      leds_on = false;
      strip.setBrightness(0);
      strip.show();
      publish_knob_leds_state(op);
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
      if (leds_on) strip.setBrightness(leds_brightness);
      strip.show();
      publish_knob_leds_state(op);
      return true;
    }
    if (strcmp(op, "set_all") == 0) {
      int r = p["r"] | 0;
      int g = p["g"] | 0;
      int b = p["b"] | 0;
      r = constrain(r, 0, 255);
      g = constrain(g, 0, 255);
      b = constrain(b, 0, 255);
      leds_fill((uint8_t)r, (uint8_t)g, (uint8_t)b);
      publish_knob_leds_state(op);
      return true;
    }
    if (strcmp(op, "set_pixel") == 0) {
      if (!p.containsKey("index")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      int idx = p["index"] | 0;
      if (idx < 0 || idx >= NUM_LEDS) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      int r = p["r"] | 0;
      int g = p["g"] | 0;
      int b = p["b"] | 0;
      r = constrain(r, 0, 255);
      g = constrain(g, 0, 255);
      b = constrain(b, 0, 255);
      strip.setPixelColor(idx, (uint8_t)r, (uint8_t)g, (uint8_t)b);
      strip.show();
      publish_knob_leds_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Sensor-only devices.
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);
  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  // Inputs
  pinMode(PIN_KNOB_A2, INPUT_PULLUP);
  pinMode(PIN_KNOB_A3, INPUT_PULLUP);
  pinMode(PIN_B1, INPUT_PULLUP);
  pinMode(PIN_B2, INPUT_PULLUP);
  pinMode(PIN_B3, INPUT_PULLUP);
  pinMode(PIN_B4, INPUT_PULLUP);
  pinMode(PIN_C1, INPUT_PULLUP);
  pinMode(PIN_C2, INPUT_PULLUP);
  pinMode(PIN_C3, INPUT_PULLUP);
  pinMode(PIN_C4, INPUT_PULLUP);
  pinMode(PIN_D1, INPUT_PULLUP);
  pinMode(PIN_D2, INPUT_PULLUP);
  pinMode(PIN_D3, INPUT_PULLUP);
  pinMode(PIN_D4, INPUT_PULLUP);
  pinMode(PIN_E1, INPUT_PULLUP);
  pinMode(PIN_E2, INPUT_PULLUP);
  pinMode(PIN_E3, INPUT_PULLUP);
  pinMode(PIN_E4, INPUT_PULLUP);
  pinMode(PIN_F1, INPUT_PULLUP);
  pinMode(PIN_F2, INPUT_PULLUP);
  pinMode(PIN_F3, INPUT_PULLUP);
  pinMode(PIN_F4, INPUT_PULLUP);
  pinMode(PIN_G1, INPUT_PULLUP);
  pinMode(PIN_G4, INPUT_PULLUP);

  pinMode(PIN_BUTTON_1, INPUT_PULLUP);
  pinMode(PIN_BUTTON_2, INPUT_PULLUP);
  pinMode(PIN_BUTTON_3, INPUT_PULLUP);

  pinMode(PIN_ENDSTOP_UP_R, INPUT_PULLUP);
  pinMode(PIN_ENDSTOP_UP_L, INPUT_PULLUP);
  pinMode(PIN_ENDSTOP_DN_R, INPUT_PULLUP);
  pinMode(PIN_ENDSTOP_DN_L, INPUT_PULLUP);

  // Outputs
  pinMode(PIN_MAGLOCK, OUTPUT);
  maglock_locked = true;
  digitalWrite(PIN_MAGLOCK, HIGH);

  // Steppers
  stepper_one.setMaxSpeed(8000);
  stepper_one.setAcceleration(800);
  stepper_two.setMaxSpeed(8000);
  stepper_two.setAcceleration(800);

  // LEDs
  strip.begin();
  strip.setBrightness(leds_brightness);
  leds_fill(0, 0, 0);

  if (!sne_riddle_door_lift.begin()) while (true) delay(1000);
  if (!sne_riddle_lever_maglock.begin()) while (true) delay(1000);
  if (!sne_riddle_knob_leds.begin()) while (true) delay(1000);
  if (!sne_riddle_knob_puzzle.begin()) while (true) delay(1000);
  if (!sne_riddle_clue_buttons.begin()) while (true) delay(1000);

  sne_riddle_door_lift.setCommandHandler(handleCommand, &ctx_door);
  sne_riddle_lever_maglock.setCommandHandler(handleCommand, &ctx_maglock);
  sne_riddle_knob_leds.setCommandHandler(handleCommand, &ctx_knob_leds);
  sne_riddle_knob_puzzle.setCommandHandler(handleCommand, &ctx_knob_puzzle);
  sne_riddle_clue_buttons.setCommandHandler(handleCommand, &ctx_buttons);

  publish_all_state("boot");
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  run_motors();

  static unsigned long last_publish = 0;
  const unsigned long now = millis();
  bool force = (now - last_publish) >= 1000UL;
  if (force) {
    publish_door_state("periodic");
    publish_knob_puzzle_state("periodic");
    publish_buttons_state("periodic");
    publish_maglock_state("periodic");
    publish_knob_leds_state("periodic");
    last_publish = now;
  }
}
