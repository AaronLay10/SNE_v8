// Lever Fan Safe Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - lever_fan_safe_photocell_safe
// - lever_fan_safe_photocell_fan
// - lever_fan_safe_ir_receiver_safe
// - lever_fan_safe_ir_receiver_fan
// - lever_fan_safe_maglock_fan
// - lever_fan_safe_solenoid_safe
// - lever_fan_safe_fan_motor

#include <Arduino.h>
#include <ArduinoJson.h>

// Suppress IRremote begin() error - receiver only.
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>

#include <AccelStepper.h>
#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_PHOTOCELL_SAFE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_PHOTOCELL_FAN = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_IR_SAFE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_IR_FAN = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_MAGLOCK_FAN = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_SOLENOID_SAFE = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_FAN_MOTOR = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (from v2)
static const int PIN_POWER_LED = 13;
static const int PIN_PHOTOCELL_SAFE = A1;
static const int PIN_PHOTOCELL_FAN = A0;
static const int PIN_IR_FAN = 16;
static const int PIN_IR_SAFE = 17;
static const int PIN_MAGLOCK_FAN = 41;
static const int PIN_SOLENOID_SAFE = 40;
static const int PIN_FAN_MOTOR_ENABLE = 37; // active LOW enable

static const int PIN_STEPPER_1 = 33;
static const int PIN_STEPPER_2 = 34;
static const int PIN_STEPPER_3 = 35;
static const int PIN_STEPPER_4 = 36;

static const int PHOTOCELL_THRESHOLD = 300;
static const unsigned long IR_SWITCH_INTERVAL_MS = 200;
static const unsigned long SENSOR_REFRESH_MS = 60UL * 1000UL;

static const int FAN_SPEED_STEPS_PER_SEC = 1500;

static AccelStepper stepper(AccelStepper::FULL4WIRE, PIN_STEPPER_1, PIN_STEPPER_2, PIN_STEPPER_3, PIN_STEPPER_4);

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

static sentient_v8::Client sne_photocell_safe(make_cfg("lever_fan_safe_photocell_safe", HMAC_PHOTOCELL_SAFE));
static sentient_v8::Client sne_photocell_fan(make_cfg("lever_fan_safe_photocell_fan", HMAC_PHOTOCELL_FAN));
static sentient_v8::Client sne_ir_safe(make_cfg("lever_fan_safe_ir_receiver_safe", HMAC_IR_SAFE));
static sentient_v8::Client sne_ir_fan(make_cfg("lever_fan_safe_ir_receiver_fan", HMAC_IR_FAN));
static sentient_v8::Client sne_maglock_fan(make_cfg("lever_fan_safe_maglock_fan", HMAC_MAGLOCK_FAN));
static sentient_v8::Client sne_solenoid_safe(make_cfg("lever_fan_safe_solenoid_safe", HMAC_SOLENOID_SAFE));
static sentient_v8::Client sne_fan_motor(make_cfg("lever_fan_safe_fan_motor", HMAC_FAN_MOTOR));

enum class DeviceKind : uint8_t { PhotocellSafe, PhotocellFan, IrSafe, IrFan, MaglockFan, SolenoidSafe, FanMotor };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_photocell_safe = {DeviceKind::PhotocellSafe};
static DeviceCtx ctx_photocell_fan = {DeviceKind::PhotocellFan};
static DeviceCtx ctx_ir_safe = {DeviceKind::IrSafe};
static DeviceCtx ctx_ir_fan = {DeviceKind::IrFan};
static DeviceCtx ctx_maglock_fan = {DeviceKind::MaglockFan};
static DeviceCtx ctx_solenoid_safe = {DeviceKind::SolenoidSafe};
static DeviceCtx ctx_fan_motor = {DeviceKind::FanMotor};

static sentient_v8::Client *clients[] = {&sne_photocell_safe, &sne_photocell_fan, &sne_ir_safe, &sne_ir_fan, &sne_maglock_fan, &sne_solenoid_safe, &sne_fan_motor};

// Runtime state
static int photocell_safe_raw = 0;
static int photocell_fan_raw = 0;
static bool safe_lever_open = false;
static bool fan_lever_open = false;
static bool last_safe_lever_open = false;
static bool last_fan_lever_open = false;

static bool ir_enabled = true;
static int current_ir_pin = PIN_IR_FAN;
static unsigned long last_ir_switch_ms = 0;
static bool ir_signal_in_progress = false;

static bool maglock_locked = true;   // HIGH=locked
static bool solenoid_open = false;   // HIGH=open
static bool fan_running = false;     // stepper spinning
static int fan_speed = 0;

static uint32_t last_ir_code_safe = 0;
static uint32_t last_ir_raw_safe = 0;
static uint32_t last_ir_code_fan = 0;
static uint32_t last_ir_raw_fan = 0;
static bool ir_seen_safe = false;
static bool ir_seen_fan = false;

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

static const char *lever_state(bool open) { return open ? "OPEN" : "CLOSED"; }

static void read_sensors() {
  photocell_safe_raw = analogRead(PIN_PHOTOCELL_SAFE);
  photocell_fan_raw = analogRead(PIN_PHOTOCELL_FAN);
  safe_lever_open = photocell_safe_raw > PHOTOCELL_THRESHOLD;
  fan_lever_open = photocell_fan_raw > PHOTOCELL_THRESHOLD;
}

static void publish_all(const char *reason) {
  {
    StaticJsonDocument<256> st;
    st["lever_position"] = lever_state(safe_lever_open);
    st["open"] = safe_lever_open;
    st["raw"] = photocell_safe_raw;
    st["reason"] = reason ? reason : "";
    sne_photocell_safe.publishState(st);
  }
  {
    StaticJsonDocument<256> st;
    st["lever_position"] = lever_state(fan_lever_open);
    st["open"] = fan_lever_open;
    st["raw"] = photocell_fan_raw;
    st["reason"] = reason ? reason : "";
    sne_photocell_fan.publishState(st);
  }
  {
    StaticJsonDocument<320> st;
    st["enabled"] = ir_enabled;
    if (ir_seen_safe) {
      st["last_code"] = (uint32_t)last_ir_code_safe;
      st["last_raw"] = (uint32_t)last_ir_raw_safe;
    }
    st["reason"] = reason ? reason : "";
    sne_ir_safe.publishState(st);
  }
  {
    StaticJsonDocument<320> st;
    st["enabled"] = ir_enabled;
    if (ir_seen_fan) {
      st["last_code"] = (uint32_t)last_ir_code_fan;
      st["last_raw"] = (uint32_t)last_ir_raw_fan;
    }
    st["reason"] = reason ? reason : "";
    sne_ir_fan.publishState(st);
  }
  {
    StaticJsonDocument<192> st;
    st["locked"] = maglock_locked;
    st["reason"] = reason ? reason : "";
    sne_maglock_fan.publishState(st);
  }
  {
    StaticJsonDocument<192> st;
    st["open"] = solenoid_open;
    st["reason"] = reason ? reason : "";
    sne_solenoid_safe.publishState(st);
  }
  {
    StaticJsonDocument<256> st;
    st["running"] = fan_running;
    st["speed"] = fan_speed;
    st["reason"] = reason ? reason : "";
    sne_fan_motor.publishState(st);
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
    read_sensors();
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

  switch (ctx->kind) {
    case DeviceKind::MaglockFan: {
      if (strcmp(op, "lock") == 0) {
        digitalWrite(PIN_MAGLOCK_FAN, HIGH);
        maglock_locked = true;
        publish_all("lock");
        return true;
      }
      if (strcmp(op, "unlock") == 0) {
        digitalWrite(PIN_MAGLOCK_FAN, LOW);
        maglock_locked = false;
        publish_all("unlock");
        return true;
      }
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    case DeviceKind::SolenoidSafe: {
      if (strcmp(op, "open") == 0) {
        digitalWrite(PIN_SOLENOID_SAFE, HIGH);
        solenoid_open = true;
        publish_all("open");
        return true;
      }
      if (strcmp(op, "close") == 0) {
        digitalWrite(PIN_SOLENOID_SAFE, LOW);
        solenoid_open = false;
        publish_all("close");
        return true;
      }
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    case DeviceKind::FanMotor: {
      if (strcmp(op, "fan_on") == 0) {
        digitalWrite(PIN_FAN_MOTOR_ENABLE, LOW); // active LOW enable
        fan_running = true;
        fan_speed = FAN_SPEED_STEPS_PER_SEC;
        stepper.setSpeed((float)fan_speed);
        publish_all("fan_on");
        return true;
      }
      if (strcmp(op, "fan_off") == 0) {
        digitalWrite(PIN_FAN_MOTOR_ENABLE, HIGH);
        fan_running = false;
        fan_speed = 0;
        stepper.setSpeed(0);
        publish_all("fan_off");
        return true;
      }
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    case DeviceKind::IrSafe:
    case DeviceKind::IrFan: {
      if (strcmp(op, "ir_enable") == 0) {
        ir_enabled = true;
        publish_all("ir_enable");
        return true;
      }
      if (strcmp(op, "ir_disable") == 0) {
        ir_enabled = false;
        publish_all("ir_disable");
        return true;
      }
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    case DeviceKind::PhotocellSafe:
    case DeviceKind::PhotocellFan: {
      // Sensors are input-only.
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
  }

  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

static void publish_ir_event(sentient_v8::Client &client, uint32_t code, uint32_t raw) {
  StaticJsonDocument<192> ev;
  ev["code"] = (uint32_t)code;
  ev["raw"] = (uint32_t)raw;
  ev["ts_ms"] = millis();
  client.publishTelemetry(ev);
}

static void handle_ir_signal(int pin) {
  bool is_noise = (IrReceiver.decodedIRData.command == 0 && IrReceiver.decodedIRData.address == 0 &&
                   IrReceiver.decodedIRData.decodedRawData == 0 && IrReceiver.decodedIRData.numberOfBits == 0);
  if (is_noise) return;

  uint32_t code = (uint32_t)IrReceiver.decodedIRData.command;
  uint32_t raw = (uint32_t)IrReceiver.decodedIRData.decodedRawData;

  if (pin == PIN_IR_SAFE) {
    ir_seen_safe = true;
    last_ir_code_safe = code;
    last_ir_raw_safe = raw;
    publish_ir_event(sne_ir_safe, code, raw);
  } else {
    ir_seen_fan = true;
    last_ir_code_fan = code;
    last_ir_raw_fan = raw;
    publish_ir_event(sne_ir_fan, code, raw);
  }

  publish_all("ir_event");
}

static void switch_ir_sensor() {
  current_ir_pin = (current_ir_pin == PIN_IR_FAN) ? PIN_IR_SAFE : PIN_IR_FAN;
  IrReceiver.begin(current_ir_pin, ENABLE_LED_FEEDBACK, PIN_POWER_LED);
  last_ir_switch_ms = millis();
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  pinMode(PIN_MAGLOCK_FAN, OUTPUT);
  pinMode(PIN_SOLENOID_SAFE, OUTPUT);
  pinMode(PIN_FAN_MOTOR_ENABLE, OUTPUT);
  pinMode(PIN_PHOTOCELL_SAFE, INPUT);
  pinMode(PIN_PHOTOCELL_FAN, INPUT);

  // Initial outputs
  digitalWrite(PIN_MAGLOCK_FAN, HIGH);
  maglock_locked = true;
  digitalWrite(PIN_SOLENOID_SAFE, LOW);
  solenoid_open = false;
  digitalWrite(PIN_FAN_MOTOR_ENABLE, HIGH);
  fan_running = false;
  fan_speed = 0;

  stepper.setMaxSpeed(3000);
  stepper.setSpeed(0);
  stepper.stop();

  // IR init
  IrReceiver.begin(current_ir_pin, ENABLE_LED_FEEDBACK, PIN_POWER_LED);
  last_ir_switch_ms = millis();

  if (!sne_photocell_safe.begin()) while (true) delay(1000);
  if (!sne_photocell_fan.begin()) while (true) delay(1000);
  if (!sne_ir_safe.begin()) while (true) delay(1000);
  if (!sne_ir_fan.begin()) while (true) delay(1000);
  if (!sne_maglock_fan.begin()) while (true) delay(1000);
  if (!sne_solenoid_safe.begin()) while (true) delay(1000);
  if (!sne_fan_motor.begin()) while (true) delay(1000);

  sne_photocell_safe.setCommandHandler(handleCommand, &ctx_photocell_safe);
  sne_photocell_fan.setCommandHandler(handleCommand, &ctx_photocell_fan);
  sne_ir_safe.setCommandHandler(handleCommand, &ctx_ir_safe);
  sne_ir_fan.setCommandHandler(handleCommand, &ctx_ir_fan);
  sne_maglock_fan.setCommandHandler(handleCommand, &ctx_maglock_fan);
  sne_solenoid_safe.setCommandHandler(handleCommand, &ctx_solenoid_safe);
  sne_fan_motor.setCommandHandler(handleCommand, &ctx_fan_motor);

  read_sensors();
  last_safe_lever_open = safe_lever_open;
  last_fan_lever_open = fan_lever_open;
  publish_all("boot");
}

void loop() {
  for (size_t i = 0; i < (sizeof(clients) / sizeof(clients[0])); i++) clients[i]->loop();

  read_sensors();

  static unsigned long last_publish_ms = 0;
  const unsigned long now = millis();
  bool periodic_due = (now - last_publish_ms) >= SENSOR_REFRESH_MS;
  bool changed = (safe_lever_open != last_safe_lever_open) || (fan_lever_open != last_fan_lever_open);

  if (changed || periodic_due) {
    last_safe_lever_open = safe_lever_open;
    last_fan_lever_open = fan_lever_open;
    last_publish_ms = now;
    publish_all(changed ? "change" : "periodic");
  }

  // Handle IR signal
  if (ir_enabled && IrReceiver.decode()) {
    ir_signal_in_progress = true;
    handle_ir_signal(current_ir_pin);
    IrReceiver.resume();
    ir_signal_in_progress = false;
    last_ir_switch_ms = millis();
  }
  if (ir_enabled && !ir_signal_in_progress && (millis() - last_ir_switch_ms > IR_SWITCH_INTERVAL_MS)) {
    switch_ir_sensor();
  }

  if (fan_running && digitalRead(PIN_FAN_MOTOR_ENABLE) == LOW) {
    stepper.runSpeed();
  }
}
