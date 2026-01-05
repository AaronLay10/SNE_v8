// Fuse Box Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - fuse_rfid_a, fuse_rfid_b, fuse_rfid_c, fuse_rfid_d, fuse_rfid_e
// - fuse_fuse_a, fuse_fuse_b, fuse_fuse_c
// - fuse_knife_switch
// - fuse_actuator
// - fuse_maglock_b, fuse_maglock_c, fuse_maglock_d
// - fuse_metal_gate

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
static const char *HMAC_RFID_A = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RFID_B = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RFID_C = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RFID_D = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RFID_E = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FUSE_A = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FUSE_B = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FUSE_C = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_KNIFE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_ACTUATOR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MAGLOCK_B = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MAGLOCK_C = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MAGLOCK_D = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_GATE = "0000000000000000000000000000000000000000000000000000000000000000";

// Pin Definitions
static const int PIN_POWER_LED = 13;

// RFID Readers
#define RFID_A Serial1 // Pins 0,1
#define RFID_B Serial2 // Pins 7,8
#define RFID_C Serial3 // Pins 14,15
#define RFID_D Serial4 // Pins 16,17
#define RFID_E Serial5 // Pins 20,21

// TIR (Tag In Range) Sensors
static const int PIN_TIR_A = 4;
static const int PIN_TIR_B = 5;
static const int PIN_TIR_C = 2;
static const int PIN_TIR_D = 6;
static const int PIN_TIR_E = 3;

// Actuator
static const int PIN_ACTUATOR_FWD = 33;
static const int PIN_ACTUATOR_RWD = 34;

// Maglocks (HIGH = locked)
static const int PIN_MAGLOCK_B = 10;
static const int PIN_MAGLOCK_C = 11;
static const int PIN_MAGLOCK_D = 12;

// Metal Gate (HIGH = locked)
static const int PIN_METAL_GATE = 31;

// Fuse Resistor Sensors
static const int PIN_FUSE_A = A5;
static const int PIN_FUSE_B = A8;
static const int PIN_FUSE_C = A9;

// Knife Switch
static const int PIN_KNIFE_SWITCH = 18;

// State
static bool knife_switch_on = false;
static bool maglock_b_locked = true;
static bool maglock_c_locked = true;
static bool maglock_d_locked = true;
static bool metal_gate_locked = true;

static int resistor_a = 0;
static int resistor_b = 0;
static int resistor_c = 0;

static int last_resistor_a = -1;
static int last_resistor_b = -1;
static int last_resistor_c = -1;

static bool last_knife_switch_on = false;

struct RfidReader {
  HardwareSerial *serial;
  int tir_pin;
  char tag[21];
  char last_tag[21];
  char buf[16];
  uint8_t count;
  bool packet_started;
};

static RfidReader rfid_readers[] = {
    {&RFID_A, PIN_TIR_A, "", "", {0}, 0, false},
    {&RFID_B, PIN_TIR_B, "", "", {0}, 0, false},
    {&RFID_C, PIN_TIR_C, "", "", {0}, 0, false},
    {&RFID_D, PIN_TIR_D, "", "", {0}, 0, false},
    {&RFID_E, PIN_TIR_E, "", "", {0}, 0, false},
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
  c.heartbeatIntervalMs = 1000;
  c.rxJsonCapacity = 4096;
  c.txJsonCapacity = 4096;
  return c;
}

static sentient_v8::Client sne_rfid_a(make_cfg("fuse_rfid_a", HMAC_RFID_A));
static sentient_v8::Client sne_rfid_b(make_cfg("fuse_rfid_b", HMAC_RFID_B));
static sentient_v8::Client sne_rfid_c(make_cfg("fuse_rfid_c", HMAC_RFID_C));
static sentient_v8::Client sne_rfid_d(make_cfg("fuse_rfid_d", HMAC_RFID_D));
static sentient_v8::Client sne_rfid_e(make_cfg("fuse_rfid_e", HMAC_RFID_E));
static sentient_v8::Client sne_fuse_a(make_cfg("fuse_fuse_a", HMAC_FUSE_A));
static sentient_v8::Client sne_fuse_b(make_cfg("fuse_fuse_b", HMAC_FUSE_B));
static sentient_v8::Client sne_fuse_c(make_cfg("fuse_fuse_c", HMAC_FUSE_C));
static sentient_v8::Client sne_knife(make_cfg("fuse_knife_switch", HMAC_KNIFE));
static sentient_v8::Client sne_actuator(make_cfg("fuse_actuator", HMAC_ACTUATOR));
static sentient_v8::Client sne_maglock_b(make_cfg("fuse_maglock_b", HMAC_MAGLOCK_B));
static sentient_v8::Client sne_maglock_c(make_cfg("fuse_maglock_c", HMAC_MAGLOCK_C));
static sentient_v8::Client sne_maglock_d(make_cfg("fuse_maglock_d", HMAC_MAGLOCK_D));
static sentient_v8::Client sne_gate(make_cfg("fuse_metal_gate", HMAC_GATE));

enum class DeviceKind : uint8_t {
  RfidA,
  RfidB,
  RfidC,
  RfidD,
  RfidE,
  FuseA,
  FuseB,
  FuseC,
  Knife,
  Actuator,
  MaglockB,
  MaglockC,
  MaglockD,
  Gate,
};

struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_rfid_a = {DeviceKind::RfidA};
static DeviceCtx ctx_rfid_b = {DeviceKind::RfidB};
static DeviceCtx ctx_rfid_c = {DeviceKind::RfidC};
static DeviceCtx ctx_rfid_d = {DeviceKind::RfidD};
static DeviceCtx ctx_rfid_e = {DeviceKind::RfidE};
static DeviceCtx ctx_fuse_a = {DeviceKind::FuseA};
static DeviceCtx ctx_fuse_b = {DeviceKind::FuseB};
static DeviceCtx ctx_fuse_c = {DeviceKind::FuseC};
static DeviceCtx ctx_knife = {DeviceKind::Knife};
static DeviceCtx ctx_actuator = {DeviceKind::Actuator};
static DeviceCtx ctx_maglock_b = {DeviceKind::MaglockB};
static DeviceCtx ctx_maglock_c = {DeviceKind::MaglockC};
static DeviceCtx ctx_maglock_d = {DeviceKind::MaglockD};
static DeviceCtx ctx_gate = {DeviceKind::Gate};

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
  digitalWrite(PIN_POWER_LED, HIGH);
  digitalWrite(PIN_MAGLOCK_B, maglock_b_locked ? HIGH : LOW);
  digitalWrite(PIN_MAGLOCK_C, maglock_c_locked ? HIGH : LOW);
  digitalWrite(PIN_MAGLOCK_D, maglock_d_locked ? HIGH : LOW);
  digitalWrite(PIN_METAL_GATE, metal_gate_locked ? HIGH : LOW);
}

static void publish_state_rfid(sentient_v8::Client &c, RfidReader &r) {
  StaticJsonDocument<256> st;
  st["tir_in_range"] = (digitalRead(r.tir_pin) == HIGH);
  st["tag"] = (r.tag[0] ? r.tag : "");
  c.publishState(st);
}

static void publish_state_fuse(sentient_v8::Client &c, int value) {
  StaticJsonDocument<128> st;
  st["resistor_value"] = value;
  c.publishState(st);
}

static void publish_state_knife() {
  StaticJsonDocument<128> st;
  st["on"] = knife_switch_on;
  sne_knife.publishState(st);
}

static void publish_state_actuator(const char *mode) {
  StaticJsonDocument<192> st;
  st["mode"] = mode ? mode : "STOP";
  sne_actuator.publishState(st);
}

static void publish_state_maglock(sentient_v8::Client &c, bool locked) {
  StaticJsonDocument<128> st;
  st["locked"] = locked;
  c.publishState(st);
}

static void publish_state_gate() {
  StaticJsonDocument<128> st;
  st["locked"] = metal_gate_locked;
  sne_gate.publishState(st);
}

static void clean_tag(char *tag) {
  int j = 0;
  for (int i = 0; tag[i] != '\0'; i++) {
    if (tag[i] != '\n' && tag[i] != '\r') {
      tag[j++] = tag[i];
    }
  }
  tag[j] = '\0';
}

static int round_resistor_value(int raw_value) {
  if (raw_value < 50) return 0;
  int rounded = ((raw_value + 50) / 100) * 100;
  return (rounded > 300) ? 300 : rounded;
}

static int calculate_resistor_value(int pin) {
  int raw_value = analogRead(pin);

  const int Vin_scaled = 3300;
  const int cont_resistor = 1000;
  const int threshold = 10;
  const int max_resistor = 1000;

  int Vout_scaled = (raw_value * Vin_scaled) / 1024;
  int resistor = (Vout_scaled >= threshold) ? (cont_resistor * (Vin_scaled - Vout_scaled)) / Vout_scaled : 0;
  return round_resistor_value((resistor > max_resistor) ? 0 : resistor);
}

static void monitor_rfid() {
  for (size_t i = 0; i < (sizeof(rfid_readers) / sizeof(rfid_readers[0])); i++) {
    RfidReader &r = rfid_readers[i];

    while (r.serial->available()) {
      char incoming_char = (char)r.serial->read();
      if ((uint8_t)incoming_char == 0x02) {
        r.count = 0;
        r.packet_started = true;
      } else if ((uint8_t)incoming_char == 0x03 && r.packet_started) {
        r.buf[r.count] = '\0';
        strncpy(r.tag, r.buf, sizeof(r.tag) - 1);
        r.tag[sizeof(r.tag) - 1] = '\0';
        clean_tag(r.tag);
        r.packet_started = false;
      } else if (r.packet_started && r.count < sizeof(r.buf) - 1) {
        r.buf[r.count++] = incoming_char;
      }
    }

    // Clear tag when TIR indicates removed.
    if (digitalRead(r.tir_pin) == LOW && r.tag[0]) {
      r.tag[0] = '\0';
    }

    if (strcmp(r.tag, r.last_tag) != 0) {
      strncpy(r.last_tag, r.tag, sizeof(r.last_tag));
      switch (i) {
      case 0:
        publish_state_rfid(sne_rfid_a, r);
        break;
      case 1:
        publish_state_rfid(sne_rfid_b, r);
        break;
      case 2:
        publish_state_rfid(sne_rfid_c, r);
        break;
      case 3:
        publish_state_rfid(sne_rfid_d, r);
        break;
      case 4:
        publish_state_rfid(sne_rfid_e, r);
        break;
      }
    }
  }
}

static void monitor_resistors() {
  static unsigned long last_publish = 0;
  unsigned long now = millis();

  resistor_a = calculate_resistor_value(PIN_FUSE_A);
  resistor_b = calculate_resistor_value(PIN_FUSE_B);
  resistor_c = calculate_resistor_value(PIN_FUSE_C);

  bool changed = (resistor_a != last_resistor_a || resistor_b != last_resistor_b || resistor_c != last_resistor_c);
  if (!changed && (now - last_publish) < 5000) return;
  last_publish = now;

  if (changed) {
    last_resistor_a = resistor_a;
    last_resistor_b = resistor_b;
    last_resistor_c = resistor_c;
  }

  publish_state_fuse(sne_fuse_a, resistor_a);
  publish_state_fuse(sne_fuse_b, resistor_b);
  publish_state_fuse(sne_fuse_c, resistor_c);
}

static void monitor_knife() {
  static unsigned long last_publish = 0;
  unsigned long now = millis();

  knife_switch_on = (digitalRead(PIN_KNIFE_SWITCH) == HIGH);
  bool changed = (knife_switch_on != last_knife_switch_on);
  if (!changed && (now - last_publish) < 5000) return;
  last_publish = now;
  last_knife_switch_on = knife_switch_on;
  publish_state_knife();
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

  switch (ctx->kind) {
  case DeviceKind::Actuator:
    if (strcmp(op, "forward") == 0) {
      digitalWrite(PIN_ACTUATOR_FWD, HIGH);
      digitalWrite(PIN_ACTUATOR_RWD, LOW);
      publish_state_actuator("FORWARD");
      return true;
    }
    if (strcmp(op, "reverse") == 0) {
      digitalWrite(PIN_ACTUATOR_FWD, LOW);
      digitalWrite(PIN_ACTUATOR_RWD, HIGH);
      publish_state_actuator("REVERSE");
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      digitalWrite(PIN_ACTUATOR_FWD, LOW);
      digitalWrite(PIN_ACTUATOR_RWD, LOW);
      publish_state_actuator("STOP");
      return true;
    }
    goto bad;

  case DeviceKind::MaglockB:
    if (strcmp(op, "drop_panel") != 0) goto bad;
    maglock_b_locked = false;
    apply_outputs();
    publish_state_maglock(sne_maglock_b, maglock_b_locked);
    return true;
  case DeviceKind::MaglockC:
    if (strcmp(op, "drop_panel") != 0) goto bad;
    maglock_c_locked = false;
    apply_outputs();
    publish_state_maglock(sne_maglock_c, maglock_c_locked);
    return true;
  case DeviceKind::MaglockD:
    if (strcmp(op, "drop_panel") != 0) goto bad;
    maglock_d_locked = false;
    apply_outputs();
    publish_state_maglock(sne_maglock_d, maglock_d_locked);
    return true;

  case DeviceKind::Gate:
    if (strcmp(op, "unlock_gate") != 0) goto bad;
    metal_gate_locked = false;
    apply_outputs();
    publish_state_gate();
    return true;

  case DeviceKind::RfidA:
  case DeviceKind::RfidB:
  case DeviceKind::RfidC:
  case DeviceKind::RfidD:
  case DeviceKind::RfidE:
  case DeviceKind::FuseA:
  case DeviceKind::FuseB:
  case DeviceKind::FuseC:
  case DeviceKind::Knife:
    if (strcmp(op, "request_status") == 0) {
      // Publish current state snapshot.
      if (ctx->kind == DeviceKind::RfidA) publish_state_rfid(sne_rfid_a, rfid_readers[0]);
      if (ctx->kind == DeviceKind::RfidB) publish_state_rfid(sne_rfid_b, rfid_readers[1]);
      if (ctx->kind == DeviceKind::RfidC) publish_state_rfid(sne_rfid_c, rfid_readers[2]);
      if (ctx->kind == DeviceKind::RfidD) publish_state_rfid(sne_rfid_d, rfid_readers[3]);
      if (ctx->kind == DeviceKind::RfidE) publish_state_rfid(sne_rfid_e, rfid_readers[4]);
      if (ctx->kind == DeviceKind::FuseA) publish_state_fuse(sne_fuse_a, resistor_a);
      if (ctx->kind == DeviceKind::FuseB) publish_state_fuse(sne_fuse_b, resistor_b);
      if (ctx->kind == DeviceKind::FuseC) publish_state_fuse(sne_fuse_c, resistor_c);
      if (ctx->kind == DeviceKind::Knife) publish_state_knife();
      return true;
    }
    if (strcmp(op, "noop") == 0) return true;
    goto bad;
  }

bad:
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  // RFID serial ports
  RFID_A.begin(9600);
  RFID_B.begin(9600);
  RFID_C.begin(9600);
  RFID_D.begin(9600);
  RFID_E.begin(9600);

  // Sensor pins
  pinMode(PIN_TIR_A, INPUT_PULLDOWN);
  pinMode(PIN_TIR_B, INPUT_PULLDOWN);
  pinMode(PIN_TIR_C, INPUT_PULLDOWN);
  pinMode(PIN_TIR_D, INPUT_PULLDOWN);
  pinMode(PIN_TIR_E, INPUT_PULLDOWN);
  pinMode(PIN_KNIFE_SWITCH, INPUT_PULLDOWN);

  // Outputs
  pinMode(PIN_ACTUATOR_FWD, OUTPUT);
  pinMode(PIN_ACTUATOR_RWD, OUTPUT);
  digitalWrite(PIN_ACTUATOR_FWD, LOW);
  digitalWrite(PIN_ACTUATOR_RWD, LOW);

  pinMode(PIN_MAGLOCK_B, OUTPUT);
  pinMode(PIN_MAGLOCK_C, OUTPUT);
  pinMode(PIN_MAGLOCK_D, OUTPUT);
  pinMode(PIN_METAL_GATE, OUTPUT);

  maglock_b_locked = true;
  maglock_c_locked = true;
  maglock_d_locked = true;
  metal_gate_locked = true;
  apply_outputs();

  // Initialize baseline reads
  resistor_a = calculate_resistor_value(PIN_FUSE_A);
  resistor_b = calculate_resistor_value(PIN_FUSE_B);
  resistor_c = calculate_resistor_value(PIN_FUSE_C);
  knife_switch_on = (digitalRead(PIN_KNIFE_SWITCH) == HIGH);
  last_knife_switch_on = knife_switch_on;

  // Start MQTT clients
  if (!sne_rfid_a.begin()) while (true) delay(1000);
  if (!sne_rfid_b.begin()) while (true) delay(1000);
  if (!sne_rfid_c.begin()) while (true) delay(1000);
  if (!sne_rfid_d.begin()) while (true) delay(1000);
  if (!sne_rfid_e.begin()) while (true) delay(1000);
  if (!sne_fuse_a.begin()) while (true) delay(1000);
  if (!sne_fuse_b.begin()) while (true) delay(1000);
  if (!sne_fuse_c.begin()) while (true) delay(1000);
  if (!sne_knife.begin()) while (true) delay(1000);
  if (!sne_actuator.begin()) while (true) delay(1000);
  if (!sne_maglock_b.begin()) while (true) delay(1000);
  if (!sne_maglock_c.begin()) while (true) delay(1000);
  if (!sne_maglock_d.begin()) while (true) delay(1000);
  if (!sne_gate.begin()) while (true) delay(1000);

  // Bind command handlers
  sne_rfid_a.setCommandHandler(handleCommand, &ctx_rfid_a);
  sne_rfid_b.setCommandHandler(handleCommand, &ctx_rfid_b);
  sne_rfid_c.setCommandHandler(handleCommand, &ctx_rfid_c);
  sne_rfid_d.setCommandHandler(handleCommand, &ctx_rfid_d);
  sne_rfid_e.setCommandHandler(handleCommand, &ctx_rfid_e);
  sne_fuse_a.setCommandHandler(handleCommand, &ctx_fuse_a);
  sne_fuse_b.setCommandHandler(handleCommand, &ctx_fuse_b);
  sne_fuse_c.setCommandHandler(handleCommand, &ctx_fuse_c);
  sne_knife.setCommandHandler(handleCommand, &ctx_knife);
  sne_actuator.setCommandHandler(handleCommand, &ctx_actuator);
  sne_maglock_b.setCommandHandler(handleCommand, &ctx_maglock_b);
  sne_maglock_c.setCommandHandler(handleCommand, &ctx_maglock_c);
  sne_maglock_d.setCommandHandler(handleCommand, &ctx_maglock_d);
  sne_gate.setCommandHandler(handleCommand, &ctx_gate);

  // Initial state publish
  publish_state_rfid(sne_rfid_a, rfid_readers[0]);
  publish_state_rfid(sne_rfid_b, rfid_readers[1]);
  publish_state_rfid(sne_rfid_c, rfid_readers[2]);
  publish_state_rfid(sne_rfid_d, rfid_readers[3]);
  publish_state_rfid(sne_rfid_e, rfid_readers[4]);
  publish_state_fuse(sne_fuse_a, resistor_a);
  publish_state_fuse(sne_fuse_b, resistor_b);
  publish_state_fuse(sne_fuse_c, resistor_c);
  publish_state_knife();
  publish_state_actuator("STOP");
  publish_state_maglock(sne_maglock_b, maglock_b_locked);
  publish_state_maglock(sne_maglock_c, maglock_c_locked);
  publish_state_maglock(sne_maglock_d, maglock_d_locked);
  publish_state_gate();
}

void loop() {
  sne_rfid_a.loop();
  sne_rfid_b.loop();
  sne_rfid_c.loop();
  sne_rfid_d.loop();
  sne_rfid_e.loop();
  sne_fuse_a.loop();
  sne_fuse_b.loop();
  sne_fuse_c.loop();
  sne_knife.loop();
  sne_actuator.loop();
  sne_maglock_b.loop();
  sne_maglock_c.loop();
  sne_maglock_d.loop();
  sne_gate.loop();

  monitor_rfid();
  monitor_resistors();
  monitor_knife();
}
