// Gauge 6 + LEDs Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - gauge_6_leds_gauge_6
// - gauge_6_leds_lever_1_red
// - gauge_6_leds_lever_2_blue
// - gauge_6_leds_lever_3_green
// - gauge_6_leds_lever_4_white
// - gauge_6_leds_lever_5_orange
// - gauge_6_leds_lever_6_yellow
// - gauge_6_leds_lever_7_purple
// - gauge_6_leds_ceiling_leds
// - gauge_6_leds_gauge_leds

#include <Arduino.h>
#include <ArduinoJson.h>

#include <AccelStepper.h>
#include <EEPROM.h>
#include <FastLED.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_GAUGE_6 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_1 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_2 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_3 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_4 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_5 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_6 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_7 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CEILING = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GAUGE_LEDS = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (from v2)
static const int PIN_POWER_LED = 13;

// Gauge 6 Stepper Motor
static const int PIN_GAUGE_6_STEP = 39;
static const int PIN_GAUGE_6_DIR = 40;
static const int PIN_GAUGE_6_ENABLE = 41; // active LOW enable
static const int PIN_VALVE_6_POT = A4;

// Photoresistor Lever Sensors
static const int PIN_LEVER_1_RED = A2;
static const int PIN_LEVER_2_BLUE = A3;
static const int PIN_LEVER_3_GREEN = A5;
static const int PIN_LEVER_4_WHITE = A6;
static const int PIN_LEVER_5_ORANGE = A7;
static const int PIN_LEVER_6_YELLOW = A8;
static const int PIN_LEVER_7_PURPLE = A9;

// LED Pins
static const int PIN_CEILING_LEDS = 7;
static const int PIN_GAUGE_LED_1 = 25;
static const int PIN_GAUGE_LED_2 = 26;
static const int PIN_GAUGE_LED_3 = 27;
static const int PIN_GAUGE_LED_4 = 28;
static const int PIN_GAUGE_LED_5 = 29;
static const int PIN_GAUGE_LED_6 = 30;
static const int PIN_GAUGE_LED_7 = 31;

// Gauge config
static const int GAUGE_MAX_SPEED = 800;
static const int GAUGE_ACCEL = 500;
static const int GAUGE_MIN_STEPS = 0;
static const int GAUGE_MAX_STEPS = 700;

// Valve calibration (map potentiometer reading to steps)
static const int VALVE_6_ZERO = 225;
static const int VALVE_6_MAX = 500;

// EEPROM addr for gauge position
static const int EEPROM_ADDR_GAUGE6 = 0;

// LED config
static const int NUM_CEILING_LEDS = 219;
static CRGB ceiling_leds[NUM_CEILING_LEDS];

// Ceiling LED sections (clock face pattern)
static const int section_start[] = {0, 0, 25, 48, 73, 99, 125, 149, 174, 198};
static const int section_length[] = {0, 25, 23, 25, 26, 26, 24, 25, 24, 21};

// Gauge indicator LEDs (7 single LEDs)
static CRGB gauge_leds[7][1];

// Color definitions (from v2)
static const uint32_t color_clock_red = 0xFF0000;
static const uint32_t color_clock_blue = 0x0000FF;
static const uint32_t color_clock_green = 0x00FF00;
static const uint32_t color_clock_white = 0xFFFFFF;
static const uint32_t color_clock_orange = 0xFF8000;
static const uint32_t color_clock_yellow = 0xFFFF00;
static const uint32_t color_clock_purple = 0x800080;

static const uint32_t color_gauge_base = 0x221100;
static const uint32_t flicker_red = 0xFF0000;
static const uint32_t flicker_blue = 0x0000FF;
static const uint32_t flicker_green = 0x00FF00;
static const uint32_t flicker_white = 0xFFFFFF;
static const uint32_t flicker_orange = 0xFF8000;
static const uint32_t flicker_yellow = 0xFFFF00;
static const uint32_t flicker_purple = 0x800080;

// Flicker state
struct FlickerState {
  bool enabled = false;
  bool active = false;
  unsigned long next_burst_at = 0;
  unsigned long flicker_end = 0;
  uint32_t color1 = 0;
  uint32_t color2 = 0;
  bool use_two_colors = false;
  bool use_second_color = false;
};
static FlickerState gauge_flicker[7];

// Sensors
static const int PHOTORESISTOR_THRESHOLD = 500;

static bool lever_open[7] = {false, false, false, false, false, false, false};
static bool last_published_lever_open[7] = {false, false, false, false, false, false, false};
static bool levers_initialized = false;

static bool gauges_active = false;
static int valve_6_raw = 0;

static const unsigned long SENSOR_REFRESH_MS = 60UL * 1000UL;

// Ceiling + gauge-leds state (for retained state)
static const char *ceiling_mode = "off"; // off|pattern_1|pattern_2|pattern_3
static const char *gauge_leds_mode = "off"; // off|on|flicker_2|flicker_5|flicker_8

static AccelStepper stepper_6(AccelStepper::DRIVER, PIN_GAUGE_6_STEP, PIN_GAUGE_6_DIR);

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

static sentient_v8::Client sne_gauge_6(make_cfg("gauge_6_leds_gauge_6", HMAC_GAUGE_6));
static sentient_v8::Client sne_lever_1(make_cfg("gauge_6_leds_lever_1_red", HMAC_LEVER_1));
static sentient_v8::Client sne_lever_2(make_cfg("gauge_6_leds_lever_2_blue", HMAC_LEVER_2));
static sentient_v8::Client sne_lever_3(make_cfg("gauge_6_leds_lever_3_green", HMAC_LEVER_3));
static sentient_v8::Client sne_lever_4(make_cfg("gauge_6_leds_lever_4_white", HMAC_LEVER_4));
static sentient_v8::Client sne_lever_5(make_cfg("gauge_6_leds_lever_5_orange", HMAC_LEVER_5));
static sentient_v8::Client sne_lever_6(make_cfg("gauge_6_leds_lever_6_yellow", HMAC_LEVER_6));
static sentient_v8::Client sne_lever_7(make_cfg("gauge_6_leds_lever_7_purple", HMAC_LEVER_7));
static sentient_v8::Client sne_ceiling(make_cfg("gauge_6_leds_ceiling_leds", HMAC_CEILING));
static sentient_v8::Client sne_gauge_leds(make_cfg("gauge_6_leds_gauge_leds", HMAC_GAUGE_LEDS));

static sentient_v8::Client *clients[] = {&sne_gauge_6, &sne_lever_1, &sne_lever_2, &sne_lever_3, &sne_lever_4,
                                         &sne_lever_5, &sne_lever_6, &sne_lever_7, &sne_ceiling, &sne_gauge_leds};

enum class DeviceKind : uint8_t { Gauge6, Lever, Ceiling, GaugeLeds };
struct DeviceCtx {
  DeviceKind kind;
  int lever_idx; // 0..6 for levers
};
static DeviceCtx ctx_gauge6 = {DeviceKind::Gauge6, -1};
static DeviceCtx ctx_levers[7] = {
    {DeviceKind::Lever, 0}, {DeviceKind::Lever, 1}, {DeviceKind::Lever, 2}, {DeviceKind::Lever, 3},
    {DeviceKind::Lever, 4}, {DeviceKind::Lever, 5}, {DeviceKind::Lever, 6},
};
static DeviceCtx ctx_ceiling = {DeviceKind::Ceiling, -1};
static DeviceCtx ctx_gauge_leds = {DeviceKind::GaugeLeds, -1};

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

static void save_gauge_position() {
  int position = stepper_6.currentPosition();
  EEPROM.put(EEPROM_ADDR_GAUGE6, position);
}

static void load_gauge_position() {
  int position6;
  EEPROM.get(EEPROM_ADDR_GAUGE6, position6);
  if (position6 < -5000 || position6 > 5000) position6 = 0;
  stepper_6.setCurrentPosition(position6);
}

static void set_ceiling_off() {
  FastLED.clear();
  FastLED.show();
  ceiling_mode = "off";
}

static void set_ceiling_pattern_1() {
  FastLED.clear();
  fill_solid(&ceiling_leds[section_start[2]], section_length[2], CRGB(color_clock_blue));
  fill_solid(&ceiling_leds[section_start[7]], section_length[7], CRGB(color_clock_green));
  FastLED.show();
  ceiling_mode = "pattern_1";
}

static void set_ceiling_pattern_2() {
  FastLED.clear();
  fill_solid(&ceiling_leds[section_start[1]], section_length[1], CRGB(color_clock_red));
  fill_solid(&ceiling_leds[section_start[5]], section_length[5], CRGB(color_clock_white));
  fill_solid(&ceiling_leds[section_start[8]], section_length[8], CRGB(color_clock_orange));
  FastLED.show();
  ceiling_mode = "pattern_2";
}

static void set_ceiling_pattern_3() {
  FastLED.clear();
  fill_solid(&ceiling_leds[section_start[1]], section_length[1], CRGB(color_clock_purple));
  fill_solid(&ceiling_leds[section_start[2]], section_length[2], CRGB(color_clock_blue));
  fill_solid(&ceiling_leds[section_start[5]], section_length[5], CRGB(color_clock_yellow));
  fill_solid(&ceiling_leds[section_start[7]], section_length[7], CRGB(color_clock_green));
  FastLED.show();
  ceiling_mode = "pattern_3";
}

static void set_flicker_off() {
  for (int i = 0; i < 7; i++) {
    gauge_flicker[i].enabled = false;
    gauge_leds[i][0] = CRGB::Black;
  }
  FastLED.show();
  gauge_leds_mode = "off";
}

static void set_gauge_leds_on() {
  for (int i = 0; i < 7; i++) {
    gauge_flicker[i].enabled = false;
    gauge_leds[i][0] = CRGB(color_gauge_base);
  }
  FastLED.show();
  gauge_leds_mode = "on";
}

static void set_gauge_leds_off() {
  for (int i = 0; i < 7; i++) {
    gauge_flicker[i].enabled = false;
    gauge_leds[i][0] = CRGB::Black;
  }
  FastLED.show();
  gauge_leds_mode = "off";
}

static void set_flicker_mode_2() {
  for (int i = 0; i < 7; i++) {
    gauge_flicker[i].enabled = false;
    gauge_leds[i][0] = CRGB(color_gauge_base);
  }
  gauge_flicker[3].enabled = true;
  gauge_flicker[3].color1 = flicker_blue;
  gauge_flicker[3].color2 = color_gauge_base;
  gauge_flicker[3].use_two_colors = true;

  gauge_flicker[5].enabled = true;
  gauge_flicker[5].color1 = flicker_green;
  gauge_flicker[5].color2 = color_gauge_base;
  gauge_flicker[5].use_two_colors = true;

  FastLED.show();
  gauge_leds_mode = "flicker_2";
}

static void set_flicker_mode_5() {
  for (int i = 0; i < 7; i++) {
    gauge_flicker[i].enabled = false;
    gauge_leds[i][0] = CRGB(color_gauge_base);
  }
  gauge_flicker[0].enabled = true;
  gauge_flicker[0].color1 = flicker_red;
  gauge_flicker[0].color2 = color_gauge_base;
  gauge_flicker[0].use_two_colors = true;

  gauge_flicker[1].enabled = true;
  gauge_flicker[1].color1 = flicker_orange;
  gauge_flicker[1].color2 = color_gauge_base;
  gauge_flicker[1].use_two_colors = true;

  gauge_flicker[2].enabled = true;
  gauge_flicker[2].color1 = flicker_white;
  gauge_flicker[2].color2 = color_gauge_base;
  gauge_flicker[2].use_two_colors = true;

  gauge_flicker[6].enabled = true;
  gauge_flicker[6].color1 = flicker_orange;
  gauge_flicker[6].color2 = color_gauge_base;
  gauge_flicker[6].use_two_colors = true;

  FastLED.show();
  gauge_leds_mode = "flicker_5";
}

static void set_flicker_mode_8() {
  for (int i = 0; i < 7; i++) {
    gauge_flicker[i].enabled = false;
    gauge_leds[i][0] = CRGB(color_gauge_base);
  }
  gauge_flicker[0].enabled = true;
  gauge_flicker[0].color1 = flicker_red;
  gauge_flicker[0].color2 = color_gauge_base;
  gauge_flicker[0].use_two_colors = true;

  gauge_flicker[1].enabled = true;
  gauge_flicker[1].color1 = flicker_orange;
  gauge_flicker[1].color2 = flicker_purple;
  gauge_flicker[1].use_two_colors = true;

  gauge_flicker[2].enabled = true;
  gauge_flicker[2].color1 = flicker_white;
  gauge_flicker[2].color2 = color_gauge_base;
  gauge_flicker[2].use_two_colors = true;

  gauge_flicker[3].enabled = true;
  gauge_flicker[3].color1 = flicker_blue;
  gauge_flicker[3].color2 = color_gauge_base;
  gauge_flicker[3].use_two_colors = true;

  gauge_flicker[4].enabled = true;
  gauge_flicker[4].color1 = flicker_purple;
  gauge_flicker[4].color2 = color_gauge_base;
  gauge_flicker[4].use_two_colors = true;

  gauge_flicker[5].enabled = true;
  gauge_flicker[5].color1 = flicker_orange;
  gauge_flicker[5].color2 = color_gauge_base;
  gauge_flicker[5].use_two_colors = true;

  gauge_flicker[6].enabled = true;
  gauge_flicker[6].color1 = flicker_orange;
  gauge_flicker[6].color2 = color_gauge_base;
  gauge_flicker[6].use_two_colors = true;

  FastLED.show();
  gauge_leds_mode = "flicker_8";
}

static void update_gauge_flicker() {
  unsigned long now = millis();
  bool any_active = false;

  for (int i = 0; i < 7; i++) {
    if (!gauge_flicker[i].enabled) continue;

    FlickerState &fs = gauge_flicker[i];
    if (!fs.active) {
      if (now >= fs.next_burst_at) {
        fs.active = true;
        fs.flicker_end = now + random(50, 250);
        fs.use_second_color = (fs.use_two_colors && random(2) == 0);
        fs.next_burst_at = now + random(100, 500);
      }
    }

    if (fs.active && now <= fs.flicker_end) {
      CRGB color = (fs.use_two_colors && fs.use_second_color) ? CRGB(fs.color2) : CRGB(fs.color1);
      color.nscale8_video(random(256));
      gauge_leds[i][0] = color;
      any_active = true;
    } else if (fs.active && now > fs.flicker_end) {
      fs.active = false;
      gauge_leds[i][0] = CRGB::Black;
    }
  }

  if (any_active) FastLED.show();
}

static void update_gauge_tracking() {
  if (!gauges_active) return;
  int raw_reading = analogRead(PIN_VALVE_6_POT);
  valve_6_raw = raw_reading;
  int target_steps = map(raw_reading, VALVE_6_ZERO, VALVE_6_MAX, GAUGE_MIN_STEPS, GAUGE_MAX_STEPS);
  target_steps = constrain(target_steps, GAUGE_MIN_STEPS, GAUGE_MAX_STEPS);
  stepper_6.moveTo(target_steps);
}

static void publish_gauge_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["gauges_active"] = gauges_active;
  st["position_steps"] = stepper_6.currentPosition();
  st["valve_raw"] = valve_6_raw;
  st["reason"] = reason ? reason : "";
  sne_gauge_6.publishState(st);
}

static void publish_lever_state(int idx, const char *reason, int raw) {
  sentient_v8::Client *c = nullptr;
  if (idx == 0) c = &sne_lever_1;
  if (idx == 1) c = &sne_lever_2;
  if (idx == 2) c = &sne_lever_3;
  if (idx == 3) c = &sne_lever_4;
  if (idx == 4) c = &sne_lever_5;
  if (idx == 5) c = &sne_lever_6;
  if (idx == 6) c = &sne_lever_7;
  if (!c) return;

  StaticJsonDocument<192> st;
  st["state"] = lever_open[idx] ? "OPEN" : "CLOSED";
  st["open"] = lever_open[idx];
  st["raw"] = raw;
  st["reason"] = reason ? reason : "";
  c->publishState(st);
  last_published_lever_open[idx] = lever_open[idx];
}

static void publish_ceiling_state(const char *reason) {
  StaticJsonDocument<160> st;
  st["mode"] = ceiling_mode;
  st["reason"] = reason ? reason : "";
  sne_ceiling.publishState(st);
}

static void publish_gauge_leds_state(const char *reason) {
  StaticJsonDocument<160> st;
  st["mode"] = gauge_leds_mode;
  st["reason"] = reason ? reason : "";
  sne_gauge_leds.publishState(st);
}

static void publish_all_state(const char *reason) {
  publish_gauge_state(reason);
  publish_ceiling_state(reason);
  publish_gauge_leds_state(reason);
  // levers published in monitor loop to include raw
}

static void read_levers(int raw[7]) {
  raw[0] = analogRead(PIN_LEVER_1_RED);
  raw[1] = analogRead(PIN_LEVER_2_BLUE);
  raw[2] = analogRead(PIN_LEVER_3_GREEN);
  raw[3] = analogRead(PIN_LEVER_4_WHITE);
  raw[4] = analogRead(PIN_LEVER_5_ORANGE);
  raw[5] = analogRead(PIN_LEVER_6_YELLOW);
  raw[6] = analogRead(PIN_LEVER_7_PURPLE);
  for (int i = 0; i < 7; i++) lever_open[i] = (raw[i] > PHOTORESISTOR_THRESHOLD);
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
    int raw[7];
    read_levers(raw);
    for (int i = 0; i < 7; i++) publish_lever_state(i, "request_status", raw[i]);
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

  if (ctx->kind == DeviceKind::Gauge6) {
    if (strcmp(op, "activate") == 0) {
      digitalWrite(PIN_GAUGE_6_ENABLE, LOW);
      gauges_active = true;
      publish_gauge_state("activate");
      return true;
    }
    if (strcmp(op, "deactivate") == 0) {
      gauges_active = false;
      stepper_6.moveTo(GAUGE_MIN_STEPS);
      publish_gauge_state("deactivate");
      return true;
    }
    if (strcmp(op, "adjust_zero") == 0) {
      if (!p.containsKey("steps")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      int steps = p["steps"] | 0;
      stepper_6.move(steps);
      publish_gauge_state("adjust_zero");
      return true;
    }
    if (strcmp(op, "set_current_as_zero") == 0) {
      stepper_6.setCurrentPosition(GAUGE_MIN_STEPS);
      save_gauge_position();
      publish_gauge_state("set_current_as_zero");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::Ceiling) {
    if (strcmp(op, "off") == 0) {
      set_ceiling_off();
      publish_ceiling_state("off");
      return true;
    }
    if (strcmp(op, "pattern_1") == 0) {
      set_ceiling_pattern_1();
      publish_ceiling_state("pattern_1");
      return true;
    }
    if (strcmp(op, "pattern_2") == 0) {
      set_ceiling_pattern_2();
      publish_ceiling_state("pattern_2");
      return true;
    }
    if (strcmp(op, "pattern_3") == 0) {
      set_ceiling_pattern_3();
      publish_ceiling_state("pattern_3");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::GaugeLeds) {
    if (strcmp(op, "flicker_off") == 0) {
      set_flicker_off();
      publish_gauge_leds_state("flicker_off");
      return true;
    }
    if (strcmp(op, "flicker_mode_2") == 0) {
      set_flicker_mode_2();
      publish_gauge_leds_state("flicker_mode_2");
      return true;
    }
    if (strcmp(op, "flicker_mode_5") == 0) {
      set_flicker_mode_5();
      publish_gauge_leds_state("flicker_mode_5");
      return true;
    }
    if (strcmp(op, "flicker_mode_8") == 0) {
      set_flicker_mode_8();
      publish_gauge_leds_state("flicker_mode_8");
      return true;
    }
    if (strcmp(op, "leds_on") == 0) {
      set_gauge_leds_on();
      publish_gauge_leds_state("leds_on");
      return true;
    }
    if (strcmp(op, "leds_off") == 0) {
      set_gauge_leds_off();
      publish_gauge_leds_state("leds_off");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Levers are input-only.
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  // Stepper setup
  stepper_6.setMaxSpeed(GAUGE_MAX_SPEED);
  stepper_6.setAcceleration(GAUGE_ACCEL);
  pinMode(PIN_GAUGE_6_ENABLE, OUTPUT);
  digitalWrite(PIN_GAUGE_6_ENABLE, HIGH); // disable initially
  load_gauge_position();

  pinMode(PIN_VALVE_6_POT, INPUT);

  // Analog inputs
  pinMode(PIN_LEVER_1_RED, INPUT);
  pinMode(PIN_LEVER_2_BLUE, INPUT);
  pinMode(PIN_LEVER_3_GREEN, INPUT);
  pinMode(PIN_LEVER_4_WHITE, INPUT);
  pinMode(PIN_LEVER_5_ORANGE, INPUT);
  pinMode(PIN_LEVER_6_YELLOW, INPUT);
  pinMode(PIN_LEVER_7_PURPLE, INPUT);

  // LEDs
  FastLED.addLeds<WS2811, PIN_CEILING_LEDS, RGB>(ceiling_leds, NUM_CEILING_LEDS);
  FastLED.addLeds<WS2812B, PIN_GAUGE_LED_1, GRB>(gauge_leds[0], 1);
  FastLED.addLeds<WS2812B, PIN_GAUGE_LED_2, GRB>(gauge_leds[1], 1);
  FastLED.addLeds<WS2812B, PIN_GAUGE_LED_3, GRB>(gauge_leds[2], 1);
  FastLED.addLeds<WS2812B, PIN_GAUGE_LED_4, GRB>(gauge_leds[3], 1);
  FastLED.addLeds<WS2812B, PIN_GAUGE_LED_5, GRB>(gauge_leds[4], 1);
  FastLED.addLeds<WS2812B, PIN_GAUGE_LED_6, GRB>(gauge_leds[5], 1);
  FastLED.addLeds<WS2812B, PIN_GAUGE_LED_7, GRB>(gauge_leds[6], 1);
  FastLED.clear();
  FastLED.show();

  // Seed flicker randomness.
  randomSeed(analogRead(PIN_VALVE_6_POT));

  if (!sne_gauge_6.begin()) while (true) delay(1000);
  if (!sne_lever_1.begin()) while (true) delay(1000);
  if (!sne_lever_2.begin()) while (true) delay(1000);
  if (!sne_lever_3.begin()) while (true) delay(1000);
  if (!sne_lever_4.begin()) while (true) delay(1000);
  if (!sne_lever_5.begin()) while (true) delay(1000);
  if (!sne_lever_6.begin()) while (true) delay(1000);
  if (!sne_lever_7.begin()) while (true) delay(1000);
  if (!sne_ceiling.begin()) while (true) delay(1000);
  if (!sne_gauge_leds.begin()) while (true) delay(1000);

  sne_gauge_6.setCommandHandler(handleCommand, &ctx_gauge6);
  sne_lever_1.setCommandHandler(handleCommand, &ctx_levers[0]);
  sne_lever_2.setCommandHandler(handleCommand, &ctx_levers[1]);
  sne_lever_3.setCommandHandler(handleCommand, &ctx_levers[2]);
  sne_lever_4.setCommandHandler(handleCommand, &ctx_levers[3]);
  sne_lever_5.setCommandHandler(handleCommand, &ctx_levers[4]);
  sne_lever_6.setCommandHandler(handleCommand, &ctx_levers[5]);
  sne_lever_7.setCommandHandler(handleCommand, &ctx_levers[6]);
  sne_ceiling.setCommandHandler(handleCommand, &ctx_ceiling);
  sne_gauge_leds.setCommandHandler(handleCommand, &ctx_gauge_leds);

  set_ceiling_off();
  set_gauge_leds_off();

  int raw[7];
  read_levers(raw);
  for (int i = 0; i < 7; i++) last_published_lever_open[i] = lever_open[i];
  levers_initialized = true;

  publish_all_state("boot");
  for (int i = 0; i < 7; i++) publish_lever_state(i, "boot", raw[i]);
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  stepper_6.run();
  update_gauge_tracking();
  update_gauge_flicker();

  int raw[7];
  read_levers(raw);

  const unsigned long now = millis();
  static unsigned long last_publish = 0;
  bool force_publish = (now - last_publish) >= SENSOR_REFRESH_MS;

  bool any_changed = false;
  for (int i = 0; i < 7; i++) {
    if (!levers_initialized || force_publish || lever_open[i] != last_published_lever_open[i]) {
      publish_lever_state(i, lever_open[i] != last_published_lever_open[i] ? "change" : "periodic", raw[i]);
      any_changed = true;
    }
  }
  levers_initialized = true;

  if (force_publish || any_changed) {
    last_publish = now;
    publish_gauge_state(force_publish ? "periodic" : "change");
    publish_ceiling_state(force_publish ? "periodic" : "change");
    publish_gauge_leds_state(force_publish ? "periodic" : "change");
    save_gauge_position();
  }
}
