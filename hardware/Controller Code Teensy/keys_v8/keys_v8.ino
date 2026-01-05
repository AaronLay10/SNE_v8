// Keys Puzzle Controller — v8 (Teensy 4.1)
//
// v8 integration:
// - Commands: `room/{room}/device/{device}/cmd` (CommandEnvelope, HMAC)
// - Acks:     `room/{room}/device/{device}/ack` (CommandAck)
// - State:    `room/{room}/device/{device}/state` (retained DeviceState)
// - Heartbeat:`room/{room}/device/{device}/heartbeat`
// - Presence: `room/{room}/device/{device}/presence` (retained ONLINE + LWT OFFLINE)
//
// Device identity (Option 2):
// - One Teensy hosts multiple v8 `device_id`s (one per key box color).
// - Each `device_id` is a separate MQTT connection to preserve correct LWT semantics.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>

#include <SentientV8.h>

// --- Per-device config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_GREEN_KEY_BOX = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_YELLOW_KEY_BOX = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BLUE_KEY_BOX = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RED_KEY_BOX = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (INPUT_PULLUP, active LOW)
static const int pin_green_bottom = 3;
static const int pin_green_right = 4;
static const int pin_yellow_right = 5;
static const int pin_yellow_top = 6;
static const int pin_blue_left = 7;
static const int pin_blue_bottom = 8;
static const int pin_red_left = 9;
static const int pin_red_bottom = 10;

// LEDs
static const int led_pin = 2;
static const int num_leds = 4;
static const int power_led_pin = 13;

static CRGB leds[num_leds];

static bool box_on[4] = {true, true, true, true};
static CRGB box_color[4] = {CRGB(136, 99, 8), CRGB(136, 99, 8), CRGB(136, 99, 8), CRGB(136, 99, 8)};

static bool s_green_bottom = false, s_green_right = false;
static bool s_yellow_right = false, s_yellow_top = false;
static bool s_blue_left = false, s_blue_bottom = false;
static bool s_red_left = false, s_red_bottom = false;

static bool p_green = false, p_yellow = false, p_blue = false, p_red = false;

static unsigned long last_state_publish[4] = {0, 0, 0, 0};

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

static sentient_v8::Config cfg_green = make_cfg("keys_green_key_box", HMAC_GREEN_KEY_BOX);
static sentient_v8::Config cfg_yellow = make_cfg("keys_yellow_key_box", HMAC_YELLOW_KEY_BOX);
static sentient_v8::Config cfg_blue = make_cfg("keys_blue_key_box", HMAC_BLUE_KEY_BOX);
static sentient_v8::Config cfg_red = make_cfg("keys_red_key_box", HMAC_RED_KEY_BOX);

static sentient_v8::Client sne_green(cfg_green);
static sentient_v8::Client sne_yellow(cfg_yellow);
static sentient_v8::Client sne_blue(cfg_blue);
static sentient_v8::Client sne_red(cfg_red);

static sentient_v8::Client *clients[4] = {&sne_green, &sne_yellow, &sne_blue, &sne_red};

static void apply_leds() {
  digitalWrite(power_led_pin, HIGH);
  for (int i = 0; i < 4; i++) {
    leds[i] = box_on[i] ? box_color[i] : CRGB::Black;
  }
  FastLED.show();
}

static const char *box_name(int idx) {
  switch (idx) {
  case 0:
    return "green";
  case 1:
    return "yellow";
  case 2:
    return "blue";
  case 3:
    return "red";
  default:
    return "unknown";
  }
}

static void publish_state_box(int idx) {
  if (idx < 0 || idx > 3) return;

  StaticJsonDocument<384> st;
  st["box"] = box_name(idx);

  JsonObject switches = st.createNestedObject("switches");
  bool a = false, b = false, pair = false;
  switch (idx) {
  case 0:
    switches["bottom"] = s_green_bottom;
    switches["right"] = s_green_right;
    a = s_green_bottom;
    b = s_green_right;
    pair = p_green;
    break;
  case 1:
    switches["right"] = s_yellow_right;
    switches["top"] = s_yellow_top;
    a = s_yellow_right;
    b = s_yellow_top;
    pair = p_yellow;
    break;
  case 2:
    switches["left"] = s_blue_left;
    switches["bottom"] = s_blue_bottom;
    a = s_blue_left;
    b = s_blue_bottom;
    pair = p_blue;
    break;
  case 3:
    switches["left"] = s_red_left;
    switches["bottom"] = s_red_bottom;
    a = s_red_left;
    b = s_red_bottom;
    pair = p_red;
    break;
  }
  st["pair_pressed"] = pair;
  st["switch_a_pressed"] = a;
  st["switch_b_pressed"] = b;

  JsonObject led = st.createNestedObject("led");
  led["on"] = box_on[idx];
  led["r"] = box_color[idx].r;
  led["g"] = box_color[idx].g;
  led["b"] = box_color[idx].b;

  clients[idx]->publishState(st);
  last_state_publish[idx] = millis();
}

static void read_switches() {
  auto read_active_low = [](int pin) -> bool { return digitalRead(pin) == LOW; };

  bool nb = read_active_low(pin_green_bottom);
  bool nr = read_active_low(pin_green_right);
  bool yR = read_active_low(pin_yellow_right);
  bool yT = read_active_low(pin_yellow_top);
  bool bL = read_active_low(pin_blue_left);
  bool bB = read_active_low(pin_blue_bottom);
  bool rL = read_active_low(pin_red_left);
  bool rB = read_active_low(pin_red_bottom);

  bool publish_box[4] = {false, false, false, false};

  if (nb != s_green_bottom) { s_green_bottom = nb; publish_box[0] = true; }
  if (nr != s_green_right) { s_green_right = nr; publish_box[0] = true; }
  if (yR != s_yellow_right) { s_yellow_right = yR; publish_box[1] = true; }
  if (yT != s_yellow_top) { s_yellow_top = yT; publish_box[1] = true; }
  if (bL != s_blue_left) { s_blue_left = bL; publish_box[2] = true; }
  if (bB != s_blue_bottom) { s_blue_bottom = bB; publish_box[2] = true; }
  if (rL != s_red_left) { s_red_left = rL; publish_box[3] = true; }
  if (rB != s_red_bottom) { s_red_bottom = rB; publish_box[3] = true; }

  bool ng = s_green_bottom && s_green_right;
  bool ny = s_yellow_right && s_yellow_top;
  bool nbp = s_blue_left && s_blue_bottom;
  bool nrp = s_red_left && s_red_bottom;
  if (ng != p_green) { p_green = ng; publish_box[0] = true; }
  if (ny != p_yellow) { p_yellow = ny; publish_box[1] = true; }
  if (nbp != p_blue) { p_blue = nbp; publish_box[2] = true; }
  if (nrp != p_red) { p_red = nrp; publish_box[3] = true; }

  for (int i = 0; i < 4; i++) {
    if (publish_box[i]) publish_state_box(i);
  }
}

struct BoxCtx {
  int idx;
};
static BoxCtx box_ctx[4] = {{0}, {1}, {2}, {3}};

static bool parse_color(JsonVariantConst c, CRGB &out) {
  if (!c.is<JsonObjectConst>()) return false;
  int r = c["r"] | -1;
  int g = c["g"] | -1;
  int b = c["b"] | -1;
  if (r < 0 || r > 255) return false;
  if (g < 0 || g > 255) return false;
  if (b < 0 || b > 255) return false;
  out = CRGB(uint8_t(r), uint8_t(g), uint8_t(b));
  return true;
}

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void *ctx) {
  BoxCtx *bctx = (BoxCtx *)ctx;
  int idx = bctx ? bctx->idx : -1;
  if (idx < 0 || idx > 3) {
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

  if (strcmp(op, "set") == 0) {
    bool touched = false;
    if (p.containsKey("on")) {
      box_on[idx] = p["on"] | false;
      touched = true;
    }
    if (p.containsKey("color")) {
      CRGB c;
      if (!parse_color(p["color"], c)) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      box_color[idx] = c;
      touched = true;
    }
    if (!touched) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
  } else if (strcmp(op, "reset") == 0) {
    box_on[idx] = true;
    box_color[idx] = CRGB(136, 99, 8);
  } else {
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  apply_leds();
  publish_state_box(idx);
  return true;
}

static void ensure_ethernet_dhcp() {
#if !defined(ESP32)
  static bool started = false;
  if (started) return;

  byte mac[6];
  teensyMAC(mac);

  // Try DHCP; if it fails, keep running (SentientV8 will keep retrying MQTT).
  Ethernet.begin(mac);
  delay(250);
  started = true;
#endif
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(power_led_pin, OUTPUT);
  digitalWrite(power_led_pin, LOW);

  pinMode(pin_green_bottom, INPUT_PULLUP);
  pinMode(pin_green_right, INPUT_PULLUP);
  pinMode(pin_yellow_right, INPUT_PULLUP);
  pinMode(pin_yellow_top, INPUT_PULLUP);
  pinMode(pin_blue_left, INPUT_PULLUP);
  pinMode(pin_blue_bottom, INPUT_PULLUP);
  pinMode(pin_red_left, INPUT_PULLUP);
  pinMode(pin_red_bottom, INPUT_PULLUP);

  FastLED.addLeds<NEOPIXEL, led_pin>(leds, num_leds);
  apply_leds();

  if (!sne_green.begin()) while (true) delay(1000);
  if (!sne_yellow.begin()) while (true) delay(1000);
  if (!sne_blue.begin()) while (true) delay(1000);
  if (!sne_red.begin()) while (true) delay(1000);

  sne_green.setCommandHandler(handleCommand, &box_ctx[0]);
  sne_yellow.setCommandHandler(handleCommand, &box_ctx[1]);
  sne_blue.setCommandHandler(handleCommand, &box_ctx[2]);
  sne_red.setCommandHandler(handleCommand, &box_ctx[3]);

  publish_state_box(0);
  publish_state_box(1);
  publish_state_box(2);
  publish_state_box(3);
}

void loop() {
  sne_green.loop();
  sne_yellow.loop();
  sne_blue.loop();
  sne_red.loop();

  read_switches();

  const unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    if (now - last_state_publish[i] > 60UL * 1000UL) {
      publish_state_box(i);
    }
  }
}
