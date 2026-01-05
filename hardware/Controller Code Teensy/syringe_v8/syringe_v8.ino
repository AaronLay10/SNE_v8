// syringe_v8 — Syringe Puzzle Controller (Teensy 4.1)
//
// Ported from v2 firmware (stateless executor):
// - 6 rotary encoders (input)
// - 6 WS2812B LED rings (12 LEDs each)
// - filament LED (on/off)
// - main actuator (up/down/stop)
// - forge actuator (extend/retract)
//
// v8 behavior:
// - Option 2 device identity: one v8 `device_id` per logical device
// - One MQTT connection per `device_id` (correct LWT OFFLINE semantics)
// - Commands are `action="SET"` and require `parameters.op` (string)

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

// --- Pins (ported from v2) ---
static const uint8_t PIN_POWER_LED = 13;
static const uint8_t PIN_FILAMENT_LED = 30;

// LED Ring Pins
static const uint8_t PIN_LED_RING_A = 24;
static const uint8_t PIN_LED_RING_B = 25;
static const uint8_t PIN_LED_RING_C = 26;
static const uint8_t PIN_LED_RING_D = 27;
static const uint8_t PIN_LED_RING_E = 28;
static const uint8_t PIN_LED_RING_F = 29;
static const int NUM_LEDS = 12;

// Actuator Pins
static const uint8_t PIN_FORGE_ACTUATOR_RETRACT = 31;
static const uint8_t PIN_FORGE_ACTUATOR_EXTEND = 32;
static const uint8_t PIN_ACTUATORS_UP = 33;
static const uint8_t PIN_ACTUATORS_DN = 34;

// Rotary Encoder Pins
static const uint8_t PIN_ENCODER_LT_A = 18;
static const uint8_t PIN_ENCODER_LT_B = 19;
static const uint8_t PIN_ENCODER_LM_A = 16;
static const uint8_t PIN_ENCODER_LM_B = 17;
static const uint8_t PIN_ENCODER_LB_A = 14;
static const uint8_t PIN_ENCODER_LB_B = 15;
static const uint8_t PIN_ENCODER_RT_A = 20;
static const uint8_t PIN_ENCODER_RT_B = 21;
static const uint8_t PIN_ENCODER_RM_A = 40;
static const uint8_t PIN_ENCODER_RM_B = 41;
static const uint8_t PIN_ENCODER_RB_A = 22;
static const uint8_t PIN_ENCODER_RB_B = 23;

// --- LEDs ---
static CRGB ring_a[NUM_LEDS];
static CRGB ring_b[NUM_LEDS];
static CRGB ring_c[NUM_LEDS];
static CRGB ring_d[NUM_LEDS];
static CRGB ring_e[NUM_LEDS];
static CRGB ring_f[NUM_LEDS];

static const char *ring_color_name[6] = {"blue", "red", "yellow", "green", "purple", "orange"};

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

static void set_ring_color(int idx, const char *color) {
  CRGB rgb = color_from_name(color);
  ring_color_name[idx] = color;
  switch (idx) {
    case 0: fill_solid(ring_a, NUM_LEDS, rgb); break;
    case 1: fill_solid(ring_b, NUM_LEDS, rgb); break;
    case 2: fill_solid(ring_c, NUM_LEDS, rgb); break;
    case 3: fill_solid(ring_d, NUM_LEDS, rgb); break;
    case 4: fill_solid(ring_e, NUM_LEDS, rgb); break;
    case 5: fill_solid(ring_f, NUM_LEDS, rgb); break;
  }
  FastLED.show();
}

// --- Encoders ---
static volatile long encoder_values[6] = {0, 0, 0, 0, 0, 0};
static int last_encoded[6] = {0, 0, 0, 0, 0, 0};
static long last_published_values[6] = {0, 0, 0, 0, 0, 0};

static void update_encoder(int index, int pinA, int pinB) {
  int MSB = digitalRead(pinA);
  int LSB = digitalRead(pinB);

  int encoded = (MSB << 1) | LSB;
  int sum = (last_encoded[index] << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoder_values[index]++;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoder_values[index]--;

  last_encoded[index] = encoded;
}

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

// --- v8 Clients ---
static sentient_v8::Config cfg_enc_lt = make_cfg("syringe_encoder_left_top", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_enc_lm = make_cfg("syringe_encoder_left_middle", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_enc_lb = make_cfg("syringe_encoder_left_bottom", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_enc_rt = make_cfg("syringe_encoder_right_top", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_enc_rm = make_cfg("syringe_encoder_right_middle", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_enc_rb = make_cfg("syringe_encoder_right_bottom", HMAC_KEY_PLACEHOLDER);

static sentient_v8::Config cfg_ring_a = make_cfg("syringe_led_ring_a", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_ring_b = make_cfg("syringe_led_ring_b", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_ring_c = make_cfg("syringe_led_ring_c", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_ring_d = make_cfg("syringe_led_ring_d", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_ring_e = make_cfg("syringe_led_ring_e", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_ring_f = make_cfg("syringe_led_ring_f", HMAC_KEY_PLACEHOLDER);

static sentient_v8::Config cfg_filament = make_cfg("syringe_filament_led", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_main_actuator = make_cfg("syringe_main_actuator", HMAC_KEY_PLACEHOLDER);
static sentient_v8::Config cfg_forge_actuator = make_cfg("syringe_forge_actuator", HMAC_KEY_PLACEHOLDER);

static sentient_v8::Client sne_enc_lt(cfg_enc_lt);
static sentient_v8::Client sne_enc_lm(cfg_enc_lm);
static sentient_v8::Client sne_enc_lb(cfg_enc_lb);
static sentient_v8::Client sne_enc_rt(cfg_enc_rt);
static sentient_v8::Client sne_enc_rm(cfg_enc_rm);
static sentient_v8::Client sne_enc_rb(cfg_enc_rb);

static sentient_v8::Client sne_ring_a(cfg_ring_a);
static sentient_v8::Client sne_ring_b(cfg_ring_b);
static sentient_v8::Client sne_ring_c(cfg_ring_c);
static sentient_v8::Client sne_ring_d(cfg_ring_d);
static sentient_v8::Client sne_ring_e(cfg_ring_e);
static sentient_v8::Client sne_ring_f(cfg_ring_f);

static sentient_v8::Client sne_filament(cfg_filament);
static sentient_v8::Client sne_main_actuator(cfg_main_actuator);
static sentient_v8::Client sne_forge_actuator(cfg_forge_actuator);

static sentient_v8::Client *clients[] = {&sne_enc_lt, &sne_enc_lm, &sne_enc_lb, &sne_enc_rt, &sne_enc_rm, &sne_enc_rb,
                                         &sne_ring_a, &sne_ring_b, &sne_ring_c, &sne_ring_d, &sne_ring_e, &sne_ring_f,
                                         &sne_filament, &sne_main_actuator, &sne_forge_actuator};

enum class DeviceKind : uint8_t {
  EncLt,
  EncLm,
  EncLb,
  EncRt,
  EncRm,
  EncRb,
  RingA,
  RingB,
  RingC,
  RingD,
  RingE,
  RingF,
  Filament,
  MainActuator,
  ForgeActuator,
};

struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_enc_lt = {DeviceKind::EncLt};
static DeviceCtx ctx_enc_lm = {DeviceKind::EncLm};
static DeviceCtx ctx_enc_lb = {DeviceKind::EncLb};
static DeviceCtx ctx_enc_rt = {DeviceKind::EncRt};
static DeviceCtx ctx_enc_rm = {DeviceKind::EncRm};
static DeviceCtx ctx_enc_rb = {DeviceKind::EncRb};

static DeviceCtx ctx_ring_a = {DeviceKind::RingA};
static DeviceCtx ctx_ring_b = {DeviceKind::RingB};
static DeviceCtx ctx_ring_c = {DeviceKind::RingC};
static DeviceCtx ctx_ring_d = {DeviceKind::RingD};
static DeviceCtx ctx_ring_e = {DeviceKind::RingE};
static DeviceCtx ctx_ring_f = {DeviceKind::RingF};

static DeviceCtx ctx_filament = {DeviceKind::Filament};
static DeviceCtx ctx_main_actuator = {DeviceKind::MainActuator};
static DeviceCtx ctx_forge_actuator = {DeviceKind::ForgeActuator};

static bool filament_on = true;
static const char *main_actuator_state = "up";
static const char *forge_actuator_state = "retract";

static const unsigned long ENCODER_PUBLISH_MS = 100;
static const unsigned long STATE_REFRESH_MS = 60UL * 1000UL;
static unsigned long last_encoder_publish = 0;
static unsigned long last_periodic_publish = 0;

static void publish_encoder_state(sentient_v8::Client &client, int idx, const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "syringe_v8";
  st["reason"] = reason ? reason : "";
  st["encoder_count"] = (long)encoder_values[idx];
  client.publishState(st);
}

static void publish_ring_state(sentient_v8::Client &client, int idx, const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "syringe_v8";
  st["reason"] = reason ? reason : "";
  st["color"] = ring_color_name[idx];
  client.publishState(st);
}

static void publish_filament_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "syringe_v8";
  st["reason"] = reason ? reason : "";
  st["on"] = filament_on;
  sne_filament.publishState(st);
}

static void publish_main_actuator_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "syringe_v8";
  st["reason"] = reason ? reason : "";
  st["state"] = main_actuator_state;
  sne_main_actuator.publishState(st);
}

static void publish_forge_actuator_state(const char *reason) {
  StaticJsonDocument<256> st;
  st["firmware_version"] = "syringe_v8";
  st["reason"] = reason ? reason : "";
  st["state"] = forge_actuator_state;
  sne_forge_actuator.publishState(st);
}

static void publish_all_state(const char *reason) {
  publish_encoder_state(sne_enc_lt, 0, reason);
  publish_encoder_state(sne_enc_lm, 1, reason);
  publish_encoder_state(sne_enc_lb, 2, reason);
  publish_encoder_state(sne_enc_rt, 3, reason);
  publish_encoder_state(sne_enc_rm, 4, reason);
  publish_encoder_state(sne_enc_rb, 5, reason);

  publish_ring_state(sne_ring_a, 0, reason);
  publish_ring_state(sne_ring_b, 1, reason);
  publish_ring_state(sne_ring_c, 2, reason);
  publish_ring_state(sne_ring_d, 3, reason);
  publish_ring_state(sne_ring_e, 4, reason);
  publish_ring_state(sne_ring_f, 5, reason);

  publish_filament_state(reason);
  publish_main_actuator_state(reason);
  publish_forge_actuator_state(reason);
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

  // Encoders are input-only, except reset_counter.
  if (ctx->kind == DeviceKind::EncLt || ctx->kind == DeviceKind::EncLm || ctx->kind == DeviceKind::EncLb ||
      ctx->kind == DeviceKind::EncRt || ctx->kind == DeviceKind::EncRm || ctx->kind == DeviceKind::EncRb) {
    if (strcmp(op, "reset_counter") == 0) {
      int idx = (ctx->kind == DeviceKind::EncLt) ? 0 : (ctx->kind == DeviceKind::EncLm) ? 1
                                : (ctx->kind == DeviceKind::EncLb) ? 2
                                : (ctx->kind == DeviceKind::EncRt) ? 3
                                : (ctx->kind == DeviceKind::EncRm) ? 4
                                                                  : 5;
      long value = p.containsKey("value") ? (long)(p["value"] | 0) : 0;
      noInterrupts();
      encoder_values[idx] = value;
      interrupts();
      last_published_values[idx] = value;
      publish_encoder_state((idx == 0) ? sne_enc_lt
                           : (idx == 1) ? sne_enc_lm
                           : (idx == 2) ? sne_enc_lb
                           : (idx == 3) ? sne_enc_rt
                           : (idx == 4) ? sne_enc_rm
                                        : sne_enc_rb,
                           idx, "reset_counter");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // LED rings
  if (ctx->kind == DeviceKind::RingA || ctx->kind == DeviceKind::RingB || ctx->kind == DeviceKind::RingC ||
      ctx->kind == DeviceKind::RingD || ctx->kind == DeviceKind::RingE || ctx->kind == DeviceKind::RingF) {
    int idx = (ctx->kind == DeviceKind::RingA) ? 0 : (ctx->kind == DeviceKind::RingB) ? 1
                        : (ctx->kind == DeviceKind::RingC) ? 2
                        : (ctx->kind == DeviceKind::RingD) ? 3
                        : (ctx->kind == DeviceKind::RingE) ? 4
                                                           : 5;
    const char *color = nullptr;
    if (strcmp(op, "set_color") == 0) {
      if (!p.containsKey("color")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      color = p["color"] | "";
    } else if (strcmp(op, "off") == 0) {
      color = "off";
    } else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }

    set_ring_color(idx, color);
    publish_ring_state((idx == 0) ? sne_ring_a
                      : (idx == 1) ? sne_ring_b
                      : (idx == 2) ? sne_ring_c
                      : (idx == 3) ? sne_ring_d
                      : (idx == 4) ? sne_ring_e
                                   : sne_ring_f,
                      idx, op);
    return true;
  }

  // Filament LED
  if (ctx->kind == DeviceKind::Filament) {
    if (strcmp(op, "on") == 0) filament_on = true;
    else if (strcmp(op, "off") == 0) filament_on = false;
    else {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    digitalWrite(PIN_FILAMENT_LED, filament_on ? HIGH : LOW);
    publish_filament_state(op);
    return true;
  }

  // Main actuator
  if (ctx->kind == DeviceKind::MainActuator) {
    if (strcmp(op, "up") == 0) {
      digitalWrite(PIN_ACTUATORS_UP, HIGH);
      digitalWrite(PIN_ACTUATORS_DN, LOW);
      main_actuator_state = "up";
      publish_main_actuator_state(op);
      return true;
    }
    if (strcmp(op, "down") == 0) {
      digitalWrite(PIN_ACTUATORS_UP, LOW);
      digitalWrite(PIN_ACTUATORS_DN, HIGH);
      main_actuator_state = "down";
      publish_main_actuator_state(op);
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      digitalWrite(PIN_ACTUATORS_UP, LOW);
      digitalWrite(PIN_ACTUATORS_DN, LOW);
      main_actuator_state = "stopped";
      publish_main_actuator_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Forge actuator
  if (ctx->kind == DeviceKind::ForgeActuator) {
    if (strcmp(op, "extend") == 0) {
      digitalWrite(PIN_FORGE_ACTUATOR_EXTEND, HIGH);
      digitalWrite(PIN_FORGE_ACTUATOR_RETRACT, LOW);
      forge_actuator_state = "extend";
      publish_forge_actuator_state(op);
      return true;
    }
    if (strcmp(op, "retract") == 0) {
      digitalWrite(PIN_FORGE_ACTUATOR_EXTEND, LOW);
      digitalWrite(PIN_FORGE_ACTUATOR_RETRACT, HIGH);
      forge_actuator_state = "retract";
      publish_forge_actuator_state(op);
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

static void monitor_encoders() {
  const unsigned long now = millis();
  if (now - last_encoder_publish < ENCODER_PUBLISH_MS) return;

  for (int i = 0; i < 6; i++) {
    long v = encoder_values[i];
    if (v != last_published_values[i]) {
      if (i == 0) publish_encoder_state(sne_enc_lt, i, "change");
      if (i == 1) publish_encoder_state(sne_enc_lm, i, "change");
      if (i == 2) publish_encoder_state(sne_enc_lb, i, "change");
      if (i == 3) publish_encoder_state(sne_enc_rt, i, "change");
      if (i == 4) publish_encoder_state(sne_enc_rm, i, "change");
      if (i == 5) publish_encoder_state(sne_enc_rb, i, "change");
      last_published_values[i] = v;
    }
  }

  last_encoder_publish = now;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  pinMode(PIN_FILAMENT_LED, OUTPUT);
  filament_on = true;
  digitalWrite(PIN_FILAMENT_LED, HIGH);

  pinMode(PIN_ACTUATORS_UP, OUTPUT);
  pinMode(PIN_ACTUATORS_DN, OUTPUT);
  // v2 boot default: UP asserted.
  digitalWrite(PIN_ACTUATORS_UP, HIGH);
  digitalWrite(PIN_ACTUATORS_DN, LOW);
  main_actuator_state = "up";

  pinMode(PIN_FORGE_ACTUATOR_EXTEND, OUTPUT);
  pinMode(PIN_FORGE_ACTUATOR_RETRACT, OUTPUT);
  digitalWrite(PIN_FORGE_ACTUATOR_EXTEND, LOW);
  digitalWrite(PIN_FORGE_ACTUATOR_RETRACT, HIGH);
  forge_actuator_state = "retract";

  // FastLED setup
  FastLED.addLeds<WS2812B, PIN_LED_RING_A, GRB>(ring_a, NUM_LEDS);
  FastLED.addLeds<WS2812B, PIN_LED_RING_B, GRB>(ring_b, NUM_LEDS);
  FastLED.addLeds<WS2812B, PIN_LED_RING_C, GRB>(ring_c, NUM_LEDS);
  FastLED.addLeds<WS2812B, PIN_LED_RING_D, GRB>(ring_d, NUM_LEDS);
  FastLED.addLeds<WS2812B, PIN_LED_RING_E, GRB>(ring_e, NUM_LEDS);
  FastLED.addLeds<WS2812B, PIN_LED_RING_F, GRB>(ring_f, NUM_LEDS);

  // Initial ring colors (v2 defaults)
  set_ring_color(0, "blue");
  set_ring_color(1, "red");
  set_ring_color(2, "yellow");
  set_ring_color(3, "green");
  set_ring_color(4, "purple");
  set_ring_color(5, "orange");

  // Encoder pins
  pinMode(PIN_ENCODER_LT_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_LT_B, INPUT_PULLUP);
  pinMode(PIN_ENCODER_LM_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_LM_B, INPUT_PULLUP);
  pinMode(PIN_ENCODER_LB_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_LB_B, INPUT_PULLUP);
  pinMode(PIN_ENCODER_RT_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_RT_B, INPUT_PULLUP);
  pinMode(PIN_ENCODER_RM_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_RM_B, INPUT_PULLUP);
  pinMode(PIN_ENCODER_RB_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_RB_B, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LT_A), []() { update_encoder(0, PIN_ENCODER_LT_A, PIN_ENCODER_LT_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LT_B), []() { update_encoder(0, PIN_ENCODER_LT_A, PIN_ENCODER_LT_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LM_A), []() { update_encoder(1, PIN_ENCODER_LM_A, PIN_ENCODER_LM_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LM_B), []() { update_encoder(1, PIN_ENCODER_LM_A, PIN_ENCODER_LM_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LB_A), []() { update_encoder(2, PIN_ENCODER_LB_A, PIN_ENCODER_LB_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LB_B), []() { update_encoder(2, PIN_ENCODER_LB_A, PIN_ENCODER_LB_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RT_A), []() { update_encoder(3, PIN_ENCODER_RT_A, PIN_ENCODER_RT_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RT_B), []() { update_encoder(3, PIN_ENCODER_RT_A, PIN_ENCODER_RT_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RM_A), []() { update_encoder(4, PIN_ENCODER_RM_A, PIN_ENCODER_RM_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RM_B), []() { update_encoder(4, PIN_ENCODER_RM_A, PIN_ENCODER_RM_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RB_A), []() { update_encoder(5, PIN_ENCODER_RB_A, PIN_ENCODER_RB_B); }, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RB_B), []() { update_encoder(5, PIN_ENCODER_RB_A, PIN_ENCODER_RB_B); }, CHANGE);

  if (!sne_enc_lt.begin()) while (true) delay(1000);
  if (!sne_enc_lm.begin()) while (true) delay(1000);
  if (!sne_enc_lb.begin()) while (true) delay(1000);
  if (!sne_enc_rt.begin()) while (true) delay(1000);
  if (!sne_enc_rm.begin()) while (true) delay(1000);
  if (!sne_enc_rb.begin()) while (true) delay(1000);
  if (!sne_ring_a.begin()) while (true) delay(1000);
  if (!sne_ring_b.begin()) while (true) delay(1000);
  if (!sne_ring_c.begin()) while (true) delay(1000);
  if (!sne_ring_d.begin()) while (true) delay(1000);
  if (!sne_ring_e.begin()) while (true) delay(1000);
  if (!sne_ring_f.begin()) while (true) delay(1000);
  if (!sne_filament.begin()) while (true) delay(1000);
  if (!sne_main_actuator.begin()) while (true) delay(1000);
  if (!sne_forge_actuator.begin()) while (true) delay(1000);

  sne_enc_lt.setCommandHandler(handleCommand, &ctx_enc_lt);
  sne_enc_lm.setCommandHandler(handleCommand, &ctx_enc_lm);
  sne_enc_lb.setCommandHandler(handleCommand, &ctx_enc_lb);
  sne_enc_rt.setCommandHandler(handleCommand, &ctx_enc_rt);
  sne_enc_rm.setCommandHandler(handleCommand, &ctx_enc_rm);
  sne_enc_rb.setCommandHandler(handleCommand, &ctx_enc_rb);

  sne_ring_a.setCommandHandler(handleCommand, &ctx_ring_a);
  sne_ring_b.setCommandHandler(handleCommand, &ctx_ring_b);
  sne_ring_c.setCommandHandler(handleCommand, &ctx_ring_c);
  sne_ring_d.setCommandHandler(handleCommand, &ctx_ring_d);
  sne_ring_e.setCommandHandler(handleCommand, &ctx_ring_e);
  sne_ring_f.setCommandHandler(handleCommand, &ctx_ring_f);

  sne_filament.setCommandHandler(handleCommand, &ctx_filament);
  sne_main_actuator.setCommandHandler(handleCommand, &ctx_main_actuator);
  sne_forge_actuator.setCommandHandler(handleCommand, &ctx_forge_actuator);

  publish_all_state("boot");
  last_encoder_publish = millis();
  last_periodic_publish = millis();
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();
  monitor_encoders();

  const unsigned long now = millis();
  if (now - last_periodic_publish >= STATE_REFRESH_MS) {
    publish_all_state("periodic");
    last_periodic_publish = now;
  }
}
