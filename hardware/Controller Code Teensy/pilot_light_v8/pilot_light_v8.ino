// Pilot Light Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - pilot_light_boiler_fire_leds
// - pilot_light_boiler_monitor_power_relay
// - pilot_light_newell_power_relay
// - pilot_light_flange_leds
// - pilot_light_pilotlight_color_sensor
// - pilot_light_controller

#include <Arduino.h>
#include <ArduinoJson.h>

#include <FastLED.h>
#include <Wire.h>
#include <Adafruit_TCS34725.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_FIRE_LEDS = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MONITOR_RELAY = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_NEWELL_RELAY = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FLANGE_LEDS = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_COLOR_SENSOR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CONTROLLER = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins
static const int power_led_pin = 13;
static const int led_pin_a = 2;           // Boiler Bank 1, Strip 1
static const int led_pin_b = 3;           // Boiler Bank 1, Strip 2
static const int led_pin_c = 4;           // Boiler Bank 1, Strip 3
static const int led_pin_d = 5;           // Boiler Bank 2, Strip 1
static const int led_pin_e = 6;           // Boiler Bank 2, Strip 2
static const int led_pin_f = 7;           // Boiler Bank 2, Strip 3
static const int boiler_monitor_pin = 10; // Boiler Monitor Power Relay
static const int newell_power_pin = 9;    // Newell Post Power Relay
static const int led_flange_pin = 24;     // Flange Status LED Strip

// Hardware config
static const int num_leds = 34;
static const int color_temp_threshold = 50;
static const int lux_threshold = 10;
static const unsigned long sensor_publish_interval_ms = 60UL * 1000UL;

// LED arrays
static CRGB leds_b1s1[num_leds];
static CRGB leds_b1s2[num_leds];
static CRGB leds_b1s3[num_leds];
static CRGB leds_b2s1[num_leds];
static CRGB leds_b2s2[num_leds];
static CRGB leds_b2s3[num_leds];
static CRGB leds_flange[num_leds];
static CRGBPalette16 fire_palette;
static int heat[num_leds];
static int flame[num_leds];

// Sensor hardware
static Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_1X);
static bool color_sensor_available = false;
static uint16_t last_color_temp = 0;
static uint16_t last_lux = 0;
static unsigned long last_sensor_publish_time = 0;

// Current hardware state
static bool fire_leds_active = false;
static bool boiler_monitor_on = false;
static bool newell_power_on = true; // matches v2 default
static bool flange_leds_on = true;
static CRGB flange_color = CRGB::Green;

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

static sentient_v8::Config cfg_fire = make_cfg("pilot_light_boiler_fire_leds", HMAC_FIRE_LEDS);
static sentient_v8::Config cfg_monitor = make_cfg("pilot_light_boiler_monitor_power_relay", HMAC_MONITOR_RELAY);
static sentient_v8::Config cfg_newell = make_cfg("pilot_light_newell_power_relay", HMAC_NEWELL_RELAY);
static sentient_v8::Config cfg_flange = make_cfg("pilot_light_flange_leds", HMAC_FLANGE_LEDS);
static sentient_v8::Config cfg_sensor = make_cfg("pilot_light_pilotlight_color_sensor", HMAC_COLOR_SENSOR);
static sentient_v8::Config cfg_controller = make_cfg("pilot_light_controller", HMAC_CONTROLLER);

static sentient_v8::Client sne_fire(cfg_fire);
static sentient_v8::Client sne_monitor(cfg_monitor);
static sentient_v8::Client sne_newell(cfg_newell);
static sentient_v8::Client sne_flange(cfg_flange);
static sentient_v8::Client sne_sensor(cfg_sensor);
static sentient_v8::Client sne_controller(cfg_controller);

enum class DeviceKind : uint8_t { FireLeds, MonitorRelay, NewellRelay, FlangeLeds, ColorSensor, Controller };

struct DeviceCtx {
  DeviceKind kind;
  sentient_v8::Client *client;
};

static DeviceCtx ctx_fire = {DeviceKind::FireLeds, &sne_fire};
static DeviceCtx ctx_monitor = {DeviceKind::MonitorRelay, &sne_monitor};
static DeviceCtx ctx_newell = {DeviceKind::NewellRelay, &sne_newell};
static DeviceCtx ctx_flange = {DeviceKind::FlangeLeds, &sne_flange};
static DeviceCtx ctx_sensor = {DeviceKind::ColorSensor, &sne_sensor};
static DeviceCtx ctx_controller = {DeviceKind::Controller, &sne_controller};

static unsigned long last_fire_frame = 0;

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
  digitalWrite(boiler_monitor_pin, boiler_monitor_on ? HIGH : LOW);
  digitalWrite(newell_power_pin, newell_power_on ? HIGH : LOW);

  if (!flange_leds_on) {
    fill_solid(leds_flange, num_leds, CRGB::Black);
  } else {
    fill_solid(leds_flange, num_leds, flange_color);
  }

  if (!fire_leds_active) {
    fill_solid(leds_b1s1, num_leds, CRGB::Black);
    fill_solid(leds_b1s2, num_leds, CRGB::Black);
    fill_solid(leds_b1s3, num_leds, CRGB::Black);
    fill_solid(leds_b2s1, num_leds, CRGB::Black);
    fill_solid(leds_b2s2, num_leds, CRGB::Black);
    fill_solid(leds_b2s3, num_leds, CRGB::Black);
  }

  FastLED.show();
}

static void publish_state_fire() {
  StaticJsonDocument<256> st;
  st["active"] = fire_leds_active;
  sne_fire.publishState(st);
}

static void publish_state_monitor() {
  StaticJsonDocument<256> st;
  st["on"] = boiler_monitor_on;
  sne_monitor.publishState(st);
}

static void publish_state_newell() {
  StaticJsonDocument<256> st;
  st["on"] = newell_power_on;
  sne_newell.publishState(st);
}

static void publish_state_flange() {
  StaticJsonDocument<256> st;
  st["on"] = flange_leds_on;
  JsonObject c = st.createNestedObject("color");
  c["r"] = flange_color.r;
  c["g"] = flange_color.g;
  c["b"] = flange_color.b;
  sne_flange.publishState(st);
}

static void publish_state_sensor(uint16_t color_temp, uint16_t lux) {
  StaticJsonDocument<256> st;
  st["ok"] = color_sensor_available;
  st["color_temperature"] = color_temp;
  st["lux"] = lux;
  sne_sensor.publishState(st);
}

static void publish_telemetry_sensor(uint16_t color_temp, uint16_t lux, uint16_t r, uint16_t g, uint16_t b, uint16_t c) {
  StaticJsonDocument<384> t;
  t["color_temperature"] = color_temp;
  t["lux"] = lux;
  JsonObject raw = t.createNestedObject("raw");
  raw["r"] = r;
  raw["g"] = g;
  raw["b"] = b;
  raw["c"] = c;
  sne_sensor.publishTelemetry(t);
}

static void publish_state_controller() {
  StaticJsonDocument<384> st;
  st["fire_leds_active"] = fire_leds_active;
  st["boiler_monitor_on"] = boiler_monitor_on;
  st["newell_power_on"] = newell_power_on;
  st["flange_leds_on"] = flange_leds_on;
  st["color_sensor_ok"] = color_sensor_available;
  sne_controller.publishState(st);
}

static void publish_all_states() {
  publish_state_fire();
  publish_state_monitor();
  publish_state_newell();
  publish_state_flange();
  publish_state_controller();
  // sensor state is published on read/change (and at startup if sensor present)
}

static void fill_fire_frame() {
  int i = random(0, num_leds);
  heat[i] = qsub8(heat[i], random8(1, 10));
  flame[i] = random(-50, 50);
  if (flame[i] == 0) flame[i] = random(1, 50);

  for (int idx = 0; idx < num_leds; idx++) {
    heat[idx] = min(255, max(100, heat[idx] + flame[idx]));
    CRGB color = ColorFromPalette(fire_palette, scale8(heat[idx], 240), scale8(heat[idx], 250), LINEARBLEND);
    leds_b1s1[idx] = color;
    leds_b1s2[idx] = color;
    leds_b1s3[idx] = color;
    leds_b2s1[idx] = color;
    leds_b2s2[idx] = color;
    leds_b2s3[idx] = color;

    flame[idx] = (heat[idx] + flame[idx] > 255) ? random(-50, -1)
                 : (heat[idx] < 0)              ? random(1, 50)
                                                : flame[idx];
  }

  FastLED.show();
}

static bool parse_rgb(JsonVariantConst v, CRGB &out) {
  if (!v.is<JsonObjectConst>()) return false;
  int r = v["r"] | -1;
  int g = v["g"] | -1;
  int b = v["b"] | -1;
  if (r < 0 || r > 255) return false;
  if (g < 0 || g > 255) return false;
  if (b < 0 || b > 255) return false;
  out = CRGB(uint8_t(r), uint8_t(g), uint8_t(b));
  return true;
}

static void sensor_read_and_publish(bool force_publish) {
  if (!color_sensor_available) {
    color_sensor_available = tcs.begin();
    if (!color_sensor_available) return;
  }

  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);
  uint16_t color_temp = tcs.calculateColorTemperature_dn40(r, g, b, c);
  uint16_t lux = tcs.calculateLux(r, g, b);

  bool changed = false;
  if (abs((int)color_temp - (int)last_color_temp) >= color_temp_threshold) changed = true;
  if (abs((int)lux - (int)last_lux) >= lux_threshold) changed = true;

  unsigned long now = millis();
  bool interval = (now - last_sensor_publish_time >= sensor_publish_interval_ms);
  if (!force_publish && !changed && !interval) return;

  last_color_temp = color_temp;
  last_lux = lux;
  last_sensor_publish_time = now;

  publish_state_sensor(color_temp, lux);
  publish_telemetry_sensor(color_temp, lux, r, g, b, c);
  publish_state_controller();
}

static void reset_hardware_defaults() {
  fire_leds_active = false;
  boiler_monitor_on = false;
  newell_power_on = true;
  flange_leds_on = true;
  flange_color = CRGB::Green;
  apply_outputs();
  publish_all_states();
  sensor_read_and_publish(true);
}

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void *vctx) {
  DeviceCtx *ctx = (DeviceCtx *)vctx;
  if (!ctx || !ctx->client) {
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
  case DeviceKind::FireLeds: {
    if (strcmp(op, "set") != 0 || !p.containsKey("on")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    fire_leds_active = p["on"] | false;
    if (!fire_leds_active) {
      apply_outputs();
    }
    publish_state_fire();
    publish_state_controller();
    return true;
  }
  case DeviceKind::MonitorRelay: {
    if (strcmp(op, "set") != 0 || !p.containsKey("on")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    boiler_monitor_on = p["on"] | false;
    apply_outputs();
    publish_state_monitor();
    publish_state_controller();
    return true;
  }
  case DeviceKind::NewellRelay: {
    if (strcmp(op, "set") != 0 || !p.containsKey("on")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    newell_power_on = p["on"] | false;
    apply_outputs();
    publish_state_newell();
    publish_state_controller();
    return true;
  }
  case DeviceKind::FlangeLeds: {
    if (strcmp(op, "set") != 0) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    bool touched = false;
    if (p.containsKey("on")) {
      flange_leds_on = p["on"] | false;
      touched = true;
    }
    if (p.containsKey("color")) {
      CRGB c;
      if (!parse_rgb(p["color"], c)) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      flange_color = c;
      touched = true;
    }
    if (!touched) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    apply_outputs();
    publish_state_flange();
    publish_state_controller();
    return true;
  }
  case DeviceKind::ColorSensor: {
    if (strcmp(op, "read") != 0) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    sensor_read_and_publish(true);
    return true;
  }
  case DeviceKind::Controller: {
    if (strcmp(op, "reset") == 0) {
      reset_hardware_defaults();
      return true;
    }
    if (strcmp(op, "request_status") == 0) {
      publish_all_states();
      sensor_read_and_publish(true);
      return true;
    }
    if (strcmp(op, "noop") == 0) {
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }
  }

  rejectedAckReason["reason_code"] = "INTERNAL_ERROR";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(power_led_pin, OUTPUT);
  pinMode(boiler_monitor_pin, OUTPUT);
  pinMode(newell_power_pin, OUTPUT);

  FastLED.addLeds<WS2812B, led_pin_a, GRB>(leds_b1s1, num_leds);
  FastLED.addLeds<WS2812B, led_pin_b, GRB>(leds_b1s2, num_leds);
  FastLED.addLeds<WS2812B, led_pin_c, GRB>(leds_b1s3, num_leds);
  FastLED.addLeds<WS2812B, led_pin_d, GRB>(leds_b2s1, num_leds);
  FastLED.addLeds<WS2812B, led_pin_e, GRB>(leds_b2s2, num_leds);
  FastLED.addLeds<WS2812B, led_pin_f, GRB>(leds_b2s3, num_leds);
  FastLED.addLeds<WS2812B, led_flange_pin, GRB>(leds_flange, num_leds);
  FastLED.setBrightness(250);

  fire_palette = CRGBPalette16(0x3A79EB, 0x6495ED, 0xFC6000);
  randomSeed(analogRead(A0));

  color_sensor_available = tcs.begin();

  apply_outputs();

  if (!sne_fire.begin()) while (true) delay(1000);
  if (!sne_monitor.begin()) while (true) delay(1000);
  if (!sne_newell.begin()) while (true) delay(1000);
  if (!sne_flange.begin()) while (true) delay(1000);
  if (!sne_sensor.begin()) while (true) delay(1000);
  if (!sne_controller.begin()) while (true) delay(1000);

  sne_fire.setCommandHandler(handleCommand, &ctx_fire);
  sne_monitor.setCommandHandler(handleCommand, &ctx_monitor);
  sne_newell.setCommandHandler(handleCommand, &ctx_newell);
  sne_flange.setCommandHandler(handleCommand, &ctx_flange);
  sne_sensor.setCommandHandler(handleCommand, &ctx_sensor);
  sne_controller.setCommandHandler(handleCommand, &ctx_controller);

  publish_all_states();
  sensor_read_and_publish(true);
}

void loop() {
  sne_fire.loop();
  sne_monitor.loop();
  sne_newell.loop();
  sne_flange.loop();
  sne_sensor.loop();
  sne_controller.loop();

  if (fire_leds_active) {
    unsigned long now = millis();
    if (now - last_fire_frame >= 30) {
      last_fire_frame = now;
      fill_fire_frame();
    }
  }

  sensor_read_and_publish(false);
}
