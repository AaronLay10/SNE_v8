// Picture Frame LEDs Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - picture_frame_leds_tv_vincent
// - picture_frame_leds_tv_edith
// - picture_frame_leds_tv_maks
// - picture_frame_leds_tv_oliver
// - picture_frame_leds_all_tvs

#include <Arduino.h>
#include <ArduinoJson.h>

#include <Adafruit_NeoPixel.h>
#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_TV_VINCENT = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_TV_EDITH = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_TV_MAKS = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_TV_OLIVER = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_ALL_TVS = "0000000000000000000000000000000000000000000000000000000000000000";

// Hardware
static const int PIN_POWER_LED = 13;
static const int NUM_STRIPS = 32;
static const int NUM_LEDS_PER_STRIP = 12;
static const int STRIPS_PER_TV = 8;

static const int TV_PINS[NUM_STRIPS] = {
    0,  1,  2,  3,  4,  5,  6,  7,   // Vincent
    16, 17, 18, 19, 20, 21, 22, 23,  // Edith
    25, 26, 27, 28, 29, 30, 31, 32,  // Maks
    14, 15, 8,  9,  10, 11, 12, 24,  // Oliver
};

struct TVColor {
  uint8_t r, g, b;
};

static const TVColor DEFAULT_COLORS[4] = {
    {218, 190, 0},   // Vincent - mustard yellow
    {70, 0, 150},    // Edith - dark lavender
    {20, 91, 0},     // Maks - forest green
    {10, 10, 100},   // Oliver - greyish blue
};

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

static sentient_v8::Client sne_tv_vincent(make_cfg("picture_frame_leds_tv_vincent", HMAC_TV_VINCENT));
static sentient_v8::Client sne_tv_edith(make_cfg("picture_frame_leds_tv_edith", HMAC_TV_EDITH));
static sentient_v8::Client sne_tv_maks(make_cfg("picture_frame_leds_tv_maks", HMAC_TV_MAKS));
static sentient_v8::Client sne_tv_oliver(make_cfg("picture_frame_leds_tv_oliver", HMAC_TV_OLIVER));
static sentient_v8::Client sne_all_tvs(make_cfg("picture_frame_leds_all_tvs", HMAC_ALL_TVS));

static sentient_v8::Client *clients[] = {&sne_tv_vincent, &sne_tv_edith, &sne_tv_maks, &sne_tv_oliver, &sne_all_tvs};

enum class DeviceKind : uint8_t { Vincent, Edith, Maks, Oliver, All };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_vincent = {DeviceKind::Vincent};
static DeviceCtx ctx_edith = {DeviceKind::Edith};
static DeviceCtx ctx_maks = {DeviceKind::Maks};
static DeviceCtx ctx_oliver = {DeviceKind::Oliver};
static DeviceCtx ctx_all = {DeviceKind::All};

static Adafruit_NeoPixel strips[NUM_STRIPS];
static bool tv_power[4] = {true, true, true, true};
static uint8_t tv_brightness[4] = {10, 10, 10, 10};
static TVColor tv_color[4];

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

static void apply_tv_color(int tv_index) {
  const int strip_start = tv_index * STRIPS_PER_TV;
  const int strip_end = strip_start + STRIPS_PER_TV;

  for (int strip = strip_start; strip < strip_end; strip++) {
    if (tv_power[tv_index]) {
      uint32_t color = strips[strip].Color(tv_color[tv_index].r, tv_color[tv_index].g, tv_color[tv_index].b);
      for (int led = 0; led < NUM_LEDS_PER_STRIP; led++) strips[strip].setPixelColor(led, color);
    } else {
      strips[strip].clear();
    }
    strips[strip].show();
  }
}

static void set_tv_power(int tv_index, bool on) {
  tv_power[tv_index] = on;
  apply_tv_color(tv_index);
}

static void set_tv_color(int tv_index, uint8_t r, uint8_t g, uint8_t b) {
  tv_color[tv_index] = {r, g, b};
  apply_tv_color(tv_index);
}

static void set_tv_brightness(int tv_index, uint8_t brightness) {
  tv_brightness[tv_index] = brightness;
  const int strip_start = tv_index * STRIPS_PER_TV;
  const int strip_end = strip_start + STRIPS_PER_TV;
  for (int strip = strip_start; strip < strip_end; strip++) {
    strips[strip].setBrightness(brightness);
    strips[strip].show();
  }
}

static void publish_tv_state(int tv_index, const char *reason) {
  sentient_v8::Client *c = nullptr;
  if (tv_index == 0) c = &sne_tv_vincent;
  if (tv_index == 1) c = &sne_tv_edith;
  if (tv_index == 2) c = &sne_tv_maks;
  if (tv_index == 3) c = &sne_tv_oliver;
  if (!c) return;

  StaticJsonDocument<256> st;
  st["power"] = tv_power[tv_index];
  st["brightness"] = tv_brightness[tv_index];
  JsonObject col = st["color"].to<JsonObject>();
  col["r"] = tv_color[tv_index].r;
  col["g"] = tv_color[tv_index].g;
  col["b"] = tv_color[tv_index].b;
  st["reason"] = reason ? reason : "";
  c->publishState(st);
}

static void publish_all_state(const char *reason) {
  StaticJsonDocument<512> st;
  for (int i = 0; i < 4; i++) {
    JsonObject t = st["tvs"][i].to<JsonObject>();
    t["power"] = tv_power[i];
    t["brightness"] = tv_brightness[i];
    JsonObject col = t["color"].to<JsonObject>();
    col["r"] = tv_color[i].r;
    col["g"] = tv_color[i].g;
    col["b"] = tv_color[i].b;
  }
  st["reason"] = reason ? reason : "";
  sne_all_tvs.publishState(st);
}

static void publish_all(const char *reason) {
  for (int i = 0; i < 4; i++) publish_tv_state(i, reason);
  publish_all_state(reason);
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
    publish_all("request_status");
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

  int tv_start = -1;
  int tv_end = -1;
  switch (ctx->kind) {
    case DeviceKind::Vincent: tv_start = tv_end = 0; break;
    case DeviceKind::Edith: tv_start = tv_end = 1; break;
    case DeviceKind::Maks: tv_start = tv_end = 2; break;
    case DeviceKind::Oliver: tv_start = tv_end = 3; break;
    case DeviceKind::All: tv_start = 0; tv_end = 3; break;
  }

  if (strcmp(op, "power_on") == 0) {
    for (int tv = tv_start; tv <= tv_end; tv++) set_tv_power(tv, true);
    publish_all("power_on");
    return true;
  }
  if (strcmp(op, "power_off") == 0) {
    for (int tv = tv_start; tv <= tv_end; tv++) set_tv_power(tv, false);
    publish_all("power_off");
    return true;
  }
  if (strcmp(op, "set_color") == 0) {
    if (!p.containsKey("r") || !p.containsKey("g") || !p.containsKey("b")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    uint8_t r = (uint8_t)(p["r"] | 0);
    uint8_t g = (uint8_t)(p["g"] | 0);
    uint8_t b = (uint8_t)(p["b"] | 0);
    for (int tv = tv_start; tv <= tv_end; tv++) set_tv_color(tv, r, g, b);
    publish_all("set_color");
    return true;
  }
  if (strcmp(op, "set_brightness") == 0) {
    if (!p.containsKey("brightness")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    uint8_t brightness = (uint8_t)(p["brightness"] | 0);
    for (int tv = tv_start; tv <= tv_end; tv++) set_tv_brightness(tv, brightness);
    publish_all("set_brightness");
    return true;
  }
  if (strcmp(op, "flicker") == 0) {
    int cycles = p["cycles"] | 5;
    int delay_ms = p["delay_ms"] | 100;
    if (cycles < 1) cycles = 1;
    if (cycles > 20) cycles = 20;
    if (delay_ms < 10) delay_ms = 10;
    if (delay_ms > 1000) delay_ms = 1000;

    for (int tv = tv_start; tv <= tv_end; tv++) {
      for (int i = 0; i < cycles; i++) {
        set_tv_power(tv, false);
        delay(delay_ms);
        set_tv_power(tv, true);
        delay(delay_ms);
      }
    }
    publish_all("flicker");
    return true;
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

  for (int i = 0; i < NUM_STRIPS; i++) {
    strips[i] = Adafruit_NeoPixel(NUM_LEDS_PER_STRIP, TV_PINS[i], NEO_GRB + NEO_KHZ800);
    strips[i].begin();
    strips[i].setBrightness(10);
    strips[i].clear();
    strips[i].show();
  }

  for (int i = 0; i < 4; i++) {
    tv_color[i] = DEFAULT_COLORS[i];
    tv_power[i] = true;
    tv_brightness[i] = 10;
    set_tv_brightness(i, tv_brightness[i]);
    set_tv_color(i, tv_color[i].r, tv_color[i].g, tv_color[i].b);
  }

  if (!sne_tv_vincent.begin()) while (true) delay(1000);
  if (!sne_tv_edith.begin()) while (true) delay(1000);
  if (!sne_tv_maks.begin()) while (true) delay(1000);
  if (!sne_tv_oliver.begin()) while (true) delay(1000);
  if (!sne_all_tvs.begin()) while (true) delay(1000);

  sne_tv_vincent.setCommandHandler(handleCommand, &ctx_vincent);
  sne_tv_edith.setCommandHandler(handleCommand, &ctx_edith);
  sne_tv_maks.setCommandHandler(handleCommand, &ctx_maks);
  sne_tv_oliver.setCommandHandler(handleCommand, &ctx_oliver);
  sne_all_tvs.setCommandHandler(handleCommand, &ctx_all);

  publish_all("boot");
}

void loop() {
  static unsigned long last_publish = 0;
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  const unsigned long now = millis();
  if (now - last_publish > 60UL * 1000UL) {
    publish_all("periodic");
    last_publish = now;
  }
}
