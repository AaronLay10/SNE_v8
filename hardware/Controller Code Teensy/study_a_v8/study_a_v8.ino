// Study A Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - study_a_tentacle_mover_a
// - study_a_tentacle_mover_b
// - study_a_riddle_motor
// - study_a_porthole_controller
// - study_a_tentacle_sensors

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
static const char *HMAC_MOVER_A = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MOVER_B = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_RIDDLE_MOTOR = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_PORTHOLE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_TENTACLE_SENSORS = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (from v2)
static const int PIN_POWER_LED = 13;

// Tentacle move outputs (2x2 = 4 outputs)
static const int PIN_TENTACLE_MOVE_A1 = 0;
static const int PIN_TENTACLE_MOVE_A2 = 1;
static const int PIN_TENTACLE_MOVE_B1 = 2;
static const int PIN_TENTACLE_MOVE_B2 = 3;

// Porthole sensors (6 inputs, INPUT_PULLUP, active LOW)
static const int PIN_PORTHOLE_A1 = 5;
static const int PIN_PORTHOLE_A2 = 6;
static const int PIN_PORTHOLE_B1 = 7;
static const int PIN_PORTHOLE_B2 = 8;
static const int PIN_PORTHOLE_C1 = 9;
static const int PIN_PORTHOLE_C2 = 10;

// Porthole control outputs
static const int PIN_PORTHOLE_OPEN = 33;
static const int PIN_PORTHOLE_CLOSE = 34;

// Riddle motor
static const int PIN_RIDDLE_MOTOR = 12;

// Tentacle sensors (16 inputs, INPUT_PULLUP, active LOW)
static const int PIN_TENTACLE_A1 = 14;
static const int PIN_TENTACLE_A2 = 15;
static const int PIN_TENTACLE_A3 = 16;
static const int PIN_TENTACLE_A4 = 17;
static const int PIN_TENTACLE_B1 = 18;
static const int PIN_TENTACLE_B2 = 19;
static const int PIN_TENTACLE_B3 = 20;
static const int PIN_TENTACLE_B4 = 21;
static const int PIN_TENTACLE_C1 = 22;
static const int PIN_TENTACLE_C2 = 23;
static const int PIN_TENTACLE_C3 = 36;
static const int PIN_TENTACLE_C4 = 37;
static const int PIN_TENTACLE_D1 = 38;
static const int PIN_TENTACLE_D2 = 39;
static const int PIN_TENTACLE_D3 = 40;
static const int PIN_TENTACLE_D4 = 41;

static const unsigned long SENSOR_REFRESH_MS = 60UL * 1000UL;

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

static sentient_v8::Client sne_mover_a(make_cfg("study_a_tentacle_mover_a", HMAC_MOVER_A));
static sentient_v8::Client sne_mover_b(make_cfg("study_a_tentacle_mover_b", HMAC_MOVER_B));
static sentient_v8::Client sne_riddle_motor(make_cfg("study_a_riddle_motor", HMAC_RIDDLE_MOTOR));
static sentient_v8::Client sne_porthole(make_cfg("study_a_porthole_controller", HMAC_PORTHOLE));
static sentient_v8::Client sne_tentacle_sensors(make_cfg("study_a_tentacle_sensors", HMAC_TENTACLE_SENSORS));

enum class DeviceKind : uint8_t { MoverA, MoverB, RiddleMotor, Porthole, TentacleSensors };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_mover_a = {DeviceKind::MoverA};
static DeviceCtx ctx_mover_b = {DeviceKind::MoverB};
static DeviceCtx ctx_riddle = {DeviceKind::RiddleMotor};
static DeviceCtx ctx_porthole = {DeviceKind::Porthole};
static DeviceCtx ctx_tentacles = {DeviceKind::TentacleSensors};

static bool riddle_motor_on = false;
static const char *porthole_commanded = "close";

static bool porthole_last[6] = {false, false, false, false, false, false};
static bool tentacle_last[16] = {false};

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

static bool active_low(int pin) { return digitalRead(pin) == LOW; }

static void read_portholes(bool out[6]) {
  out[0] = active_low(PIN_PORTHOLE_A1);
  out[1] = active_low(PIN_PORTHOLE_A2);
  out[2] = active_low(PIN_PORTHOLE_B1);
  out[3] = active_low(PIN_PORTHOLE_B2);
  out[4] = active_low(PIN_PORTHOLE_C1);
  out[5] = active_low(PIN_PORTHOLE_C2);
}

static void read_tentacles(bool out[16]) {
  out[0] = active_low(PIN_TENTACLE_A1);
  out[1] = active_low(PIN_TENTACLE_A2);
  out[2] = active_low(PIN_TENTACLE_A3);
  out[3] = active_low(PIN_TENTACLE_A4);
  out[4] = active_low(PIN_TENTACLE_B1);
  out[5] = active_low(PIN_TENTACLE_B2);
  out[6] = active_low(PIN_TENTACLE_B3);
  out[7] = active_low(PIN_TENTACLE_B4);
  out[8] = active_low(PIN_TENTACLE_C1);
  out[9] = active_low(PIN_TENTACLE_C2);
  out[10] = active_low(PIN_TENTACLE_C3);
  out[11] = active_low(PIN_TENTACLE_C4);
  out[12] = active_low(PIN_TENTACLE_D1);
  out[13] = active_low(PIN_TENTACLE_D2);
  out[14] = active_low(PIN_TENTACLE_D3);
  out[15] = active_low(PIN_TENTACLE_D4);
}

static void publish_state_all(const char *reason) {
  {
    StaticJsonDocument<192> st;
    st["commanded"] = "n/a";
    st["reason"] = reason ? reason : "";
    sne_mover_a.publishState(st);
  }
  {
    StaticJsonDocument<192> st;
    st["commanded"] = "n/a";
    st["reason"] = reason ? reason : "";
    sne_mover_b.publishState(st);
  }
  {
    StaticJsonDocument<128> st;
    st["on"] = riddle_motor_on;
    st["reason"] = reason ? reason : "";
    sne_riddle_motor.publishState(st);
  }
  {
    bool p[6];
    read_portholes(p);
    StaticJsonDocument<256> st;
    st["commanded"] = porthole_commanded;
    st["a1"] = p[0];
    st["a2"] = p[1];
    st["b1"] = p[2];
    st["b2"] = p[3];
    st["c1"] = p[4];
    st["c2"] = p[5];
    st["reason"] = reason ? reason : "";
    sne_porthole.publishState(st);
  }
  {
    bool t[16];
    read_tentacles(t);
    StaticJsonDocument<768> st;
    st["a1"] = t[0];
    st["a2"] = t[1];
    st["a3"] = t[2];
    st["a4"] = t[3];
    st["b1"] = t[4];
    st["b2"] = t[5];
    st["b3"] = t[6];
    st["b4"] = t[7];
    st["c1"] = t[8];
    st["c2"] = t[9];
    st["c3"] = t[10];
    st["c4"] = t[11];
    st["d1"] = t[12];
    st["d2"] = t[13];
    st["d3"] = t[14];
    st["d4"] = t[15];
    st["reason"] = reason ? reason : "";
    sne_tentacle_sensors.publishState(st);
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

  if (strcmp(op, "request_status") == 0) {
    publish_state_all("request_status");
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

  if (ctx->kind == DeviceKind::MoverA) {
    if (strcmp(op, "up") == 0) {
      digitalWrite(PIN_TENTACLE_MOVE_A1, HIGH);
      digitalWrite(PIN_TENTACLE_MOVE_A2, LOW);
      publish_state_all("up");
      return true;
    }
    if (strcmp(op, "down") == 0) {
      digitalWrite(PIN_TENTACLE_MOVE_A1, LOW);
      digitalWrite(PIN_TENTACLE_MOVE_A2, HIGH);
      publish_state_all("down");
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      digitalWrite(PIN_TENTACLE_MOVE_A1, LOW);
      digitalWrite(PIN_TENTACLE_MOVE_A2, LOW);
      publish_state_all("stop");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::MoverB) {
    if (strcmp(op, "up") == 0) {
      digitalWrite(PIN_TENTACLE_MOVE_B1, HIGH);
      digitalWrite(PIN_TENTACLE_MOVE_B2, LOW);
      publish_state_all("up");
      return true;
    }
    if (strcmp(op, "down") == 0) {
      digitalWrite(PIN_TENTACLE_MOVE_B1, LOW);
      digitalWrite(PIN_TENTACLE_MOVE_B2, HIGH);
      publish_state_all("down");
      return true;
    }
    if (strcmp(op, "stop") == 0) {
      digitalWrite(PIN_TENTACLE_MOVE_B1, LOW);
      digitalWrite(PIN_TENTACLE_MOVE_B2, LOW);
      publish_state_all("stop");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::RiddleMotor) {
    if (strcmp(op, "set") == 0) {
      if (!p.containsKey("on")) {
        rejectedAckReason["reason_code"] = "INVALID_PARAMS";
        return false;
      }
      bool on = p["on"] | false;
      digitalWrite(PIN_RIDDLE_MOTOR, on ? HIGH : LOW);
      riddle_motor_on = on;
      publish_state_all("set");
      return true;
    }
    if (strcmp(op, "motor_on") == 0) {
      digitalWrite(PIN_RIDDLE_MOTOR, HIGH);
      riddle_motor_on = true;
      publish_state_all("motor_on");
      return true;
    }
    if (strcmp(op, "motor_off") == 0) {
      digitalWrite(PIN_RIDDLE_MOTOR, LOW);
      riddle_motor_on = false;
      publish_state_all("motor_off");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::Porthole) {
    if (strcmp(op, "open") == 0) {
      digitalWrite(PIN_PORTHOLE_OPEN, HIGH);
      digitalWrite(PIN_PORTHOLE_CLOSE, LOW);
      porthole_commanded = "open";
      publish_state_all("open");
      return true;
    }
    if (strcmp(op, "close") == 0) {
      digitalWrite(PIN_PORTHOLE_OPEN, LOW);
      digitalWrite(PIN_PORTHOLE_CLOSE, HIGH);
      porthole_commanded = "close";
      publish_state_all("close");
      return true;
    }
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Sensor-only device.
  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  pinMode(PIN_TENTACLE_MOVE_A1, OUTPUT);
  pinMode(PIN_TENTACLE_MOVE_A2, OUTPUT);
  pinMode(PIN_TENTACLE_MOVE_B1, OUTPUT);
  pinMode(PIN_TENTACLE_MOVE_B2, OUTPUT);
  digitalWrite(PIN_TENTACLE_MOVE_A1, LOW);
  digitalWrite(PIN_TENTACLE_MOVE_A2, LOW);
  digitalWrite(PIN_TENTACLE_MOVE_B1, LOW);
  digitalWrite(PIN_TENTACLE_MOVE_B2, LOW);

  pinMode(PIN_PORTHOLE_A1, INPUT_PULLUP);
  pinMode(PIN_PORTHOLE_A2, INPUT_PULLUP);
  pinMode(PIN_PORTHOLE_B1, INPUT_PULLUP);
  pinMode(PIN_PORTHOLE_B2, INPUT_PULLUP);
  pinMode(PIN_PORTHOLE_C1, INPUT_PULLUP);
  pinMode(PIN_PORTHOLE_C2, INPUT_PULLUP);

  pinMode(PIN_RIDDLE_MOTOR, OUTPUT);
  digitalWrite(PIN_RIDDLE_MOTOR, LOW);
  riddle_motor_on = false;

  pinMode(PIN_TENTACLE_A1, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_A2, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_A3, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_A4, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_B1, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_B2, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_B3, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_B4, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_C1, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_C2, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_C3, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_C4, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_D1, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_D2, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_D3, INPUT_PULLUP);
  pinMode(PIN_TENTACLE_D4, INPUT_PULLUP);

  pinMode(PIN_PORTHOLE_OPEN, OUTPUT);
  pinMode(PIN_PORTHOLE_CLOSE, OUTPUT);
  digitalWrite(PIN_PORTHOLE_OPEN, LOW);
  digitalWrite(PIN_PORTHOLE_CLOSE, HIGH);
  porthole_commanded = "close";

  if (!sne_mover_a.begin()) while (true) delay(1000);
  if (!sne_mover_b.begin()) while (true) delay(1000);
  if (!sne_riddle_motor.begin()) while (true) delay(1000);
  if (!sne_porthole.begin()) while (true) delay(1000);
  if (!sne_tentacle_sensors.begin()) while (true) delay(1000);

  sne_mover_a.setCommandHandler(handleCommand, &ctx_mover_a);
  sne_mover_b.setCommandHandler(handleCommand, &ctx_mover_b);
  sne_riddle_motor.setCommandHandler(handleCommand, &ctx_riddle);
  sne_porthole.setCommandHandler(handleCommand, &ctx_porthole);
  sne_tentacle_sensors.setCommandHandler(handleCommand, &ctx_tentacles);

  bool p[6];
  read_portholes(p);
  for (int i = 0; i < 6; i++) porthole_last[i] = p[i];
  bool t[16];
  read_tentacles(t);
  for (int i = 0; i < 16; i++) tentacle_last[i] = t[i];

  publish_state_all("boot");
}

void loop() {
  sne_mover_a.loop();
  sne_mover_b.loop();
  sne_riddle_motor.loop();
  sne_porthole.loop();
  sne_tentacle_sensors.loop();

  bool p[6];
  bool t[16];
  read_portholes(p);
  read_tentacles(t);

  bool changed = false;
  for (int i = 0; i < 6; i++) {
    if (p[i] != porthole_last[i]) {
      changed = true;
      break;
    }
  }
  if (!changed) {
    for (int i = 0; i < 16; i++) {
      if (t[i] != tentacle_last[i]) {
        changed = true;
        break;
      }
    }
  }

  static unsigned long last_publish = 0;
  const unsigned long now = millis();
  if (changed || (now - last_publish) > SENSOR_REFRESH_MS) {
    last_publish = now;
    for (int i = 0; i < 6; i++) porthole_last[i] = p[i];
    for (int i = 0; i < 16; i++) tentacle_last[i] = t[i];
    publish_state_all(changed ? "change" : "periodic");
  }
}
