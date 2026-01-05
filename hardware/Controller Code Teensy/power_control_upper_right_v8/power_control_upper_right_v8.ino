// Power Control (Upper Right) — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per relay rail (+ controller device).
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - power_control_upper_right_main_lighting_24v
// - power_control_upper_right_main_lighting_12v
// - power_control_upper_right_main_lighting_5v
// - power_control_upper_right_gauges_12v_a
// - power_control_upper_right_gauges_12v_b
// - power_control_upper_right_gauges_5v
// - power_control_upper_right_lever_boiler_5v
// - power_control_upper_right_lever_boiler_12v
// - power_control_upper_right_pilot_light_5v
// - power_control_upper_right_kraken_controls_5v
// - power_control_upper_right_fuse_12v
// - power_control_upper_right_fuse_5v
// - power_control_upper_right_syringe_24v
// - power_control_upper_right_syringe_12v
// - power_control_upper_right_syringe_5v
// - power_control_upper_right_chemical_24v
// - power_control_upper_right_chemical_12v
// - power_control_upper_right_chemical_5v
// - power_control_upper_right_crawl_space_blacklight
// - power_control_upper_right_floor_audio_amp
// - power_control_upper_right_kraken_radar_amp
// - power_control_upper_right_vault_24v
// - power_control_upper_right_vault_12v
// - power_control_upper_right_vault_5v
// - power_control_upper_right_controller

#include <Arduino.h>
#include <ArduinoJson.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_MAIN_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MAIN_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MAIN_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GAUGES_12V_A = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GAUGES_12V_B = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GAUGES_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_BOILER_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_BOILER_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_PILOT_LIGHT_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_KRAKEN_CONTROLS_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FUSE_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FUSE_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_SYRINGE_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_SYRINGE_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_SYRINGE_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CHEMICAL_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CHEMICAL_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CHEMICAL_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CRAWL_SPACE_BLACKLIGHT = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FLOOR_AUDIO_AMP = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_KRAKEN_RADAR_AMP = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_VAULT_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_VAULT_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_VAULT_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CONTROLLER = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (active HIGH relays)
static const int power_led_pin = 13;

static const int main_lighting_24v_pin = 9;
static const int main_lighting_12v_pin = 10;
static const int main_lighting_5v_pin = 11;

static const int gauges_12v_a_pin = 3;
static const int gauges_12v_b_pin = 4;
static const int gauges_5v_pin = 5;

static const int lever_boiler_5v_pin = 6;
static const int lever_boiler_12v_pin = 7;

static const int pilot_light_5v_pin = 8;
static const int kraken_controls_5v_pin = 0;

static const int fuse_12v_pin = 1;
static const int fuse_5v_pin = 2;

static const int syringe_24v_pin = 28;
static const int syringe_12v_pin = 27;
static const int syringe_5v_pin = 26;

static const int chemical_24v_pin = 25;
static const int chemical_12v_pin = 24;
static const int chemical_5v_pin = 12;

static const int crawl_space_blacklight_pin = 31;
static const int floor_audio_amp_pin = 30;
static const int kraken_radar_amp_pin = 29;

static const int vault_24v_pin = 33;
static const int vault_12v_pin = 34;
static const int vault_5v_pin = 32;

// Relay states
static bool s_main_24v = false, s_main_12v = false, s_main_5v = false;
static bool s_gauges_12v_a = false, s_gauges_12v_b = false, s_gauges_5v = false;
static bool s_lever_boiler_5v = false, s_lever_boiler_12v = false;
static bool s_pilot_light_5v = false;
static bool s_kraken_controls_5v = false;
static bool s_fuse_12v = false, s_fuse_5v = false;
static bool s_syringe_24v = false, s_syringe_12v = false, s_syringe_5v = false;
static bool s_chemical_24v = false, s_chemical_12v = false, s_chemical_5v = false;
static bool s_crawl_space_blacklight = false, s_floor_audio_amp = false, s_kraken_radar_amp = false;
static bool s_vault_24v = false, s_vault_12v = false, s_vault_5v = false;

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

static sentient_v8::Client sne_main_24v(make_cfg("power_control_upper_right_main_lighting_24v", HMAC_MAIN_24V));
static sentient_v8::Client sne_main_12v(make_cfg("power_control_upper_right_main_lighting_12v", HMAC_MAIN_12V));
static sentient_v8::Client sne_main_5v(make_cfg("power_control_upper_right_main_lighting_5v", HMAC_MAIN_5V));
static sentient_v8::Client sne_gauges_12v_a(make_cfg("power_control_upper_right_gauges_12v_a", HMAC_GAUGES_12V_A));
static sentient_v8::Client sne_gauges_12v_b(make_cfg("power_control_upper_right_gauges_12v_b", HMAC_GAUGES_12V_B));
static sentient_v8::Client sne_gauges_5v(make_cfg("power_control_upper_right_gauges_5v", HMAC_GAUGES_5V));
static sentient_v8::Client sne_lever_boiler_5v(make_cfg("power_control_upper_right_lever_boiler_5v", HMAC_LEVER_BOILER_5V));
static sentient_v8::Client sne_lever_boiler_12v(make_cfg("power_control_upper_right_lever_boiler_12v", HMAC_LEVER_BOILER_12V));
static sentient_v8::Client sne_pilot_light_5v(make_cfg("power_control_upper_right_pilot_light_5v", HMAC_PILOT_LIGHT_5V));
static sentient_v8::Client sne_kraken_controls_5v(make_cfg("power_control_upper_right_kraken_controls_5v", HMAC_KRAKEN_CONTROLS_5V));
static sentient_v8::Client sne_fuse_12v(make_cfg("power_control_upper_right_fuse_12v", HMAC_FUSE_12V));
static sentient_v8::Client sne_fuse_5v(make_cfg("power_control_upper_right_fuse_5v", HMAC_FUSE_5V));
static sentient_v8::Client sne_syringe_24v(make_cfg("power_control_upper_right_syringe_24v", HMAC_SYRINGE_24V));
static sentient_v8::Client sne_syringe_12v(make_cfg("power_control_upper_right_syringe_12v", HMAC_SYRINGE_12V));
static sentient_v8::Client sne_syringe_5v(make_cfg("power_control_upper_right_syringe_5v", HMAC_SYRINGE_5V));
static sentient_v8::Client sne_chemical_24v(make_cfg("power_control_upper_right_chemical_24v", HMAC_CHEMICAL_24V));
static sentient_v8::Client sne_chemical_12v(make_cfg("power_control_upper_right_chemical_12v", HMAC_CHEMICAL_12V));
static sentient_v8::Client sne_chemical_5v(make_cfg("power_control_upper_right_chemical_5v", HMAC_CHEMICAL_5V));
static sentient_v8::Client sne_crawl_space_blacklight(make_cfg("power_control_upper_right_crawl_space_blacklight", HMAC_CRAWL_SPACE_BLACKLIGHT));
static sentient_v8::Client sne_floor_audio_amp(make_cfg("power_control_upper_right_floor_audio_amp", HMAC_FLOOR_AUDIO_AMP));
static sentient_v8::Client sne_kraken_radar_amp(make_cfg("power_control_upper_right_kraken_radar_amp", HMAC_KRAKEN_RADAR_AMP));
static sentient_v8::Client sne_vault_24v(make_cfg("power_control_upper_right_vault_24v", HMAC_VAULT_24V));
static sentient_v8::Client sne_vault_12v(make_cfg("power_control_upper_right_vault_12v", HMAC_VAULT_12V));
static sentient_v8::Client sne_vault_5v(make_cfg("power_control_upper_right_vault_5v", HMAC_VAULT_5V));
static sentient_v8::Client sne_controller(make_cfg("power_control_upper_right_controller", HMAC_CONTROLLER));

enum class DeviceKind : uint8_t {
  Main24V,
  Main12V,
  Main5V,
  Gauges12VA,
  Gauges12VB,
  Gauges5V,
  LeverBoiler5V,
  LeverBoiler12V,
  PilotLight5V,
  KrakenControls5V,
  Fuse12V,
  Fuse5V,
  Syringe24V,
  Syringe12V,
  Syringe5V,
  Chemical24V,
  Chemical12V,
  Chemical5V,
  CrawlSpaceBlacklight,
  FloorAudioAmp,
  KrakenRadarAmp,
  Vault24V,
  Vault12V,
  Vault5V,
  Controller,
};

struct RelayCtx {
  DeviceKind kind;
  int pin;
  bool *state;
  sentient_v8::Client *client;
};

static RelayCtx relays[] = {
    {DeviceKind::Main24V, main_lighting_24v_pin, &s_main_24v, &sne_main_24v},
    {DeviceKind::Main12V, main_lighting_12v_pin, &s_main_12v, &sne_main_12v},
    {DeviceKind::Main5V, main_lighting_5v_pin, &s_main_5v, &sne_main_5v},
    {DeviceKind::Gauges12VA, gauges_12v_a_pin, &s_gauges_12v_a, &sne_gauges_12v_a},
    {DeviceKind::Gauges12VB, gauges_12v_b_pin, &s_gauges_12v_b, &sne_gauges_12v_b},
    {DeviceKind::Gauges5V, gauges_5v_pin, &s_gauges_5v, &sne_gauges_5v},
    {DeviceKind::LeverBoiler5V, lever_boiler_5v_pin, &s_lever_boiler_5v, &sne_lever_boiler_5v},
    {DeviceKind::LeverBoiler12V, lever_boiler_12v_pin, &s_lever_boiler_12v, &sne_lever_boiler_12v},
    {DeviceKind::PilotLight5V, pilot_light_5v_pin, &s_pilot_light_5v, &sne_pilot_light_5v},
    {DeviceKind::KrakenControls5V, kraken_controls_5v_pin, &s_kraken_controls_5v, &sne_kraken_controls_5v},
    {DeviceKind::Fuse12V, fuse_12v_pin, &s_fuse_12v, &sne_fuse_12v},
    {DeviceKind::Fuse5V, fuse_5v_pin, &s_fuse_5v, &sne_fuse_5v},
    {DeviceKind::Syringe24V, syringe_24v_pin, &s_syringe_24v, &sne_syringe_24v},
    {DeviceKind::Syringe12V, syringe_12v_pin, &s_syringe_12v, &sne_syringe_12v},
    {DeviceKind::Syringe5V, syringe_5v_pin, &s_syringe_5v, &sne_syringe_5v},
    {DeviceKind::Chemical24V, chemical_24v_pin, &s_chemical_24v, &sne_chemical_24v},
    {DeviceKind::Chemical12V, chemical_12v_pin, &s_chemical_12v, &sne_chemical_12v},
    {DeviceKind::Chemical5V, chemical_5v_pin, &s_chemical_5v, &sne_chemical_5v},
    {DeviceKind::CrawlSpaceBlacklight, crawl_space_blacklight_pin, &s_crawl_space_blacklight, &sne_crawl_space_blacklight},
    {DeviceKind::FloorAudioAmp, floor_audio_amp_pin, &s_floor_audio_amp, &sne_floor_audio_amp},
    {DeviceKind::KrakenRadarAmp, kraken_radar_amp_pin, &s_kraken_radar_amp, &sne_kraken_radar_amp},
    {DeviceKind::Vault24V, vault_24v_pin, &s_vault_24v, &sne_vault_24v},
    {DeviceKind::Vault12V, vault_12v_pin, &s_vault_12v, &sne_vault_12v},
    {DeviceKind::Vault5V, vault_5v_pin, &s_vault_5v, &sne_vault_5v},
};

struct ControllerCtx {
  DeviceKind kind;
};
static ControllerCtx ctx_controller = {DeviceKind::Controller};

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

static void publish_state_relay(sentient_v8::Client &c, bool on) {
  StaticJsonDocument<128> st;
  st["on"] = on;
  c.publishState(st);
}

static void publish_state_controller() {
  StaticJsonDocument<512> st;
  st["main_lighting_24v_on"] = s_main_24v;
  st["main_lighting_12v_on"] = s_main_12v;
  st["main_lighting_5v_on"] = s_main_5v;
  st["gauges_12v_a_on"] = s_gauges_12v_a;
  st["gauges_12v_b_on"] = s_gauges_12v_b;
  st["gauges_5v_on"] = s_gauges_5v;
  st["lever_boiler_5v_on"] = s_lever_boiler_5v;
  st["lever_boiler_12v_on"] = s_lever_boiler_12v;
  st["pilot_light_5v_on"] = s_pilot_light_5v;
  st["kraken_controls_5v_on"] = s_kraken_controls_5v;
  st["fuse_12v_on"] = s_fuse_12v;
  st["fuse_5v_on"] = s_fuse_5v;
  st["syringe_24v_on"] = s_syringe_24v;
  st["syringe_12v_on"] = s_syringe_12v;
  st["syringe_5v_on"] = s_syringe_5v;
  st["chemical_24v_on"] = s_chemical_24v;
  st["chemical_12v_on"] = s_chemical_12v;
  st["chemical_5v_on"] = s_chemical_5v;
  st["crawl_space_blacklight_on"] = s_crawl_space_blacklight;
  st["floor_audio_amp_on"] = s_floor_audio_amp;
  st["kraken_radar_amp_on"] = s_kraken_radar_amp;
  st["vault_24v_on"] = s_vault_24v;
  st["vault_12v_on"] = s_vault_12v;
  st["vault_5v_on"] = s_vault_5v;
  sne_controller.publishState(st);
}

static void publish_all_states() {
  for (size_t i = 0; i < (sizeof(relays) / sizeof(relays[0])); i++) {
    publish_state_relay(*relays[i].client, *relays[i].state);
  }
  publish_state_controller();
}

static void set_relay(RelayCtx &r, bool on) {
  *r.state = on;
  digitalWrite(r.pin, on ? HIGH : LOW);
  publish_state_relay(*r.client, on);
}

static void set_all(bool on) {
  for (size_t i = 0; i < (sizeof(relays) / sizeof(relays[0])); i++) {
    *relays[i].state = on;
    digitalWrite(relays[i].pin, on ? HIGH : LOW);
  }
  publish_all_states();
}

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void *vctx) {
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

  // Relay devices
  RelayCtx *r = (RelayCtx *)vctx;
  if (r && r->kind != DeviceKind::Controller) {
    if (strcmp(op, "set") != 0 || !p.containsKey("on")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    bool on = p["on"] | false;
    set_relay(*r, on);
    publish_state_controller();
    return true;
  }

  // Controller device
  if (strcmp(op, "all_on") == 0) {
    set_all(true);
    return true;
  }
  if (strcmp(op, "all_off") == 0) {
    set_all(false);
    return true;
  }
  if (strcmp(op, "emergency_off") == 0) {
    set_all(false);
    return true;
  }
  if (strcmp(op, "reset") == 0) {
    set_all(false);
    return true;
  }
  if (strcmp(op, "request_status") == 0) {
    publish_all_states();
    return true;
  }
  if (strcmp(op, "noop") == 0) {
    return true;
  }

  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(power_led_pin, OUTPUT);
  digitalWrite(power_led_pin, HIGH);

  // Initialize all relay pins to OFF (safe default)
  for (size_t i = 0; i < (sizeof(relays) / sizeof(relays[0])); i++) {
    pinMode(relays[i].pin, OUTPUT);
    *relays[i].state = false;
    digitalWrite(relays[i].pin, LOW);
  }

  // Start MQTT clients (one per device_id)
  if (!sne_main_24v.begin()) while (true) delay(1000);
  if (!sne_main_12v.begin()) while (true) delay(1000);
  if (!sne_main_5v.begin()) while (true) delay(1000);
  if (!sne_gauges_12v_a.begin()) while (true) delay(1000);
  if (!sne_gauges_12v_b.begin()) while (true) delay(1000);
  if (!sne_gauges_5v.begin()) while (true) delay(1000);
  if (!sne_lever_boiler_5v.begin()) while (true) delay(1000);
  if (!sne_lever_boiler_12v.begin()) while (true) delay(1000);
  if (!sne_pilot_light_5v.begin()) while (true) delay(1000);
  if (!sne_kraken_controls_5v.begin()) while (true) delay(1000);
  if (!sne_fuse_12v.begin()) while (true) delay(1000);
  if (!sne_fuse_5v.begin()) while (true) delay(1000);
  if (!sne_syringe_24v.begin()) while (true) delay(1000);
  if (!sne_syringe_12v.begin()) while (true) delay(1000);
  if (!sne_syringe_5v.begin()) while (true) delay(1000);
  if (!sne_chemical_24v.begin()) while (true) delay(1000);
  if (!sne_chemical_12v.begin()) while (true) delay(1000);
  if (!sne_chemical_5v.begin()) while (true) delay(1000);
  if (!sne_crawl_space_blacklight.begin()) while (true) delay(1000);
  if (!sne_floor_audio_amp.begin()) while (true) delay(1000);
  if (!sne_kraken_radar_amp.begin()) while (true) delay(1000);
  if (!sne_vault_24v.begin()) while (true) delay(1000);
  if (!sne_vault_12v.begin()) while (true) delay(1000);
  if (!sne_vault_5v.begin()) while (true) delay(1000);
  if (!sne_controller.begin()) while (true) delay(1000);

  // Bind command handlers
  for (size_t i = 0; i < (sizeof(relays) / sizeof(relays[0])); i++) {
    relays[i].client->setCommandHandler(handleCommand, &relays[i]);
  }
  sne_controller.setCommandHandler(handleCommand, &ctx_controller);

  publish_all_states();
}

void loop() {
  sne_main_24v.loop();
  sne_main_12v.loop();
  sne_main_5v.loop();
  sne_gauges_12v_a.loop();
  sne_gauges_12v_b.loop();
  sne_gauges_5v.loop();
  sne_lever_boiler_5v.loop();
  sne_lever_boiler_12v.loop();
  sne_pilot_light_5v.loop();
  sne_kraken_controls_5v.loop();
  sne_fuse_12v.loop();
  sne_fuse_5v.loop();
  sne_syringe_24v.loop();
  sne_syringe_12v.loop();
  sne_syringe_5v.loop();
  sne_chemical_24v.loop();
  sne_chemical_12v.loop();
  sne_chemical_5v.loop();
  sne_crawl_space_blacklight.loop();
  sne_floor_audio_amp.loop();
  sne_kraken_radar_amp.loop();
  sne_vault_24v.loop();
  sne_vault_12v.loop();
  sne_vault_5v.loop();
  sne_controller.loop();
}
