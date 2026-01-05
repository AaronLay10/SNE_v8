// Main Lighting Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - main_lighting_study_lights
// - main_lighting_boiler_lights
// - main_lighting_lab_lights_squares
// - main_lighting_lab_lights_grates
// - main_lighting_sconces
// - main_lighting_crawlspace_lights

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

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_STUDY_LIGHTS = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BOILER_LIGHTS = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LAB_SQUARES = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LAB_GRATES = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_SCONCES = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CRAWLSPACE = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins
static const int power_led_pin = 13;

// LED strips (FastLED)
static const int ceiling_square_a_pin = 4;
static const int ceiling_square_b_pin = 3;
static const int ceiling_square_c_pin = 5;
static const int ceiling_square_d_pin = 6;
static const int ceiling_square_e_pin = 1;
static const int ceiling_square_f_pin = 8;
static const int ceiling_square_g_pin = 7;
static const int ceiling_square_h_pin = 10;
static const int grate_1_pin = 2;
static const int grate_2_pin = 0;
static const int grate_3_pin = 9;

// Analog dimmers
static const int study_lights_pin = A1;
static const int boiler_lights_pin = A4;

// Digital relays
static const int sconces_pin = 12;
static const int crawlspace_lights_pin = 11;

// LED configuration
static const int num_leds_per_strip = 300;

static CRGB leds_sa[num_leds_per_strip];
static CRGB leds_sb[num_leds_per_strip];
static CRGB leds_sc[num_leds_per_strip];
static CRGB leds_sd[num_leds_per_strip];
static CRGB leds_se[num_leds_per_strip];
static CRGB leds_sf[num_leds_per_strip];
static CRGB leds_sg[num_leds_per_strip];
static CRGB leds_sh[num_leds_per_strip];
static CRGB leds_g1[num_leds_per_strip];
static CRGB leds_g2[num_leds_per_strip];
static CRGB leds_g3[num_leds_per_strip];

// Hardware state
static uint8_t study_dimmer = 0;
static uint8_t boiler_dimmer = 0;

static bool lab_squares_on = false;
static uint8_t lab_squares_brightness = 255;
static CRGB lab_squares_color = CRGB::Yellow;

static bool lab_grates_on = false;
static uint8_t lab_grates_brightness = 255;
static CRGB lab_grates_color = CRGB::Blue;

static bool sconces_on = false;
static bool crawlspace_lights_on = false;

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

static sentient_v8::Client sne_study(make_cfg("main_lighting_study_lights", HMAC_STUDY_LIGHTS));
static sentient_v8::Client sne_boiler(make_cfg("main_lighting_boiler_lights", HMAC_BOILER_LIGHTS));
static sentient_v8::Client sne_squares(make_cfg("main_lighting_lab_lights_squares", HMAC_LAB_SQUARES));
static sentient_v8::Client sne_grates(make_cfg("main_lighting_lab_lights_grates", HMAC_LAB_GRATES));
static sentient_v8::Client sne_sconces(make_cfg("main_lighting_sconces", HMAC_SCONCES));
static sentient_v8::Client sne_crawlspace(make_cfg("main_lighting_crawlspace_lights", HMAC_CRAWLSPACE));

enum class DeviceKind : uint8_t { Study, Boiler, Squares, Grates, Sconces, Crawlspace };

struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_study = {DeviceKind::Study};
static DeviceCtx ctx_boiler = {DeviceKind::Boiler};
static DeviceCtx ctx_squares = {DeviceKind::Squares};
static DeviceCtx ctx_grates = {DeviceKind::Grates};
static DeviceCtx ctx_sconces = {DeviceKind::Sconces};
static DeviceCtx ctx_crawlspace = {DeviceKind::Crawlspace};

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

static bool parse_color_name(const char *name, CRGB &out) {
  if (!name || !name[0]) return false;
  String s(name);
  s.toLowerCase();
  if (s == "yellow") out = CRGB::Yellow;
  else if (s == "red") out = CRGB::Red;
  else if (s == "green") out = CRGB::Green;
  else if (s == "blue") out = CRGB::Blue;
  else if (s == "white") out = CRGB::White;
  else if (s == "purple") out = CRGB::Purple;
  else if (s == "orange") out = CRGB::Orange;
  else return false;
  return true;
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

static CRGB scale_color(CRGB c, uint8_t brightness) {
  CRGB out = c;
  out.nscale8_video(brightness);
  return out;
}

static void apply_dimmers() {
  analogWrite(study_lights_pin, study_dimmer);
  analogWrite(boiler_lights_pin, boiler_dimmer);
}

static void apply_relays() {
  digitalWrite(sconces_pin, sconces_on ? HIGH : LOW);
  digitalWrite(crawlspace_lights_pin, crawlspace_lights_on ? HIGH : LOW);
}

static void apply_lab_squares() {
  if (!lab_squares_on || lab_squares_brightness == 0) {
    fill_solid(leds_sa, num_leds_per_strip, CRGB::Black);
    fill_solid(leds_sb, num_leds_per_strip, CRGB::Black);
    fill_solid(leds_sc, num_leds_per_strip, CRGB::Black);
    fill_solid(leds_sd, num_leds_per_strip, CRGB::Black);
    fill_solid(leds_se, num_leds_per_strip, CRGB::Black);
    fill_solid(leds_sf, num_leds_per_strip, CRGB::Black);
    fill_solid(leds_sg, num_leds_per_strip, CRGB::Black);
    fill_solid(leds_sh, num_leds_per_strip, CRGB::Black);
    return;
  }
  CRGB c = scale_color(lab_squares_color, lab_squares_brightness);
  fill_solid(leds_sa, num_leds_per_strip, c);
  fill_solid(leds_sb, num_leds_per_strip, c);
  fill_solid(leds_sc, num_leds_per_strip, c);
  fill_solid(leds_sd, num_leds_per_strip, c);
  fill_solid(leds_se, num_leds_per_strip, c);
  fill_solid(leds_sf, num_leds_per_strip, c);
  fill_solid(leds_sg, num_leds_per_strip, c);
  fill_solid(leds_sh, num_leds_per_strip, c);
}

static void apply_lab_grates() {
  if (!lab_grates_on || lab_grates_brightness == 0) {
    fill_solid(leds_g1, num_leds_per_strip, CRGB::Black);
    fill_solid(leds_g2, num_leds_per_strip, CRGB::Black);
    fill_solid(leds_g3, num_leds_per_strip, CRGB::Black);
    return;
  }
  CRGB c = scale_color(lab_grates_color, lab_grates_brightness);
  fill_solid(leds_g1, num_leds_per_strip, c);
  fill_solid(leds_g2, num_leds_per_strip, c);
  fill_solid(leds_g3, num_leds_per_strip, c);
}

static void apply_outputs() {
  digitalWrite(power_led_pin, HIGH);
  apply_dimmers();
  apply_relays();
  apply_lab_squares();
  apply_lab_grates();
  FastLED.show();
}

static void publish_state_study() {
  StaticJsonDocument<192> st;
  st["brightness"] = study_dimmer;
  sne_study.publishState(st);
}

static void publish_state_boiler() {
  StaticJsonDocument<192> st;
  st["brightness"] = boiler_dimmer;
  sne_boiler.publishState(st);
}

static void publish_state_squares() {
  StaticJsonDocument<256> st;
  st["on"] = lab_squares_on;
  st["brightness"] = lab_squares_brightness;
  JsonObject c = st.createNestedObject("color");
  c["r"] = lab_squares_color.r;
  c["g"] = lab_squares_color.g;
  c["b"] = lab_squares_color.b;
  sne_squares.publishState(st);
}

static void publish_state_grates() {
  StaticJsonDocument<256> st;
  st["on"] = lab_grates_on;
  st["brightness"] = lab_grates_brightness;
  JsonObject c = st.createNestedObject("color");
  c["r"] = lab_grates_color.r;
  c["g"] = lab_grates_color.g;
  c["b"] = lab_grates_color.b;
  sne_grates.publishState(st);
}

static void publish_state_sconces() {
  StaticJsonDocument<192> st;
  st["on"] = sconces_on;
  sne_sconces.publishState(st);
}

static void publish_state_crawlspace() {
  StaticJsonDocument<192> st;
  st["on"] = crawlspace_lights_on;
  sne_crawlspace.publishState(st);
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

  if (ctx->kind == DeviceKind::Study) {
    if (strcmp(op, "set_brightness") != 0) goto bad;
    int brightness = p["brightness"] | (p["value"] | 255);
    brightness = constrain(brightness, 0, 255);
    study_dimmer = (uint8_t)brightness;
    apply_outputs();
    publish_state_study();
    return true;
  }

  if (ctx->kind == DeviceKind::Boiler) {
    if (strcmp(op, "set_brightness") != 0) goto bad;
    int brightness = p["brightness"] | (p["value"] | 255);
    brightness = constrain(brightness, 0, 255);
    boiler_dimmer = (uint8_t)brightness;
    apply_outputs();
    publish_state_boiler();
    return true;
  }

  if (ctx->kind == DeviceKind::Squares) {
    if (strcmp(op, "set_brightness") == 0) {
      int brightness = p["brightness"] | (p["value"] | 255);
      brightness = constrain(brightness, 0, 255);
      lab_squares_brightness = (uint8_t)brightness;
      lab_squares_on = (lab_squares_brightness > 0);
      apply_outputs();
      publish_state_squares();
      return true;
    }
    if (strcmp(op, "set_color") == 0) {
      if (p.containsKey("color") && p["color"].is<const char *>()) {
        CRGB c;
        if (!parse_color_name(p["color"], c)) goto bad;
        lab_squares_color = c;
      } else if (p.containsKey("rgb")) {
        CRGB c;
        if (!parse_rgb(p["rgb"], c)) goto bad;
        lab_squares_color = c;
      } else {
        goto bad;
      }
      lab_squares_on = (lab_squares_brightness > 0);
      apply_outputs();
      publish_state_squares();
      return true;
    }
    goto bad;
  }

  if (ctx->kind == DeviceKind::Grates) {
    if (strcmp(op, "set_brightness") == 0) {
      int brightness = p["brightness"] | (p["value"] | 255);
      brightness = constrain(brightness, 0, 255);
      lab_grates_brightness = (uint8_t)brightness;
      lab_grates_on = (lab_grates_brightness > 0);
      apply_outputs();
      publish_state_grates();
      return true;
    }
    if (strcmp(op, "set_color") == 0) {
      if (p.containsKey("color") && p["color"].is<const char *>()) {
        CRGB c;
        if (!parse_color_name(p["color"], c)) goto bad;
        lab_grates_color = c;
      } else if (p.containsKey("rgb")) {
        CRGB c;
        if (!parse_rgb(p["rgb"], c)) goto bad;
        lab_grates_color = c;
      } else {
        goto bad;
      }
      lab_grates_on = (lab_grates_brightness > 0);
      apply_outputs();
      publish_state_grates();
      return true;
    }
    goto bad;
  }

  if (ctx->kind == DeviceKind::Sconces) {
    if (strcmp(op, "set") == 0) {
      if (!p.containsKey("on")) goto bad;
      sconces_on = p["on"] | false;
    } else if (strcmp(op, "on") == 0) {
      sconces_on = true;
    } else if (strcmp(op, "off") == 0) {
      sconces_on = false;
    } else {
      goto bad;
    }
    apply_outputs();
    publish_state_sconces();
    return true;
  }

  if (ctx->kind == DeviceKind::Crawlspace) {
    if (strcmp(op, "set") == 0) {
      if (!p.containsKey("on")) goto bad;
      crawlspace_lights_on = p["on"] | false;
    } else if (strcmp(op, "on") == 0) {
      crawlspace_lights_on = true;
    } else if (strcmp(op, "off") == 0) {
      crawlspace_lights_on = false;
    } else {
      goto bad;
    }
    apply_outputs();
    publish_state_crawlspace();
    return true;
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
  digitalWrite(power_led_pin, HIGH);

  pinMode(study_lights_pin, OUTPUT);
  pinMode(boiler_lights_pin, OUTPUT);

  pinMode(sconces_pin, OUTPUT);
  pinMode(crawlspace_lights_pin, OUTPUT);

  // Initialize LED strips and default OFF state.
  FastLED.addLeds<WS2812B, ceiling_square_a_pin, GRB>(leds_sa, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_b_pin, GRB>(leds_sb, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_c_pin, GRB>(leds_sc, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_d_pin, GRB>(leds_sd, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_e_pin, GRB>(leds_se, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_f_pin, GRB>(leds_sf, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_g_pin, GRB>(leds_sg, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_h_pin, GRB>(leds_sh, num_leds_per_strip);
  FastLED.addLeds<WS2812B, grate_1_pin, GRB>(leds_g1, num_leds_per_strip);
  FastLED.addLeds<WS2812B, grate_2_pin, GRB>(leds_g2, num_leds_per_strip);
  FastLED.addLeds<WS2812B, grate_3_pin, GRB>(leds_g3, num_leds_per_strip);

  FastLED.setBrightness(255);

  // Safe defaults.
  study_dimmer = 0;
  boiler_dimmer = 0;
  lab_squares_on = false;
  lab_squares_brightness = 255;
  lab_squares_color = CRGB::Yellow;
  lab_grates_on = false;
  lab_grates_brightness = 255;
  lab_grates_color = CRGB::Blue;
  sconces_on = false;
  crawlspace_lights_on = false;

  apply_outputs();

  if (!sne_study.begin()) while (true) delay(1000);
  if (!sne_boiler.begin()) while (true) delay(1000);
  if (!sne_squares.begin()) while (true) delay(1000);
  if (!sne_grates.begin()) while (true) delay(1000);
  if (!sne_sconces.begin()) while (true) delay(1000);
  if (!sne_crawlspace.begin()) while (true) delay(1000);

  sne_study.setCommandHandler(handleCommand, &ctx_study);
  sne_boiler.setCommandHandler(handleCommand, &ctx_boiler);
  sne_squares.setCommandHandler(handleCommand, &ctx_squares);
  sne_grates.setCommandHandler(handleCommand, &ctx_grates);
  sne_sconces.setCommandHandler(handleCommand, &ctx_sconces);
  sne_crawlspace.setCommandHandler(handleCommand, &ctx_crawlspace);

  publish_state_study();
  publish_state_boiler();
  publish_state_squares();
  publish_state_grates();
  publish_state_sconces();
  publish_state_crawlspace();
}

void loop() {
  sne_study.loop();
  sne_boiler.loop();
  sne_squares.loop();
  sne_grates.loop();
  sne_sconces.loop();
  sne_crawlspace.loop();
}
