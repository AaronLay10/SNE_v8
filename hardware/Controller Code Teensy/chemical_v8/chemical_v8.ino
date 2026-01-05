// Chemical Puzzle Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - chemical_rfid_a, chemical_rfid_b, chemical_rfid_c, chemical_rfid_d, chemical_rfid_e, chemical_rfid_f
// - chemical_engine_block_actuator
// - chemical_chest_maglocks

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
static const char *HMAC_RFID_F = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_ACTUATOR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MAGLOCKS = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins
static const int power_led_pin = 13;

// RFID Serial Ports
#define RFID_C Serial1
#define RFID_E Serial2
#define RFID_A Serial3
#define RFID_F Serial4
#define RFID_B Serial5
#define RFID_D Serial6

// RFID - Tag in Range Sensor (TIR) pins
static const int tir_pin_a = 41;
static const int tir_pin_b = 19;
static const int tir_pin_c = 2;
static const int tir_pin_d = 26;
static const int tir_pin_e = 9;
static const int tir_pin_f = 18;

// Actuator and Maglock pins
static const int actuator_fwd_pin = 22;
static const int actuator_rwd_pin = 23;
static const int maglocks_pin = 36; // HIGH = locked

static bool maglocks_locked = true;
static const int id_buffer_len = 20;

struct RfidReader {
  HardwareSerial *serial;
  int tir_pin;
  char tag[id_buffer_len];
  char last_tag[id_buffer_len];
  char buf[32];
  uint8_t count;
  bool packet_started;
  bool present;
};

static RfidReader rfid[] = {
    {&RFID_A, tir_pin_a, "", "", {0}, 0, false, false},
    {&RFID_B, tir_pin_b, "", "", {0}, 0, false, false},
    {&RFID_C, tir_pin_c, "", "", {0}, 0, false, false},
    {&RFID_D, tir_pin_d, "", "", {0}, 0, false, false},
    {&RFID_E, tir_pin_e, "", "", {0}, 0, false, false},
    {&RFID_F, tir_pin_f, "", "", {0}, 0, false, false},
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

static sentient_v8::Client sne_rfid_a(make_cfg("chemical_rfid_a", HMAC_RFID_A));
static sentient_v8::Client sne_rfid_b(make_cfg("chemical_rfid_b", HMAC_RFID_B));
static sentient_v8::Client sne_rfid_c(make_cfg("chemical_rfid_c", HMAC_RFID_C));
static sentient_v8::Client sne_rfid_d(make_cfg("chemical_rfid_d", HMAC_RFID_D));
static sentient_v8::Client sne_rfid_e(make_cfg("chemical_rfid_e", HMAC_RFID_E));
static sentient_v8::Client sne_rfid_f(make_cfg("chemical_rfid_f", HMAC_RFID_F));
static sentient_v8::Client sne_actuator(make_cfg("chemical_engine_block_actuator", HMAC_ACTUATOR));
static sentient_v8::Client sne_maglocks(make_cfg("chemical_chest_maglocks", HMAC_MAGLOCKS));

enum class DeviceKind : uint8_t { RfidA, RfidB, RfidC, RfidD, RfidE, RfidF, Actuator, Maglocks };
struct DeviceCtx {
  DeviceKind kind;
};

static DeviceCtx ctx_rfid_a = {DeviceKind::RfidA};
static DeviceCtx ctx_rfid_b = {DeviceKind::RfidB};
static DeviceCtx ctx_rfid_c = {DeviceKind::RfidC};
static DeviceCtx ctx_rfid_d = {DeviceKind::RfidD};
static DeviceCtx ctx_rfid_e = {DeviceKind::RfidE};
static DeviceCtx ctx_rfid_f = {DeviceKind::RfidF};
static DeviceCtx ctx_actuator = {DeviceKind::Actuator};
static DeviceCtx ctx_maglocks = {DeviceKind::Maglocks};

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
  digitalWrite(maglocks_pin, maglocks_locked ? HIGH : LOW);
}

static void publish_state_rfid(sentient_v8::Client &c, RfidReader &r) {
  StaticJsonDocument<256> st;
  st["tir_in_range"] = (digitalRead(r.tir_pin) == HIGH);
  st["present"] = r.present;
  st["tag_id"] = (r.present && r.tag[0]) ? r.tag : "";
  c.publishState(st);
}

static void publish_state_actuator(const char *mode) {
  StaticJsonDocument<192> st;
  st["mode"] = mode ? mode : "STOP";
  sne_actuator.publishState(st);
}

static void publish_state_maglocks() {
  StaticJsonDocument<128> st;
  st["locked"] = maglocks_locked;
  sne_maglocks.publishState(st);
}

static void monitor_rfid() {
  static bool last_present[6] = {false, false, false, false, false, false};
  static unsigned long last_pub[6] = {0, 0, 0, 0, 0, 0};

  for (size_t i = 0; i < (sizeof(rfid) / sizeof(rfid[0])); i++) {
    RfidReader &r = rfid[i];

    while (r.serial->available()) {
      char ch = (char)r.serial->read();
      if ((uint8_t)ch == 0x02) {
        r.count = 0;
        r.packet_started = true;
      } else if ((uint8_t)ch == 0x03 && r.packet_started) {
        r.buf[r.count] = '\0';

        // Remove trailing CR
        size_t len = strlen(r.buf);
        if (len > 0 && r.buf[len - 1] == '\r') r.buf[len - 1] = '\0';

        strncpy(r.tag, r.buf, sizeof(r.tag) - 1);
        r.tag[sizeof(r.tag) - 1] = '\0';
        r.packet_started = false;
      } else if (r.packet_started && r.count < sizeof(r.buf) - 1) {
        r.buf[r.count++] = ch;
      }
    }

    bool tir = (digitalRead(r.tir_pin) == HIGH);
    if (!tir) {
      if (r.present) {
        r.present = false;
        r.tag[0] = '\0';
      }
    } else {
      if (!r.present && r.tag[0]) {
        r.present = true;
      }
    }

    bool present_changed = (r.present != last_present[i]);
    if (present_changed) last_present[i] = r.present;

    bool tag_changed = (strcmp(r.tag, r.last_tag) != 0);
    if (tag_changed) {
      strncpy(r.last_tag, r.tag, sizeof(r.last_tag));
    }

    unsigned long now = millis();
    bool periodic = (now - last_pub[i] > 5000);
    if (present_changed || tag_changed || periodic) {
      last_pub[i] = now;
      if (i == 0) publish_state_rfid(sne_rfid_a, r);
      if (i == 1) publish_state_rfid(sne_rfid_b, r);
      if (i == 2) publish_state_rfid(sne_rfid_c, r);
      if (i == 3) publish_state_rfid(sne_rfid_d, r);
      if (i == 4) publish_state_rfid(sne_rfid_e, r);
      if (i == 5) publish_state_rfid(sne_rfid_f, r);
    }
  }
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

  if (ctx->kind == DeviceKind::Actuator) {
    if (strcmp(op, "forward") == 0) {
      digitalWrite(actuator_fwd_pin, HIGH);
      digitalWrite(actuator_rwd_pin, LOW);
      publish_state_actuator("FORWARD");
      return true;
    }
    if (strcmp(op, "reverse") == 0) {
      digitalWrite(actuator_fwd_pin, LOW);
      digitalWrite(actuator_rwd_pin, HIGH);
      publish_state_actuator("REVERSE");
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      digitalWrite(actuator_fwd_pin, LOW);
      digitalWrite(actuator_rwd_pin, LOW);
      publish_state_actuator("STOP");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::Maglocks) {
    if (strcmp(op, "lock") == 0) {
      maglocks_locked = true;
      apply_outputs();
      publish_state_maglocks();
      return true;
    }
    if (strcmp(op, "unlock") == 0) {
      maglocks_locked = false;
      apply_outputs();
      publish_state_maglocks();
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // RFID devices are input-only.
  if (strcmp(op, "request_status") == 0 || strcmp(op, "noop") == 0) {
    return true;
  }
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  // Serial RFID
  RFID_A.begin(9600);
  RFID_B.begin(9600);
  RFID_C.begin(9600);
  RFID_D.begin(9600);
  RFID_E.begin(9600);
  RFID_F.begin(9600);

  // TIR pins
  pinMode(tir_pin_a, INPUT_PULLDOWN);
  pinMode(tir_pin_b, INPUT_PULLDOWN);
  pinMode(tir_pin_c, INPUT_PULLDOWN);
  pinMode(tir_pin_d, INPUT_PULLDOWN);
  pinMode(tir_pin_e, INPUT_PULLDOWN);
  pinMode(tir_pin_f, INPUT_PULLDOWN);

  // Outputs
  pinMode(actuator_fwd_pin, OUTPUT);
  pinMode(actuator_rwd_pin, OUTPUT);
  pinMode(maglocks_pin, OUTPUT);
  pinMode(power_led_pin, OUTPUT);

  digitalWrite(power_led_pin, HIGH);
  digitalWrite(actuator_fwd_pin, LOW);
  digitalWrite(actuator_rwd_pin, LOW);
  maglocks_locked = true;
  apply_outputs();

  if (!sne_rfid_a.begin()) while (true) delay(1000);
  if (!sne_rfid_b.begin()) while (true) delay(1000);
  if (!sne_rfid_c.begin()) while (true) delay(1000);
  if (!sne_rfid_d.begin()) while (true) delay(1000);
  if (!sne_rfid_e.begin()) while (true) delay(1000);
  if (!sne_rfid_f.begin()) while (true) delay(1000);
  if (!sne_actuator.begin()) while (true) delay(1000);
  if (!sne_maglocks.begin()) while (true) delay(1000);

  sne_rfid_a.setCommandHandler(handleCommand, &ctx_rfid_a);
  sne_rfid_b.setCommandHandler(handleCommand, &ctx_rfid_b);
  sne_rfid_c.setCommandHandler(handleCommand, &ctx_rfid_c);
  sne_rfid_d.setCommandHandler(handleCommand, &ctx_rfid_d);
  sne_rfid_e.setCommandHandler(handleCommand, &ctx_rfid_e);
  sne_rfid_f.setCommandHandler(handleCommand, &ctx_rfid_f);
  sne_actuator.setCommandHandler(handleCommand, &ctx_actuator);
  sne_maglocks.setCommandHandler(handleCommand, &ctx_maglocks);

  publish_state_actuator("STOP");
  publish_state_maglocks();
  publish_state_rfid(sne_rfid_a, rfid[0]);
  publish_state_rfid(sne_rfid_b, rfid[1]);
  publish_state_rfid(sne_rfid_c, rfid[2]);
  publish_state_rfid(sne_rfid_d, rfid[3]);
  publish_state_rfid(sne_rfid_e, rfid[4]);
  publish_state_rfid(sne_rfid_f, rfid[5]);
}

void loop() {
  sne_rfid_a.loop();
  sne_rfid_b.loop();
  sne_rfid_c.loop();
  sne_rfid_d.loop();
  sne_rfid_e.loop();
  sne_rfid_f.loop();
  sne_actuator.loop();
  sne_maglocks.loop();

  monitor_rfid();
}
