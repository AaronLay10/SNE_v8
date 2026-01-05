// Power Control (Lower Right) — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per relay rail (+ controller device).
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - power_control_lower_right_gear_24v
// - power_control_lower_right_gear_12v
// - power_control_lower_right_gear_5v
// - power_control_lower_right_floor_24v
// - power_control_lower_right_floor_12v
// - power_control_lower_right_floor_5v
// - power_control_lower_right_riddle_rpi_5v
// - power_control_lower_right_riddle_rpi_12v
// - power_control_lower_right_riddle_5v
// - power_control_lower_right_boiler_room_subpanel_24v
// - power_control_lower_right_boiler_room_subpanel_12v
// - power_control_lower_right_boiler_room_subpanel_5v
// - power_control_lower_right_lab_room_subpanel_24v
// - power_control_lower_right_lab_room_subpanel_12v
// - power_control_lower_right_lab_room_subpanel_5v
// - power_control_lower_right_study_room_subpanel_24v
// - power_control_lower_right_study_room_subpanel_12v
// - power_control_lower_right_study_room_subpanel_5v
// - power_control_lower_right_gun_drawers_24v
// - power_control_lower_right_gun_drawers_12v
// - power_control_lower_right_gun_drawers_5v
// - power_control_lower_right_keys_5v
// - power_control_lower_right_empty_35
// - power_control_lower_right_empty_34
// - power_control_lower_right_controller

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
static const char *HMAC_GEAR_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GEAR_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GEAR_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FLOOR_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FLOOR_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FLOOR_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RIDDLE_RPI_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RIDDLE_RPI_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RIDDLE_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BOILER_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BOILER_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_BOILER_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LAB_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LAB_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LAB_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_STUDY_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_STUDY_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_STUDY_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GUN_24V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GUN_12V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GUN_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_KEYS_5V = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_EMPTY_35 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_EMPTY_34 = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_CONTROLLER = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (active HIGH relays)
static const int power_led_pin = 13;

static const int gear_24v_pin = 11;
static const int gear_12v_pin = 10;
static const int gear_5v_pin = 9;

static const int floor_24v_pin = 8;
static const int floor_12v_pin = 7;
static const int floor_5v_pin = 6;

static const int riddle_rpi_5v_pin = 5;
static const int riddle_rpi_12v_pin = 4;
static const int riddle_5v_pin = 3;

static const int boiler_room_subpanel_24v_pin = 1;
static const int boiler_room_subpanel_12v_pin = 0;
static const int boiler_room_subpanel_5v_pin = 2;

static const int lab_room_subpanel_24v_pin = 26;
static const int lab_room_subpanel_12v_pin = 25;
static const int lab_room_subpanel_5v_pin = 24;

static const int study_room_subpanel_24v_pin = 29;
static const int study_room_subpanel_12v_pin = 28;
static const int study_room_subpanel_5v_pin = 27;

static const int gun_drawers_24v_pin = 32;
static const int gun_drawers_12v_pin = 31;
static const int gun_drawers_5v_pin = 30;

static const int keys_5v_pin = 33;
static const int empty_35_pin = 35;
static const int empty_34_pin = 34;

// Relay states
static bool s_gear_24v = false, s_gear_12v = false, s_gear_5v = false;
static bool s_floor_24v = false, s_floor_12v = false, s_floor_5v = false;
static bool s_riddle_rpi_5v = false, s_riddle_rpi_12v = false, s_riddle_5v = false;
static bool s_boiler_24v = false, s_boiler_12v = false, s_boiler_5v = false;
static bool s_lab_24v = false, s_lab_12v = false, s_lab_5v = false;
static bool s_study_24v = false, s_study_12v = false, s_study_5v = false;
static bool s_gun_24v = false, s_gun_12v = false, s_gun_5v = false;
static bool s_keys_5v = false;
static bool s_empty_35 = false, s_empty_34 = false;

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

static sentient_v8::Client sne_gear_24v(make_cfg("power_control_lower_right_gear_24v", HMAC_GEAR_24V));
static sentient_v8::Client sne_gear_12v(make_cfg("power_control_lower_right_gear_12v", HMAC_GEAR_12V));
static sentient_v8::Client sne_gear_5v(make_cfg("power_control_lower_right_gear_5v", HMAC_GEAR_5V));
static sentient_v8::Client sne_floor_24v(make_cfg("power_control_lower_right_floor_24v", HMAC_FLOOR_24V));
static sentient_v8::Client sne_floor_12v(make_cfg("power_control_lower_right_floor_12v", HMAC_FLOOR_12V));
static sentient_v8::Client sne_floor_5v(make_cfg("power_control_lower_right_floor_5v", HMAC_FLOOR_5V));
static sentient_v8::Client sne_riddle_rpi_5v(make_cfg("power_control_lower_right_riddle_rpi_5v", HMAC_RIDDLE_RPI_5V));
static sentient_v8::Client sne_riddle_rpi_12v(make_cfg("power_control_lower_right_riddle_rpi_12v", HMAC_RIDDLE_RPI_12V));
static sentient_v8::Client sne_riddle_5v(make_cfg("power_control_lower_right_riddle_5v", HMAC_RIDDLE_5V));
static sentient_v8::Client sne_boiler_24v(make_cfg("power_control_lower_right_boiler_room_subpanel_24v", HMAC_BOILER_24V));
static sentient_v8::Client sne_boiler_12v(make_cfg("power_control_lower_right_boiler_room_subpanel_12v", HMAC_BOILER_12V));
static sentient_v8::Client sne_boiler_5v(make_cfg("power_control_lower_right_boiler_room_subpanel_5v", HMAC_BOILER_5V));
static sentient_v8::Client sne_lab_24v(make_cfg("power_control_lower_right_lab_room_subpanel_24v", HMAC_LAB_24V));
static sentient_v8::Client sne_lab_12v(make_cfg("power_control_lower_right_lab_room_subpanel_12v", HMAC_LAB_12V));
static sentient_v8::Client sne_lab_5v(make_cfg("power_control_lower_right_lab_room_subpanel_5v", HMAC_LAB_5V));
static sentient_v8::Client sne_study_24v(make_cfg("power_control_lower_right_study_room_subpanel_24v", HMAC_STUDY_24V));
static sentient_v8::Client sne_study_12v(make_cfg("power_control_lower_right_study_room_subpanel_12v", HMAC_STUDY_12V));
static sentient_v8::Client sne_study_5v(make_cfg("power_control_lower_right_study_room_subpanel_5v", HMAC_STUDY_5V));
static sentient_v8::Client sne_gun_24v(make_cfg("power_control_lower_right_gun_drawers_24v", HMAC_GUN_24V));
static sentient_v8::Client sne_gun_12v(make_cfg("power_control_lower_right_gun_drawers_12v", HMAC_GUN_12V));
static sentient_v8::Client sne_gun_5v(make_cfg("power_control_lower_right_gun_drawers_5v", HMAC_GUN_5V));
static sentient_v8::Client sne_keys_5v(make_cfg("power_control_lower_right_keys_5v", HMAC_KEYS_5V));
static sentient_v8::Client sne_empty_35(make_cfg("power_control_lower_right_empty_35", HMAC_EMPTY_35));
static sentient_v8::Client sne_empty_34(make_cfg("power_control_lower_right_empty_34", HMAC_EMPTY_34));
static sentient_v8::Client sne_controller(make_cfg("power_control_lower_right_controller", HMAC_CONTROLLER));

enum class DeviceKind : uint8_t {
  Gear24V,
  Gear12V,
  Gear5V,
  Floor24V,
  Floor12V,
  Floor5V,
  RiddleRpi5V,
  RiddleRpi12V,
  Riddle5V,
  Boiler24V,
  Boiler12V,
  Boiler5V,
  Lab24V,
  Lab12V,
  Lab5V,
  Study24V,
  Study12V,
  Study5V,
  Gun24V,
  Gun12V,
  Gun5V,
  Keys5V,
  Empty35,
  Empty34,
  Controller,
};

struct RelayCtx {
  DeviceKind kind;
  int pin;
  bool *state;
  sentient_v8::Client *client;
};

static RelayCtx relays[] = {
    {DeviceKind::Gear24V, gear_24v_pin, &s_gear_24v, &sne_gear_24v},
    {DeviceKind::Gear12V, gear_12v_pin, &s_gear_12v, &sne_gear_12v},
    {DeviceKind::Gear5V, gear_5v_pin, &s_gear_5v, &sne_gear_5v},
    {DeviceKind::Floor24V, floor_24v_pin, &s_floor_24v, &sne_floor_24v},
    {DeviceKind::Floor12V, floor_12v_pin, &s_floor_12v, &sne_floor_12v},
    {DeviceKind::Floor5V, floor_5v_pin, &s_floor_5v, &sne_floor_5v},
    {DeviceKind::RiddleRpi5V, riddle_rpi_5v_pin, &s_riddle_rpi_5v, &sne_riddle_rpi_5v},
    {DeviceKind::RiddleRpi12V, riddle_rpi_12v_pin, &s_riddle_rpi_12v, &sne_riddle_rpi_12v},
    {DeviceKind::Riddle5V, riddle_5v_pin, &s_riddle_5v, &sne_riddle_5v},
    {DeviceKind::Boiler24V, boiler_room_subpanel_24v_pin, &s_boiler_24v, &sne_boiler_24v},
    {DeviceKind::Boiler12V, boiler_room_subpanel_12v_pin, &s_boiler_12v, &sne_boiler_12v},
    {DeviceKind::Boiler5V, boiler_room_subpanel_5v_pin, &s_boiler_5v, &sne_boiler_5v},
    {DeviceKind::Lab24V, lab_room_subpanel_24v_pin, &s_lab_24v, &sne_lab_24v},
    {DeviceKind::Lab12V, lab_room_subpanel_12v_pin, &s_lab_12v, &sne_lab_12v},
    {DeviceKind::Lab5V, lab_room_subpanel_5v_pin, &s_lab_5v, &sne_lab_5v},
    {DeviceKind::Study24V, study_room_subpanel_24v_pin, &s_study_24v, &sne_study_24v},
    {DeviceKind::Study12V, study_room_subpanel_12v_pin, &s_study_12v, &sne_study_12v},
    {DeviceKind::Study5V, study_room_subpanel_5v_pin, &s_study_5v, &sne_study_5v},
    {DeviceKind::Gun24V, gun_drawers_24v_pin, &s_gun_24v, &sne_gun_24v},
    {DeviceKind::Gun12V, gun_drawers_12v_pin, &s_gun_12v, &sne_gun_12v},
    {DeviceKind::Gun5V, gun_drawers_5v_pin, &s_gun_5v, &sne_gun_5v},
    {DeviceKind::Keys5V, keys_5v_pin, &s_keys_5v, &sne_keys_5v},
    {DeviceKind::Empty35, empty_35_pin, &s_empty_35, &sne_empty_35},
    {DeviceKind::Empty34, empty_34_pin, &s_empty_34, &sne_empty_34},
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
  st["gear_24v_on"] = s_gear_24v;
  st["gear_12v_on"] = s_gear_12v;
  st["gear_5v_on"] = s_gear_5v;
  st["floor_24v_on"] = s_floor_24v;
  st["floor_12v_on"] = s_floor_12v;
  st["floor_5v_on"] = s_floor_5v;
  st["riddle_rpi_5v_on"] = s_riddle_rpi_5v;
  st["riddle_rpi_12v_on"] = s_riddle_rpi_12v;
  st["riddle_5v_on"] = s_riddle_5v;
  st["boiler_room_subpanel_24v_on"] = s_boiler_24v;
  st["boiler_room_subpanel_12v_on"] = s_boiler_12v;
  st["boiler_room_subpanel_5v_on"] = s_boiler_5v;
  st["lab_room_subpanel_24v_on"] = s_lab_24v;
  st["lab_room_subpanel_12v_on"] = s_lab_12v;
  st["lab_room_subpanel_5v_on"] = s_lab_5v;
  st["study_room_subpanel_24v_on"] = s_study_24v;
  st["study_room_subpanel_12v_on"] = s_study_12v;
  st["study_room_subpanel_5v_on"] = s_study_5v;
  st["gun_drawers_24v_on"] = s_gun_24v;
  st["gun_drawers_12v_on"] = s_gun_12v;
  st["gun_drawers_5v_on"] = s_gun_5v;
  st["keys_5v_on"] = s_keys_5v;
  st["empty_35_on"] = s_empty_35;
  st["empty_34_on"] = s_empty_34;
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
  if (!sne_gear_24v.begin()) while (true) delay(1000);
  if (!sne_gear_12v.begin()) while (true) delay(1000);
  if (!sne_gear_5v.begin()) while (true) delay(1000);
  if (!sne_floor_24v.begin()) while (true) delay(1000);
  if (!sne_floor_12v.begin()) while (true) delay(1000);
  if (!sne_floor_5v.begin()) while (true) delay(1000);
  if (!sne_riddle_rpi_5v.begin()) while (true) delay(1000);
  if (!sne_riddle_rpi_12v.begin()) while (true) delay(1000);
  if (!sne_riddle_5v.begin()) while (true) delay(1000);
  if (!sne_boiler_24v.begin()) while (true) delay(1000);
  if (!sne_boiler_12v.begin()) while (true) delay(1000);
  if (!sne_boiler_5v.begin()) while (true) delay(1000);
  if (!sne_lab_24v.begin()) while (true) delay(1000);
  if (!sne_lab_12v.begin()) while (true) delay(1000);
  if (!sne_lab_5v.begin()) while (true) delay(1000);
  if (!sne_study_24v.begin()) while (true) delay(1000);
  if (!sne_study_12v.begin()) while (true) delay(1000);
  if (!sne_study_5v.begin()) while (true) delay(1000);
  if (!sne_gun_24v.begin()) while (true) delay(1000);
  if (!sne_gun_12v.begin()) while (true) delay(1000);
  if (!sne_gun_5v.begin()) while (true) delay(1000);
  if (!sne_keys_5v.begin()) while (true) delay(1000);
  if (!sne_empty_35.begin()) while (true) delay(1000);
  if (!sne_empty_34.begin()) while (true) delay(1000);
  if (!sne_controller.begin()) while (true) delay(1000);

  // Bind command handlers
  for (size_t i = 0; i < (sizeof(relays) / sizeof(relays[0])); i++) {
    relays[i].client->setCommandHandler(handleCommand, &relays[i]);
  }
  sne_controller.setCommandHandler(handleCommand, &ctx_controller);

  publish_all_states();
}

void loop() {
  sne_gear_24v.loop();
  sne_gear_12v.loop();
  sne_gear_5v.loop();
  sne_floor_24v.loop();
  sne_floor_12v.loop();
  sne_floor_5v.loop();
  sne_riddle_rpi_5v.loop();
  sne_riddle_rpi_12v.loop();
  sne_riddle_5v.loop();
  sne_boiler_24v.loop();
  sne_boiler_12v.loop();
  sne_boiler_5v.loop();
  sne_lab_24v.loop();
  sne_lab_12v.loop();
  sne_lab_5v.loop();
  sne_study_24v.loop();
  sne_study_12v.loop();
  sne_study_5v.loop();
  sne_gun_24v.loop();
  sne_gun_12v.loop();
  sne_gun_5v.loop();
  sne_keys_5v.loop();
  sne_empty_35.loop();
  sne_empty_34.loop();
  sne_controller.loop();
}
