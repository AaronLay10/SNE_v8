// Lever Riddle Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - lever_riddle_hall_sensor_a
// - lever_riddle_hall_sensor_b
// - lever_riddle_hall_sensor_c
// - lever_riddle_hall_sensor_d
// - lever_riddle_photocell_lever
// - lever_riddle_cube_button
// - lever_riddle_ir_receiver
// - lever_riddle_maglock
// - lever_riddle_led_strip_main
// - lever_riddle_led_strip_lever
// - lever_riddle_cob_light

#include <Arduino.h>
#include <ArduinoJson.h>

// Suppress IRremote begin() error - receiver only.
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>

#include <FastLED.h>
#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_HALL_A = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_HALL_B = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_HALL_C = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_HALL_D = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_PHOTOCELL = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BUTTON = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_IR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MAGLOCK = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LED_MAIN = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LED_LEVER = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_COB = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (from v2)
static const int PIN_POWER_LED = 13;
static const int PIN_IR_RECEIVE = 25;
static const int PIN_MAGLOCK = 26;
static const int PIN_HALL_A = 5;
static const int PIN_HALL_B = 6;
static const int PIN_HALL_C = 7;
static const int PIN_HALL_D = 8;
static const int PIN_LED_STRIP_MAIN = 1;
static const int PIN_LED_STRIP_LEVER = 12;
static const int PIN_PHOTOCELL = A10;
static const int PIN_CUBE_BUTTON = 32;
static const int PIN_COB_LIGHT = 30;

static const int NUM_LEDS_MAIN = 9;
static const int NUM_LEDS_LEVER = 10;
static const int BRIGHTNESS = 255;

static const unsigned long SENSOR_REFRESH_MS = 60UL * 1000UL;
static const uint8_t EXPECTED_GUN_IR_CODE = 0x51;

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

static sentient_v8::Client sne_hall_a(make_cfg("lever_riddle_hall_sensor_a", HMAC_HALL_A));
static sentient_v8::Client sne_hall_b(make_cfg("lever_riddle_hall_sensor_b", HMAC_HALL_B));
static sentient_v8::Client sne_hall_c(make_cfg("lever_riddle_hall_sensor_c", HMAC_HALL_C));
static sentient_v8::Client sne_hall_d(make_cfg("lever_riddle_hall_sensor_d", HMAC_HALL_D));
static sentient_v8::Client sne_photocell(make_cfg("lever_riddle_photocell_lever", HMAC_PHOTOCELL));
static sentient_v8::Client sne_cube_button(make_cfg("lever_riddle_cube_button", HMAC_BUTTON));
static sentient_v8::Client sne_ir(make_cfg("lever_riddle_ir_receiver", HMAC_IR));
static sentient_v8::Client sne_maglock(make_cfg("lever_riddle_maglock", HMAC_MAGLOCK));
static sentient_v8::Client sne_led_main(make_cfg("lever_riddle_led_strip_main", HMAC_LED_MAIN));
static sentient_v8::Client sne_led_lever(make_cfg("lever_riddle_led_strip_lever", HMAC_LED_LEVER));
static sentient_v8::Client sne_cob(make_cfg("lever_riddle_cob_light", HMAC_COB));

enum class DeviceKind : uint8_t {
  HallA,
  HallB,
  HallC,
  HallD,
  Photocell,
  CubeButton,
  IrReceiver,
  Maglock,
  LedMain,
  LedLever,
  CobLight,
};

struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_hall_a = {DeviceKind::HallA};
static DeviceCtx ctx_hall_b = {DeviceKind::HallB};
static DeviceCtx ctx_hall_c = {DeviceKind::HallC};
static DeviceCtx ctx_hall_d = {DeviceKind::HallD};
static DeviceCtx ctx_photocell = {DeviceKind::Photocell};
static DeviceCtx ctx_cube_button = {DeviceKind::CubeButton};
static DeviceCtx ctx_ir = {DeviceKind::IrReceiver};
static DeviceCtx ctx_maglock = {DeviceKind::Maglock};
static DeviceCtx ctx_led_main = {DeviceKind::LedMain};
static DeviceCtx ctx_led_lever = {DeviceKind::LedLever};
static DeviceCtx ctx_cob = {DeviceKind::CobLight};

static CRGB leds_main[NUM_LEDS_MAIN];
static CRGB leds_lever[NUM_LEDS_LEVER];

static bool hall_a = false, hall_b = false, hall_c = false, hall_d = false;
static bool last_hall_a = false, last_hall_b = false, last_hall_c = false, last_hall_d = false;

static int photocell_value = 0;
static int last_photocell_value = -1;
static const int PHOTOCELL_THRESHOLD = 500;

static bool cube_button = false;
static bool last_cube_button = false;

static bool ir_enabled = true;
static uint32_t last_ir_raw_data = 0;
static unsigned long last_ir_timestamp = 0;
static uint8_t last_ir_code = 0;
static bool ir_seen = false;

static bool maglock_locked = true;
static bool cob_on = true;
static const char *led_main_color = "red";
static const char *led_lever_color = "white";

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

static CRGB color_from_name(const char *color) {
  if (!color) return CRGB::White;
  if (strcmp(color, "red") == 0) return CRGB::Red;
  if (strcmp(color, "green") == 0) return CRGB::Green;
  if (strcmp(color, "blue") == 0) return CRGB::Blue;
  if (strcmp(color, "yellow") == 0) return CRGB::Yellow;
  if (strcmp(color, "purple") == 0) return CRGB::Purple;
  if (strcmp(color, "white") == 0) return CRGB::White;
  if (strcmp(color, "off") == 0) return CRGB::Black;
  return CRGB::White;
}

static void set_led_color(CRGB *led_array, int count, const char *color) {
  fill_solid(led_array, count, color_from_name(color));
  FastLED.show();
}

static void publish_all_state(const char *reason) {
  {
    StaticJsonDocument<128> st;
    st["magnet_detected"] = hall_a;
    st["reason"] = reason ? reason : "";
    sne_hall_a.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["magnet_detected"] = hall_b;
    st["reason"] = reason ? reason : "";
    sne_hall_b.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["magnet_detected"] = hall_c;
    st["reason"] = reason ? reason : "";
    sne_hall_c.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["magnet_detected"] = hall_d;
    st["reason"] = reason ? reason : "";
    sne_hall_d.publishState(st);
  }
  {
    StaticJsonDocument<192> st;
    st["lever_position"] = (photocell_value > PHOTOCELL_THRESHOLD) ? "OPEN" : "CLOSED";
    st["raw_value"] = photocell_value;
    st["reason"] = reason ? reason : "";
    sne_photocell.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["pressed"] = cube_button;
    st["reason"] = reason ? reason : "";
    sne_cube_button.publishState(st);
  }
  {
    StaticJsonDocument<256> st;
    st["enabled"] = ir_enabled;
    st["expected_code"] = (uint8_t)0x51;
    if (ir_seen) {
      st["last_code"] = last_ir_code;
    }
    st["reason"] = reason ? reason : "";
    sne_ir.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["locked"] = maglock_locked;
    st["reason"] = reason ? reason : "";
    sne_maglock.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["color"] = led_main_color;
    st["reason"] = reason ? reason : "";
    sne_led_main.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["color"] = led_lever_color;
    st["reason"] = reason ? reason : "";
    sne_led_lever.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["on"] = cob_on;
    st["reason"] = reason ? reason : "";
    sne_cob.publishState(st);
  }
}

static void handle_ir_signal() {
  if (!IrReceiver.decode()) return;

  bool is_duplicate = (IrReceiver.decodedIRData.decodedRawData == last_ir_raw_data &&
                       (millis() - last_ir_timestamp) < 500);
  bool is_weak = (IrReceiver.decodedIRData.protocol == UNKNOWN || IrReceiver.decodedIRData.protocol == 2);

  if (!is_duplicate && !is_weak) {
    uint8_t code = (uint8_t)IrReceiver.decodedIRData.command;
    last_ir_code = code;
    ir_seen = true;

    StaticJsonDocument<256> ev;
    ev["ir_code"] = code;
    ev["address"] = (uint32_t)IrReceiver.decodedIRData.address;
    ev["protocol"] = (uint32_t)IrReceiver.decodedIRData.protocol;
    ev["raw"] = (uint32_t)IrReceiver.decodedIRData.decodedRawData;
    ev["expected"] = (uint8_t)EXPECTED_GUN_IR_CODE;
    ev["match"] = (code == EXPECTED_GUN_IR_CODE);
    ev["ts_ms"] = millis();
    sne_ir.publishTelemetry(ev);
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

  if (strcmp(op, "request_status") == 0) {
    publish_all_state("request_status");
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

  // Sensor-only devices reject state-changing ops.
  if (ctx->kind == DeviceKind::HallA || ctx->kind == DeviceKind::HallB || ctx->kind == DeviceKind::HallC ||
      ctx->kind == DeviceKind::HallD || ctx->kind == DeviceKind::Photocell || ctx->kind == DeviceKind::CubeButton) {
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::IrReceiver) {
    if (strcmp(op, "ir_enable") == 0) {
      ir_enabled = true;
      publish_all_state("ir_enable");
      return true;
    }
    if (strcmp(op, "ir_disable") == 0) {
      ir_enabled = false;
      publish_all_state("ir_disable");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::Maglock) {
    if (strcmp(op, "lock") == 0) {
      digitalWrite(PIN_MAGLOCK, HIGH);
      maglock_locked = true;
      publish_all_state("lock");
      return true;
    }
    if (strcmp(op, "unlock") == 0) {
      digitalWrite(PIN_MAGLOCK, LOW);
      maglock_locked = false;
      publish_all_state("unlock");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::CobLight) {
    if (strcmp(op, "set") == 0) {
      if (!p.containsKey("on")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      bool on = p["on"] | false;
      digitalWrite(PIN_COB_LIGHT, on ? HIGH : LOW);
      cob_on = on;
      publish_all_state("set");
      return true;
    }
    if (strcmp(op, "light_on") == 0) {
      digitalWrite(PIN_COB_LIGHT, HIGH);
      cob_on = true;
      publish_all_state("light_on");
      return true;
    }
    if (strcmp(op, "light_off") == 0) {
      digitalWrite(PIN_COB_LIGHT, LOW);
      cob_on = false;
      publish_all_state("light_off");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::LedMain || ctx->kind == DeviceKind::LedLever) {
    if (strcmp(op, "set_color") == 0) {
      const char *color = p["color"] | "";
      if (!color || !color[0]) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      if (ctx->kind == DeviceKind::LedMain) {
        led_main_color = color;
        set_led_color(leds_main, NUM_LEDS_MAIN, color);
      } else {
        led_lever_color = color;
        set_led_color(leds_lever, NUM_LEDS_LEVER, color);
      }
      publish_all_state("set_color");
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

  pinMode(PIN_MAGLOCK, OUTPUT);
  digitalWrite(PIN_MAGLOCK, HIGH);
  maglock_locked = true;

  pinMode(PIN_COB_LIGHT, OUTPUT);
  digitalWrite(PIN_COB_LIGHT, HIGH);
  cob_on = true;

  pinMode(PIN_HALL_A, INPUT_PULLUP);
  pinMode(PIN_HALL_B, INPUT_PULLUP);
  pinMode(PIN_HALL_C, INPUT_PULLUP);
  pinMode(PIN_HALL_D, INPUT_PULLUP);
  pinMode(PIN_PHOTOCELL, INPUT);
  pinMode(PIN_CUBE_BUTTON, INPUT_PULLUP);

  FastLED.addLeds<WS2812B, PIN_LED_STRIP_MAIN, RGB>(leds_main, NUM_LEDS_MAIN);
  FastLED.addLeds<WS2812B, PIN_LED_STRIP_LEVER, RGB>(leds_lever, NUM_LEDS_LEVER);
  FastLED.setBrightness(BRIGHTNESS);
  set_led_color(leds_main, NUM_LEDS_MAIN, led_main_color);
  set_led_color(leds_lever, NUM_LEDS_LEVER, led_lever_color);

  IrReceiver.begin(PIN_IR_RECEIVE, DISABLE_LED_FEEDBACK, PIN_POWER_LED);

  if (!sne_hall_a.begin()) while (true) delay(1000);
  if (!sne_hall_b.begin()) while (true) delay(1000);
  if (!sne_hall_c.begin()) while (true) delay(1000);
  if (!sne_hall_d.begin()) while (true) delay(1000);
  if (!sne_photocell.begin()) while (true) delay(1000);
  if (!sne_cube_button.begin()) while (true) delay(1000);
  if (!sne_ir.begin()) while (true) delay(1000);
  if (!sne_maglock.begin()) while (true) delay(1000);
  if (!sne_led_main.begin()) while (true) delay(1000);
  if (!sne_led_lever.begin()) while (true) delay(1000);
  if (!sne_cob.begin()) while (true) delay(1000);

  sne_hall_a.setCommandHandler(handleCommand, &ctx_hall_a);
  sne_hall_b.setCommandHandler(handleCommand, &ctx_hall_b);
  sne_hall_c.setCommandHandler(handleCommand, &ctx_hall_c);
  sne_hall_d.setCommandHandler(handleCommand, &ctx_hall_d);
  sne_photocell.setCommandHandler(handleCommand, &ctx_photocell);
  sne_cube_button.setCommandHandler(handleCommand, &ctx_cube_button);
  sne_ir.setCommandHandler(handleCommand, &ctx_ir);
  sne_maglock.setCommandHandler(handleCommand, &ctx_maglock);
  sne_led_main.setCommandHandler(handleCommand, &ctx_led_main);
  sne_led_lever.setCommandHandler(handleCommand, &ctx_led_lever);
  sne_cob.setCommandHandler(handleCommand, &ctx_cob);

  // Initial sensor reads
  hall_a = !digitalRead(PIN_HALL_A);
  hall_b = !digitalRead(PIN_HALL_B);
  hall_c = !digitalRead(PIN_HALL_C);
  hall_d = !digitalRead(PIN_HALL_D);
  last_hall_a = hall_a;
  last_hall_b = hall_b;
  last_hall_c = hall_c;
  last_hall_d = hall_d;

  photocell_value = analogRead(PIN_PHOTOCELL);
  last_photocell_value = photocell_value;

  cube_button = !digitalRead(PIN_CUBE_BUTTON);
  last_cube_button = cube_button;

  publish_all_state("boot");
}

void loop() {
  sne_hall_a.loop();
  sne_hall_b.loop();
  sne_hall_c.loop();
  sne_hall_d.loop();
  sne_photocell.loop();
  sne_cube_button.loop();
  sne_ir.loop();
  sne_maglock.loop();
  sne_led_main.loop();
  sne_led_lever.loop();
  sne_cob.loop();

  // Sensor monitoring
  hall_a = !digitalRead(PIN_HALL_A);
  hall_b = !digitalRead(PIN_HALL_B);
  hall_c = !digitalRead(PIN_HALL_C);
  hall_d = !digitalRead(PIN_HALL_D);

  int new_photocell = analogRead(PIN_PHOTOCELL);
  bool new_cube_button = !digitalRead(PIN_CUBE_BUTTON);

  bool changed = false;
  if (hall_a != last_hall_a) changed = true;
  if (hall_b != last_hall_b) changed = true;
  if (hall_c != last_hall_c) changed = true;
  if (hall_d != last_hall_d) changed = true;
  if (abs(new_photocell - last_photocell_value) > 50) changed = true;
  if (new_cube_button != last_cube_button) changed = true;

  photocell_value = new_photocell;
  cube_button = new_cube_button;

  if (ir_enabled) handle_ir_signal();

  static unsigned long last_publish = 0;
  const unsigned long now = millis();
  if (changed || (now - last_publish) > SENSOR_REFRESH_MS) {
    last_publish = now;
    last_hall_a = hall_a;
    last_hall_b = hall_b;
    last_hall_c = hall_c;
    last_hall_d = hall_d;
    last_cube_button = cube_button;
    if (abs(photocell_value - last_photocell_value) > 50) last_photocell_value = photocell_value;
    publish_all_state(changed ? "change" : "periodic");
  }
}
