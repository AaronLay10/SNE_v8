// Lever Boiler Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string).
//
// Devices (room-unique v8 device_ids):
// - lever_boiler_lever_boiler  (boiler lever station)
// - lever_boiler_lever_stairs  (stairs lever station)
// - lever_boiler_newell_post   (newell post light + stepper + prox sensors)

#include <Arduino.h>
#include <ArduinoJson.h>

// Suppress IRremote begin() error - receiver only.
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC keys, hex encoded (64 chars). One key per v8 device_id.
static const char *HMAC_LEVER_BOILER = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_LEVER_STAIRS = "0000000000000000000000000000000000000000000000000000000000000000";
static const char *HMAC_NEWELL_POST = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins (from v2)
static const int PIN_POWER_LED = 13;
static const int PIN_PHOTOCELL_BOILER = A1;
static const int PIN_PHOTOCELL_STAIRS = A0;
static const int PIN_IR_SENSOR_BOILER = 16;
static const int PIN_IR_SENSOR_STAIRS = 17;
static const int PIN_MAGLOCK_BOILER = 33;
static const int PIN_MAGLOCK_STAIRS = 34;
static const int PIN_LEVER_LED_BOILER = 20;
static const int PIN_LEVER_LED_STAIRS = 19;
static const int PIN_NEWELL_LIGHT = 36;
static const int PIN_NEWELL_PROX_UP = 39;
static const int PIN_NEWELL_PROX_DOWN = 38;

static const int PIN_STEPPER_1 = 1;
static const int PIN_STEPPER_2 = 2;
static const int PIN_STEPPER_3 = 3;
static const int PIN_STEPPER_4 = 4;

// Constants
static const int PHOTOCELL_THRESHOLD = 500;
static const unsigned long SENSOR_REFRESH_MS = 60UL * 1000UL;
static const unsigned long IR_SWITCH_INTERVAL_MS = 200;
static const int STEPPER_DELAY_US = 1000;

static const int STEP_SEQUENCE[4][4] = {
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1},
};

// Runtime state
static bool boiler_maglock_unlocked = false; // HIGH=lock, LOW=unlock
static bool stairs_maglock_unlocked = false;
static bool boiler_led_on = true;
static bool stairs_led_on = true;
static bool newell_light_on = false;

static int photocell_boiler_raw = 0;
static int photocell_stairs_raw = 0;
static bool boiler_valve_open = false;
static bool stairs_valve_open = false;

static bool prox_up = false;
static bool prox_down = false;

static bool last_boiler_valve_open = false;
static bool last_stairs_valve_open = false;
static bool last_prox_up = false;
static bool last_prox_down = false;
static bool last_boiler_maglock_unlocked = false;
static bool last_stairs_maglock_unlocked = false;
static bool last_boiler_led_on = true;
static bool last_stairs_led_on = true;
static bool last_newell_light_on = false;

static uint32_t last_ir_code_boiler = 0;
static uint32_t last_ir_raw_boiler = 0;
static uint32_t last_ir_code_stairs = 0;
static uint32_t last_ir_raw_stairs = 0;
static bool ir_seen_boiler = false;
static bool ir_seen_stairs = false;
static bool last_ir_seen_boiler = false;
static bool last_ir_seen_stairs = false;

// IR handling
static int current_ir_pin = PIN_IR_SENSOR_BOILER;
static unsigned long last_ir_switch_time = 0;
static bool ir_signal_in_progress = false;
static bool ir_enabled = true;

// Stepper
enum StepperDir : int8_t { DIR_STOP = 0, DIR_UP = -1, DIR_DOWN = 1 };
static StepperDir stepper_dir = DIR_STOP;
static bool stepper_moving = false;
static int step_index = 0;
static unsigned long last_step_time_us = 0;

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

static sentient_v8::Client sne_boiler(make_cfg("lever_boiler_lever_boiler", HMAC_LEVER_BOILER));
static sentient_v8::Client sne_stairs(make_cfg("lever_boiler_lever_stairs", HMAC_LEVER_STAIRS));
static sentient_v8::Client sne_newell(make_cfg("lever_boiler_newell_post", HMAC_NEWELL_POST));

enum class DeviceKind : uint8_t { Boiler, Stairs, Newell };
struct DeviceCtx {
  DeviceKind kind;
};
static DeviceCtx ctx_boiler = {DeviceKind::Boiler};
static DeviceCtx ctx_stairs = {DeviceKind::Stairs};
static DeviceCtx ctx_newell = {DeviceKind::Newell};

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

static void set_stepper_pins(int a, int b, int c, int d) {
  digitalWrite(PIN_STEPPER_1, a);
  digitalWrite(PIN_STEPPER_2, b);
  digitalWrite(PIN_STEPPER_3, c);
  digitalWrite(PIN_STEPPER_4, d);
}

static void stop_stepper() {
  stepper_dir = DIR_STOP;
  stepper_moving = false;
  set_stepper_pins(0, 0, 0, 0);
}

static void move_stepper_up() {
  if (digitalRead(PIN_NEWELL_PROX_UP) == HIGH) {
    stop_stepper();
    return;
  }
  stepper_dir = DIR_UP;
  stepper_moving = true;
}

static void move_stepper_down() {
  if (digitalRead(PIN_NEWELL_PROX_DOWN) == HIGH) {
    stop_stepper();
    return;
  }
  stepper_dir = DIR_DOWN;
  stepper_moving = true;
}

static void step_motor(int direction) {
  unsigned long now = micros();
  if ((long)(now - last_step_time_us) < STEPPER_DELAY_US) return;

  step_index += direction;
  if (step_index < 0) step_index = 3;
  if (step_index > 3) step_index = 0;

  set_stepper_pins(
      STEP_SEQUENCE[step_index][0],
      STEP_SEQUENCE[step_index][1],
      STEP_SEQUENCE[step_index][2],
      STEP_SEQUENCE[step_index][3]);

  last_step_time_us = now;
}

static void read_sensors() {
  photocell_boiler_raw = analogRead(PIN_PHOTOCELL_BOILER);
  photocell_stairs_raw = analogRead(PIN_PHOTOCELL_STAIRS);
  boiler_valve_open = (photocell_boiler_raw > PHOTOCELL_THRESHOLD);
  stairs_valve_open = (photocell_stairs_raw > PHOTOCELL_THRESHOLD);

  prox_up = (digitalRead(PIN_NEWELL_PROX_UP) == HIGH);
  prox_down = (digitalRead(PIN_NEWELL_PROX_DOWN) == HIGH);
}

static void publish_state_all() {
  {
    StaticJsonDocument<512> st;
    st["maglock_unlocked"] = boiler_maglock_unlocked;
    st["lever_led_on"] = boiler_led_on;
    st["valve_open"] = boiler_valve_open;
    st["photocell_raw"] = photocell_boiler_raw;
    if (ir_seen_boiler) {
      st["ir_code"] = (uint32_t)last_ir_code_boiler;
      st["ir_raw"] = (uint32_t)last_ir_raw_boiler;
    }
    sne_boiler.publishState(st);
  }
  {
    StaticJsonDocument<512> st;
    st["maglock_unlocked"] = stairs_maglock_unlocked;
    st["lever_led_on"] = stairs_led_on;
    st["valve_open"] = stairs_valve_open;
    st["photocell_raw"] = photocell_stairs_raw;
    if (ir_seen_stairs) {
      st["ir_code"] = (uint32_t)last_ir_code_stairs;
      st["ir_raw"] = (uint32_t)last_ir_raw_stairs;
    }
    sne_stairs.publishState(st);
  }
  {
    StaticJsonDocument<384> st;
    st["light_on"] = newell_light_on;
    st["stepper_moving"] = stepper_moving;
    st["dir"] = (stepper_dir == DIR_UP ? "up" : (stepper_dir == DIR_DOWN ? "down" : "stop"));
    st["prox_up"] = prox_up;
    st["prox_down"] = prox_down;
    sne_newell.publishState(st);
  }
}

static void maybe_publish(bool force = false) {
  static bool initialized = false;
  static unsigned long last_publish_ms = 0;
  unsigned long now = millis();

  bool changed = false;
  if (!initialized) changed = true;

  if (boiler_valve_open != last_boiler_valve_open) changed = true;
  if (stairs_valve_open != last_stairs_valve_open) changed = true;
  if (prox_up != last_prox_up) changed = true;
  if (prox_down != last_prox_down) changed = true;

  if (boiler_maglock_unlocked != last_boiler_maglock_unlocked) changed = true;
  if (stairs_maglock_unlocked != last_stairs_maglock_unlocked) changed = true;
  if (boiler_led_on != last_boiler_led_on) changed = true;
  if (stairs_led_on != last_stairs_led_on) changed = true;
  if (newell_light_on != last_newell_light_on) changed = true;

  if (ir_seen_boiler != last_ir_seen_boiler) changed = true;
  if (ir_seen_stairs != last_ir_seen_stairs) changed = true;

  if (!force && !changed && (now - last_publish_ms) < SENSOR_REFRESH_MS) return;

  last_publish_ms = now;
  initialized = true;

  last_boiler_valve_open = boiler_valve_open;
  last_stairs_valve_open = stairs_valve_open;
  last_prox_up = prox_up;
  last_prox_down = prox_down;

  last_boiler_maglock_unlocked = boiler_maglock_unlocked;
  last_stairs_maglock_unlocked = stairs_maglock_unlocked;
  last_boiler_led_on = boiler_led_on;
  last_stairs_led_on = stairs_led_on;
  last_newell_light_on = newell_light_on;

  last_ir_seen_boiler = ir_seen_boiler;
  last_ir_seen_stairs = ir_seen_stairs;

  publish_state_all();
}

static void publish_ir_event(sentient_v8::Client &client, uint32_t code, uint32_t raw) {
  StaticJsonDocument<192> ev;
  ev["code"] = (uint32_t)code;
  ev["raw"] = (uint32_t)raw;
  ev["ts_ms"] = millis();
  client.publishTelemetry(ev);
}

static void handle_ir_signal(int pin) {
  if (stepper_moving) return;

  bool is_noise = (IrReceiver.decodedIRData.command == 0 && IrReceiver.decodedIRData.address == 0 &&
                   IrReceiver.decodedIRData.decodedRawData == 0 && IrReceiver.decodedIRData.numberOfBits == 0);
  if (is_noise) return;

  uint32_t code = (uint32_t)IrReceiver.decodedIRData.command;
  uint32_t raw = (uint32_t)IrReceiver.decodedIRData.decodedRawData;

  if (pin == PIN_IR_SENSOR_BOILER) {
    ir_seen_boiler = true;
    last_ir_code_boiler = code;
    last_ir_raw_boiler = raw;
    publish_ir_event(sne_boiler, code, raw);
  } else {
    ir_seen_stairs = true;
    last_ir_code_stairs = code;
    last_ir_raw_stairs = raw;
    publish_ir_event(sne_stairs, code, raw);
  }

  digitalWrite(PIN_POWER_LED, LOW);
  delay(30);
  digitalWrite(PIN_POWER_LED, HIGH);

  maybe_publish(true);
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
    maybe_publish(true);
    return true;
  }
  if (strcmp(op, "noop") == 0) return true;

  if (ctx->kind == DeviceKind::Boiler) {
    if (strcmp(op, "maglock_unlock") == 0) {
      digitalWrite(PIN_MAGLOCK_BOILER, LOW);
      boiler_maglock_unlocked = true;
      maybe_publish(true);
      return true;
    }
    if (strcmp(op, "maglock_lock") == 0) {
      digitalWrite(PIN_MAGLOCK_BOILER, HIGH);
      boiler_maglock_unlocked = false;
      maybe_publish(true);
      return true;
    }
    if (strcmp(op, "led_on") == 0) {
      digitalWrite(PIN_LEVER_LED_BOILER, HIGH);
      boiler_led_on = true;
      maybe_publish(true);
      return true;
    }
    if (strcmp(op, "led_off") == 0) {
      digitalWrite(PIN_LEVER_LED_BOILER, LOW);
      boiler_led_on = false;
      maybe_publish(true);
      return true;
    }

    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (ctx->kind == DeviceKind::Stairs) {
    if (strcmp(op, "maglock_unlock") == 0) {
      digitalWrite(PIN_MAGLOCK_STAIRS, LOW);
      stairs_maglock_unlocked = true;
      maybe_publish(true);
      return true;
    }
    if (strcmp(op, "maglock_lock") == 0) {
      digitalWrite(PIN_MAGLOCK_STAIRS, HIGH);
      stairs_maglock_unlocked = false;
      maybe_publish(true);
      return true;
    }
    if (strcmp(op, "led_on") == 0) {
      digitalWrite(PIN_LEVER_LED_STAIRS, HIGH);
      stairs_led_on = true;
      maybe_publish(true);
      return true;
    }
    if (strcmp(op, "led_off") == 0) {
      digitalWrite(PIN_LEVER_LED_STAIRS, LOW);
      stairs_led_on = false;
      maybe_publish(true);
      return true;
    }

    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  // Newell post
  if (strcmp(op, "light_on") == 0) {
    newell_light_on = true;
    digitalWrite(PIN_NEWELL_LIGHT, HIGH);
    maybe_publish(true);
    return true;
  }
  if (strcmp(op, "light_off") == 0) {
    newell_light_on = false;
    digitalWrite(PIN_NEWELL_LIGHT, LOW);
    maybe_publish(true);
    return true;
  }
  if (strcmp(op, "stepper_up") == 0) {
    move_stepper_up();
    maybe_publish(true);
    return true;
  }
  if (strcmp(op, "stepper_down") == 0) {
    move_stepper_down();
    maybe_publish(true);
    return true;
  }
  if (strcmp(op, "stepper_stop") == 0) {
    stop_stepper();
    maybe_publish(true);
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

  pinMode(PIN_MAGLOCK_BOILER, OUTPUT);
  pinMode(PIN_MAGLOCK_STAIRS, OUTPUT);
  pinMode(PIN_LEVER_LED_BOILER, OUTPUT);
  pinMode(PIN_LEVER_LED_STAIRS, OUTPUT);
  pinMode(PIN_NEWELL_LIGHT, OUTPUT);

  pinMode(PIN_NEWELL_PROX_UP, INPUT_PULLDOWN);
  pinMode(PIN_NEWELL_PROX_DOWN, INPUT_PULLDOWN);

  pinMode(PIN_STEPPER_1, OUTPUT);
  pinMode(PIN_STEPPER_2, OUTPUT);
  pinMode(PIN_STEPPER_3, OUTPUT);
  pinMode(PIN_STEPPER_4, OUTPUT);

  // Initial states
  digitalWrite(PIN_MAGLOCK_BOILER, HIGH);
  digitalWrite(PIN_MAGLOCK_STAIRS, HIGH);
  digitalWrite(PIN_LEVER_LED_BOILER, HIGH);
  digitalWrite(PIN_LEVER_LED_STAIRS, HIGH);
  digitalWrite(PIN_NEWELL_LIGHT, LOW);
  set_stepper_pins(0, 0, 0, 0);

  // IR init
  IrReceiver.begin(current_ir_pin, DISABLE_LED_FEEDBACK, PIN_POWER_LED);
  last_ir_switch_time = millis();

  if (!sne_boiler.begin()) while (true) delay(1000);
  if (!sne_stairs.begin()) while (true) delay(1000);
  if (!sne_newell.begin()) while (true) delay(1000);

  sne_boiler.setCommandHandler(handleCommand, &ctx_boiler);
  sne_stairs.setCommandHandler(handleCommand, &ctx_stairs);
  sne_newell.setCommandHandler(handleCommand, &ctx_newell);

  read_sensors();
  maybe_publish(true);
}

void loop() {
  sne_boiler.loop();
  sne_stairs.loop();
  sne_newell.loop();

  if (ir_enabled && IrReceiver.decode()) {
    ir_signal_in_progress = true;
    handle_ir_signal(current_ir_pin);
    IrReceiver.resume();
    ir_signal_in_progress = false;
    last_ir_switch_time = millis();
  }
  if (ir_enabled && !ir_signal_in_progress && (millis() - last_ir_switch_time) > IR_SWITCH_INTERVAL_MS) {
    current_ir_pin = (current_ir_pin == PIN_IR_SENSOR_BOILER) ? PIN_IR_SENSOR_STAIRS : PIN_IR_SENSOR_BOILER;
    IrReceiver.begin(current_ir_pin, ENABLE_LED_FEEDBACK, PIN_POWER_LED);
    last_ir_switch_time = millis();
  }

  read_sensors();
  maybe_publish(false);

  if (stepper_moving) {
    if ((stepper_dir == DIR_UP && digitalRead(PIN_NEWELL_PROX_UP) == HIGH) ||
        (stepper_dir == DIR_DOWN && digitalRead(PIN_NEWELL_PROX_DOWN) == HIGH)) {
      stop_stepper();
      maybe_publish(true);
    } else {
      step_motor((int)stepper_dir);
    }
  }
}
