// ══════════════════════════════════════════════════════════════════════════════
// Boiler Room Subpanel Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Boiler Room Subpanel Controller
 *
 * Controlled Systems:
 * - Intro TV Lift Control and Power
 * - Fog Machine (power + trigger)
 * - IR Sensor for Gun Detection
 * - Barrel Maglock
 * - Study Door Maglocks (3x)
 * - Gauge Progress LEDs (60 LEDs)
 * - Ultrasonic Water Fogger
 *
 * STATELESS ARCHITECTURE:
 * - IR sensor REPORTS gun codes detected (does NOT unlock barrel)
 * - Sentient decides all actions (barrel unlock, door unlock, etc.)
 * - No autonomous behavior - pure input/output controller
 */

#define USE_NO_SEND_PWM
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 512
#endif

#include <SentientCapabilityManifest.h>
#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <IRremote.hpp>

#include "FirmwareMetadata.h"
#include "controller_naming.h"

// ──────────────────────────────────────────────────────────────────────────────
// Device Definitions
// ──────────────────────────────────────────────────────────────────────────────
// *** TV Lift System - Motorized TV lift with up/down control + power relay
// *** Fog Machine - Power relay (warmup) + trigger relay (burst)
// *** Ultrasonic Water Fogger - Creates water mist effect
// *** Barrel Maglock - Electromagnetic lock for barrel drop puzzle
// *** Study Door Maglocks - 3 separate maglocks (top, bottom A, bottom B)
// *** Gauge Progress LEDs - 60 LED strip showing gauge puzzle progress
// *** IR Sensor - Infrared receiver detecting gun IDs

// ──────────────────────────────────────────────────────────────────────────────
// Pin Assignments
// ──────────────────────────────────────────────────────────────────────────────
const int power_led_pin = 13;

// TV Lift System
const int tv_power_pin = 32;
const int tv_lift_up_pin = 2;
const int tv_lift_down_pin = 3;

// Fog Machine
const int fog_power_pin = 38;
const int fog_trigger_pin = 33;
const int ultrasonic_water_pin = 34;

// Maglocks
const int barrel_maglock_pin = 36;
const int study_door_maglock_top_pin = 29;
const int study_door_maglock_bottom_a_pin = 30;
const int study_door_maglock_bottom_b_pin = 31;

// Sensors
const int ir_sensor_pin = 35;

// LEDs
const int gauge_lights_pin = 27;
const int num_leds = 60;

CRGB leds[num_leds];

// ============================================================================
// DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!) — Updated to canonical device IDs
// ============================================================================

// Intro TV Power
const char *intro_tv_power_commands[] = {
    naming::CMD_TV_POWER_ON,
    naming::CMD_TV_POWER_OFF};

// Intro TV Lift
const char *intro_tv_lift_commands[] = {
    naming::CMD_TV_LIFT_UP,
    naming::CMD_TV_LIFT_DOWN,
    naming::CMD_TV_LIFT_STOP};

// Fog Machine Power
const char *fog_power_commands[] = {
    naming::CMD_FOG_POWER_ON,
    naming::CMD_FOG_POWER_OFF};

// Fog Machine Trigger
const char *fog_trigger_commands[] = {
    naming::CMD_FOG_TRIGGER};

// Fog Machine Ultrasonic
const char *fog_ultrasonic_commands[] = {
    naming::CMD_ULTRASONIC_ON,
    naming::CMD_ULTRASONIC_OFF};

// Barrel — maglock only
const char *barrel_commands[] = {
    naming::CMD_BARREL_LOCK,
    naming::CMD_BARREL_UNLOCK};

// Study Door — group control
const char *study_door_commands[] = {
    naming::CMD_DOOR_LOCK,
    naming::CMD_DOOR_UNLOCK};

// Gauge Progress Chest — discrete progress levels
const char *gauge_chest_commands[] = {
    naming::CMD_GAUGE_SOLVED_1,
    naming::CMD_GAUGE_SOLVED_2,
    naming::CMD_GAUGE_SOLVED_3,
    naming::CMD_GAUGE_CLEAR};

// IR Sensor — gun detection control
const char *ir_sensor_commands[] = {
    naming::CMD_IR_ACTIVATE,
    naming::CMD_IR_DEACTIVATE};

// Sensor items
const char *ir_sensor_sensors[] = {"ir_code"};

// Create device definitions with friendly names
SentientDeviceDef dev_intro_tv_power(
    naming::DEV_INTRO_TV_POWER, naming::FRIENDLY_INTRO_TV_POWER, naming::TYPE_INTRO_TV_POWER,
    intro_tv_power_commands, 2);

SentientDeviceDef dev_intro_tv_lift(
    naming::DEV_INTRO_TV_LIFT, naming::FRIENDLY_INTRO_TV_LIFT, naming::TYPE_INTRO_TV_LIFT,
    intro_tv_lift_commands, 3);

SentientDeviceDef dev_fog_power(
    naming::DEV_FOG_POWER, naming::FRIENDLY_FOG_POWER, naming::TYPE_FOG_POWER,
    fog_power_commands, 2);

SentientDeviceDef dev_fog_trigger(
    naming::DEV_FOG_TRIGGER, naming::FRIENDLY_FOG_TRIGGER, naming::TYPE_FOG_TRIGGER,
    fog_trigger_commands, 1);

SentientDeviceDef dev_fog_ultrasonic(
    naming::DEV_FOG_ULTRASONIC, naming::FRIENDLY_FOG_ULTRASONIC, naming::TYPE_FOG_ULTRASONIC,
    fog_ultrasonic_commands, 2);

SentientDeviceDef dev_barrel(
    naming::DEV_BOILER_ROOM_BARREL, naming::FRIENDLY_BOILER_ROOM_BARREL, naming::TYPE_BOILER_ROOM_BARREL,
    barrel_commands, 2);

SentientDeviceDef dev_ir_sensor(
    naming::DEV_IR_SENSOR, naming::FRIENDLY_IR_SENSOR, naming::TYPE_IR_SENSOR,
    ir_sensor_commands, 2,
    ir_sensor_sensors, 1);

SentientDeviceDef dev_study_door(
    naming::DEV_STUDY_DOOR, naming::FRIENDLY_STUDY_DOOR, naming::TYPE_STUDY_DOOR,
    study_door_commands, 2);

SentientDeviceDef dev_gauge_chest(
    naming::DEV_GAUGE_PROGRESS_CHEST, naming::FRIENDLY_GAUGE_PROGRESS_CHEST, naming::TYPE_GAUGE_PROGRESS_CHEST,
    gauge_chest_commands, 4);

// Create the device registry (manifest builder will use these IDs and names)
SentientDeviceRegistry deviceRegistry;

// ──────────────────────────────────────────────────────────────────────────────
// Configuration Constants
// ──────────────────────────────────────────────────────────────────────────────
const unsigned long heartbeat_interval_ms = 5000;
const uint32_t expected_gun_id = 0x51; // Gun ID that should trigger barrel unlock
// JSON capacity for metadata/status publishes
static const size_t metadata_json_capacity = 1024;

// ============================================================================
// MQTT CONFIGURATION
// ============================================================================
const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_user = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
const char *mqtt_namespace = naming::CLIENT_ID;
const char *room_id = naming::ROOM_ID;
// Single source of truth: controller_id comes from firmware::UNIQUE_ID
const char *controller_id = naming::CONTROLLER_ID;
const char *controller_model = "controller"; // pseudo-device for generic status
const char *controller_friendly_name = naming::CONTROLLER_FRIENDLY_NAME;

// ──────────────────────────────────────────────────────────────────────────────
// Hardware State Variables (NOT game state - just hardware execution flags)
// ──────────────────────────────────────────────────────────────────────────────
bool tv_power_on = false;
int tv_lift_state = 0; // 0=stopped, 1=up, -1=down
bool fog_power_on = false;
bool fog_trigger_on = false;
bool ultrasonic_water_on = false;
bool barrel_maglock_locked = true;
bool study_door_top_locked = true;
bool study_door_bottom_a_locked = false;
bool study_door_bottom_b_locked = false;
int gauge_progress_level = 0; // 0-3
bool ir_sensor_active = true;

// IR Sensor tracking for duplicate detection
uint32_t last_ir_raw_data = 0;
unsigned long last_ir_timestamp = 0;

// ──────────────────────────────────────────────────────────────────────────────
// Forward Declarations (required for MQTT initialization)
// ──────────────────────────────────────────────────────────────────────────────
void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);

// ──────────────────────────────────────────────────────────────────────────────
// MQTT Objects
// ──────────────────────────────────────────────────────────────────────────────
SentientCapabilityManifest manifest;
SentientMQTT mqtt(build_mqtt_config());

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 2: SETUP FUNCTION
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println(F(""));
  Serial.println(F("╔════════════════════════════════════════════════════════════╗"));
  Serial.println(F("║       Sentient Engine - BoilerRmA Controller          ║"));
  Serial.println(F("╚════════════════════════════════════════════════════════════╝"));
  Serial.print(F("[BoilerRmA] Firmware Version: "));
  Serial.println(firmware::VERSION);
  Serial.print(F("[BoilerRmA] Unique ID: "));
  Serial.println(firmware::UNIQUE_ID);

  // Configure power LED
  pinMode(power_led_pin, OUTPUT);
  digitalWrite(power_led_pin, HIGH);

  // Configure TV system
  pinMode(tv_power_pin, OUTPUT);
  pinMode(tv_lift_up_pin, OUTPUT);
  pinMode(tv_lift_down_pin, OUTPUT);
  digitalWrite(tv_power_pin, LOW);
  digitalWrite(tv_lift_up_pin, LOW);
  digitalWrite(tv_lift_down_pin, LOW);

  // Configure fog machine
  pinMode(fog_power_pin, OUTPUT);
  pinMode(fog_trigger_pin, OUTPUT);
  pinMode(ultrasonic_water_pin, OUTPUT);
  digitalWrite(fog_power_pin, LOW);
  digitalWrite(fog_trigger_pin, LOW);
  digitalWrite(ultrasonic_water_pin, LOW);

  // Configure maglocks (HIGH = locked, LOW = unlocked)
  pinMode(barrel_maglock_pin, OUTPUT);
  pinMode(study_door_maglock_top_pin, OUTPUT);
  pinMode(study_door_maglock_bottom_a_pin, OUTPUT);
  pinMode(study_door_maglock_bottom_b_pin, OUTPUT);
  digitalWrite(barrel_maglock_pin, HIGH);             // Locked
  digitalWrite(study_door_maglock_top_pin, HIGH);     // Locked
  digitalWrite(study_door_maglock_bottom_a_pin, LOW); // Unlocked
  digitalWrite(study_door_maglock_bottom_b_pin, LOW); // Unlocked

  // Configure LED strip
  FastLED.addLeds<WS2812B, gauge_lights_pin, GRB>(leds, num_leds);
  FastLED.setBrightness(255);
  fill_solid(leds, num_leds, CRGB::Black);
  FastLED.show();

  // Configure IR sensor
  IrReceiver.begin(ir_sensor_pin, false);
  Serial.println(F("[BoilerRmA] IR sensor initialized"));

  Serial.println(F("[BoilerRmA] Hardware initialized"));

  // Register all devices (SINGLE SOURCE OF TRUTH!) — canonical IDs + friendly names
  Serial.println(F("[BoilerRmA] Registering devices (canonical)..."));
  deviceRegistry.addDevice(&dev_intro_tv_power);
  deviceRegistry.addDevice(&dev_intro_tv_lift);
  deviceRegistry.addDevice(&dev_fog_power);
  deviceRegistry.addDevice(&dev_fog_trigger);
  deviceRegistry.addDevice(&dev_fog_ultrasonic);
  deviceRegistry.addDevice(&dev_barrel);
  deviceRegistry.addDevice(&dev_ir_sensor);
  deviceRegistry.addDevice(&dev_study_door);
  deviceRegistry.addDevice(&dev_gauge_chest);
  deviceRegistry.printSummary();

  // Build capability manifest
  build_capability_manifest();

  // Initialize MQTT
  Serial.println(F("[BoilerRmA] Initializing MQTT..."));
  if (!mqtt.begin())
  {
    Serial.println(F("[BoilerRmA] ERROR: MQTT initialization failed"));
    return;
  }

  Serial.println(F("[BoilerRmA] MQTT connected successfully!"));

  // Set callbacks
  // We override command routing to parse device + command from topic
  mqtt.setHeartbeatBuilder(build_heartbeat_payload);

  // Wait for broker connection (max 5 seconds)
  Serial.println(F("[BoilerRmA] Waiting for broker connection..."));
  unsigned long connection_start = millis();
  while (!mqtt.isConnected() && (millis() - connection_start < 5000))
  {
    mqtt.loop();
    delay(100);
  }

  if (mqtt.isConnected())
  {
    Serial.println(F("[BoilerRmA] Broker connected!"));

    // Register with Sentient system
    Serial.println(F("[BoilerRmA] Registering with Sentient system..."));
    if (manifest.publish_registration(mqtt.get_client(), room_id, controller_id))
    {
      Serial.println(F("[BoilerRmA] Registration successful!"));
    }
    else
    {
      Serial.println(F("[BoilerRmA] WARNING: Registration publish failed"));
    }

    // Publish initial status
    publish_hardware_status();
  }
  else
  {
    Serial.println(F("[BoilerRmA] Broker connection timeout - will retry in main loop"));
  }

  // Subscribe to device-scoped commands and set our own message handler
  {
    String topic = String(mqtt_namespace) + "/" + room_id + "/" + naming::CAT_COMMANDS + "/" + controller_id + "/+/+";
    mqtt.get_client().subscribe(topic.c_str());
    Serial.print(F("[BoilerRmA] Subscribed (device+command): "));
    Serial.println(topic);
    mqtt.get_client().setCallback([](char *topic, uint8_t *payload, unsigned int length)
                                  {
  // Parse topic → device + command
      String device, command;
      // Extract segments (expect: client/room/commands/controller/device/command)
  int seg = 0; const char *p = topic;
      const char *segStart[8] = {nullptr}; size_t segLen[8] = {0};
      segStart[0] = p;
      while (*p && seg < 8) { if (*p == '/') { segLen[seg] = p - segStart[seg]; seg++; if (seg < 8) segStart[seg] = p + 1; } p++; }
      if (seg < 8 && segStart[seg]) { segLen[seg] = p - segStart[seg]; seg++; }
      auto equalsSeg = [&](int idx, const char *lit) -> bool {
        size_t litLen = strlen(lit); return segLen[idx] == litLen && strncmp(segStart[idx], lit, litLen) == 0; };
      if (seg >= 6 && equalsSeg(0, mqtt_namespace) && equalsSeg(1, room_id) && equalsSeg(2, naming::CAT_COMMANDS) && equalsSeg(3, controller_id)) {
        device = String(segStart[4]).substring(0, segLen[4]);
        command = String(segStart[5]).substring(0, segLen[5]);
        // Serial observability for command receipt
        Serial.print(F("[BoilerRmA] CMD recv: device="));
        Serial.print(device);
        Serial.print(F(" command="));
        Serial.print(command);
        Serial.print(F(" payload_len="));
        Serial.println(length);
      } else {
        return; // Not for us
      }

      // Parse payload (JSON or raw)
      DynamicJsonDocument doc(256);
      bool hasJson = false;
      if (length > 0) {
        DeserializationError err = deserializeJson(doc, payload, length);
        hasJson = !err;
        if (err) {
          // Note: payload not valid JSON, treat as raw string
          Serial.println(F("[BoilerRmA] Payload parse: non-JSON, treating as raw"));
          doc.clear(); doc["value"] = String((const char *)payload).substring(0, length);
        }
      }

      long ack_duration_ms = -1; // will be included in ACK if >= 0

      // Dispatch
      // Intro TV Power
      if (device == naming::DEV_INTRO_TV_POWER) {
        if (command == naming::CMD_TV_POWER_ON) {
          tv_power_on = true;
          digitalWrite(tv_power_pin, HIGH);
          publish_hardware_status();
        }
        else if (command == naming::CMD_TV_POWER_OFF) {
          tv_power_on = false;
          digitalWrite(tv_power_pin, LOW);
          publish_hardware_status();
        }
      }
      // Intro TV Lift
      else if (device == naming::DEV_INTRO_TV_LIFT) {
        if (command == naming::CMD_TV_LIFT_UP) {
          tv_lift_state = 1;
          digitalWrite(tv_lift_up_pin, HIGH);
          digitalWrite(tv_lift_down_pin, LOW);
          publish_hardware_status();
        }
        else if (command == naming::CMD_TV_LIFT_DOWN) {
          tv_lift_state = -1;
          digitalWrite(tv_lift_down_pin, HIGH);
          digitalWrite(tv_lift_up_pin, LOW);
          publish_hardware_status();
        }
        else if (command == naming::CMD_TV_LIFT_STOP) {
          tv_lift_state = 0;
          digitalWrite(tv_lift_up_pin, LOW);
          digitalWrite(tv_lift_down_pin, LOW);
          publish_hardware_status();
        }
      }
      // Controller-level commands (direct to controller, not a device)
      else if (device == "controller") {
        if (command == naming::CMD_CONTROLLER_POWER_OFF_SEQUENCE) {
          // Perform power-off sequence: set outputs to safe/off state
          // TV
          tv_power_on = false; digitalWrite(tv_power_pin, LOW);
          tv_lift_state = 0; digitalWrite(tv_lift_up_pin, LOW); digitalWrite(tv_lift_down_pin, LOW);
          // Fog system
          fog_trigger_on = false; digitalWrite(fog_trigger_pin, LOW);
          ultrasonic_water_on = false; digitalWrite(ultrasonic_water_pin, LOW);
          fog_power_on = false; digitalWrite(fog_power_pin, LOW);
          // Doors/locks to safe default (locked)
          barrel_maglock_locked = true; digitalWrite(barrel_maglock_pin, HIGH);
          study_door_top_locked = true; study_door_bottom_a_locked = true; study_door_bottom_b_locked = true;
          digitalWrite(study_door_maglock_top_pin, HIGH);
          digitalWrite(study_door_maglock_bottom_a_pin, HIGH);
          digitalWrite(study_door_maglock_bottom_b_pin, HIGH);
          // Lights off
          gauge_progress_level = 0; fill_solid(leds, num_leds, CRGB::Black); FastLED.show();
          // IR off
          ir_sensor_active = false;
          // Publish status and readiness
          publish_hardware_status();
          // Publish shutdown readiness: [client]/[room]/events/[controller]/controller/shutdown_ready
          StaticJsonDocument<160> ready;
          ready["controller_id"] = controller_id;
          ready["ready"] = true;
          ready["version"] = firmware::VERSION;
          ready["timestamp_ms"] = millis();
          char rbuf[196]; serializeJson(ready, rbuf, sizeof(rbuf));
          String rTopic = String(mqtt_namespace) + "/" + room_id + "/" + naming::CAT_EVENTS + "/" + controller_id + "/shutdown_ready";
          mqtt.get_client().publish(rTopic.c_str(), rbuf, false);
        }
      }
      // Fog Machine Power
      else if (device == naming::DEV_FOG_POWER) {
        if (command == naming::CMD_FOG_POWER_ON) {
          fog_power_on = true;
          digitalWrite(fog_power_pin, HIGH);
          publish_hardware_status();
        }
        else if (command == naming::CMD_FOG_POWER_OFF) {
          fog_power_on = false;
          digitalWrite(fog_power_pin, LOW);
          publish_hardware_status();
        }
      }
      // Fog Machine Trigger
      else if (device == naming::DEV_FOG_TRIGGER) {
        if (command == naming::CMD_FOG_TRIGGER) {
          // Allow optional duration_ms (default 500ms), clamped to [100, 3000]
          long durationMs = 500;
          if (hasJson) {
            if (!doc["duration_ms"].isNull()) {
              durationMs = doc["duration_ms"].as<long>();
            } else if (!doc["duration"].isNull()) {
              durationMs = doc["duration"].as<long>();
            } else if (!doc["value"].isNull()) {
              // Accept numeric or numeric string in "value"
              if (doc["value"].is<long>()) durationMs = doc["value"].as<long>();
              else {
                String vs = doc["value"].as<String>();
                durationMs = vs.toInt();
              }
            }
          }
          if (durationMs < 100) durationMs = 100;
          if (durationMs > 3000) durationMs = 3000;
          ack_duration_ms = durationMs;
          Serial.print(F("[BoilerRmA] Fog trigger pulse: ")); Serial.print(durationMs); Serial.println(F(" ms"));
          fog_trigger_on = true;
          digitalWrite(fog_trigger_pin, HIGH);
          delay((unsigned long)durationMs);
          fog_trigger_on = false;
          digitalWrite(fog_trigger_pin, LOW);
          publish_hardware_status();
        }
      }
      // Fog Machine Ultrasonic
      else if (device == naming::DEV_FOG_ULTRASONIC) {
        if (command == naming::CMD_ULTRASONIC_ON) {
          ultrasonic_water_on = true;
          digitalWrite(ultrasonic_water_pin, HIGH);
          publish_hardware_status();
        }
        else if (command == naming::CMD_ULTRASONIC_OFF) {
          ultrasonic_water_on = false;
          digitalWrite(ultrasonic_water_pin, LOW);
          publish_hardware_status();
        }
      }
      // Study Door (group)
      else if (device == naming::DEV_STUDY_DOOR) {
        if (command == naming::CMD_DOOR_LOCK)   { study_door_top_locked = true; study_door_bottom_a_locked = true; study_door_bottom_b_locked = true; digitalWrite(study_door_maglock_top_pin, HIGH); digitalWrite(study_door_maglock_bottom_a_pin, HIGH); digitalWrite(study_door_maglock_bottom_b_pin, HIGH); publish_hardware_status(); }
        else if (command == naming::CMD_DOOR_UNLOCK) { study_door_top_locked = false; study_door_bottom_a_locked = false; study_door_bottom_b_locked = false; digitalWrite(study_door_maglock_top_pin, LOW); digitalWrite(study_door_maglock_bottom_a_pin, LOW); digitalWrite(study_door_maglock_bottom_b_pin, LOW); publish_hardware_status(); }
        else { /* unknown */ }
      }
      // Gauge Progress Chest
      else if (device == naming::DEV_GAUGE_PROGRESS_CHEST) {
        if (command == naming::CMD_GAUGE_CLEAR) { gauge_progress_level = 0; fill_solid(leds, num_leds, CRGB::Black); }
        else if (command == naming::CMD_GAUGE_SOLVED_1) { gauge_progress_level = 1; fill_solid(leds, 15, CRGB::Green); fill_solid(leds + 15, num_leds - 15, CRGB::Black); }
        else if (command == naming::CMD_GAUGE_SOLVED_2) { gauge_progress_level = 2; fill_solid(leds, 30, CRGB::Green); fill_solid(leds + 30, num_leds - 30, CRGB::Black); }
        else if (command == naming::CMD_GAUGE_SOLVED_3) { gauge_progress_level = 3; fill_solid(leds, 45, CRGB::Green); fill_solid(leds + 45, num_leds - 45, CRGB::Black); }
        FastLED.show(); publish_hardware_status();
      }
      // Barrel (maglock only)
      else if (device == naming::DEV_BOILER_ROOM_BARREL) {
        if (command == naming::CMD_BARREL_UNLOCK) { barrel_maglock_locked = false; digitalWrite(barrel_maglock_pin, LOW); publish_hardware_status(); }
        else if (command == naming::CMD_BARREL_LOCK) { barrel_maglock_locked = true; digitalWrite(barrel_maglock_pin, HIGH); publish_hardware_status(); }
        else { /* unknown */ }
      }
      // IR Sensor (gun detection control)
      else if (device == naming::DEV_IR_SENSOR) {
        if (command == naming::CMD_IR_ACTIVATE) { ir_sensor_active = true; publish_hardware_status(); }
        else if (command == naming::CMD_IR_DEACTIVATE) { ir_sensor_active = false; publish_hardware_status(); }
        else { /* unknown */ }
      }

      // Publish command acknowledgement to dedicated acknowledgement topic
      StaticJsonDocument<160> ack;
      ack["controller_id"] = controller_id;
      ack["device_id"] = device;
      ack["command"] = command;
      ack["success"] = true;
      ack["timestamp_ms"] = millis();
      if (ack_duration_ms >= 0) { ack["duration_ms"] = (long)ack_duration_ms; }
      char buf[196]; serializeJson(ack, buf, sizeof(buf));
      // Topic: <tenant>/<room>/acknowledgement/<controller>/<device>/<command>
      String ackTopic = String(mqtt_namespace) + "/" + room_id + "/" + naming::CAT_ACKNOWLEDGEMENT + "/" + controller_id + "/" + device + "/" + command;
      mqtt.get_client().publish(ackTopic.c_str(), buf, false);
      Serial.print(F("[BoilerRmA] ACK -> "));
      Serial.println(ackTopic); });
  }

  Serial.println(F("[BoilerRmA] Ready - awaiting Sentient commands"));
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 3: LOOP FUNCTION
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
  // 1. LISTEN for commands from Sentient
  mqtt.loop();

  // 2. DETECT IR sensor input and publish if detected
  check_ir_sensor();

  // 3. EXECUTE LED updates
  FastLED.show();

  delay(10);
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 4: (Legacy handler removed) — All commands are routed via topic-based
//            device+command parsing inside the MQTT callback set in setup().
// ══════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────────────
// Hardware Execution Functions (called by command handler)
// ──────────────────────────────────────────────────────────────────────────────

void check_ir_sensor()
{
  if (!ir_sensor_active || !IrReceiver.decode())
  {
    return;
  }

  // Duplicate detection
  bool is_duplicate = (IrReceiver.decodedIRData.decodedRawData == last_ir_raw_data &&
                       (millis() - last_ir_timestamp) < 500);

  // Filter weak signals
  bool is_weak_signal = (IrReceiver.decodedIRData.protocol == UNKNOWN ||
                         IrReceiver.decodedIRData.protocol == 2);

  if (!is_duplicate && !is_weak_signal)
  {
    uint32_t gun_code = IrReceiver.decodedIRData.command;

    Serial.print(F("[BoilerRmA] IR Code detected: 0x"));
    Serial.println(gun_code, HEX);

    // Publish IR code to Sentient on Barrel device sensors
    JsonDocument doc;
    doc["gun_code"] = gun_code;
    doc["expected_code"] = expected_gun_id;
    doc["is_correct"] = (gun_code == expected_gun_id);
    doc["ts"] = millis();

    // Build topic: [client]/[room]/sensors/[controller]/[device]/ir_code
    String topic = String(mqtt_namespace) + "/" + room_id + "/" + naming::CAT_SENSORS + "/" + controller_id + "/" + naming::DEV_BOILER_ROOM_BARREL + "/ir_code";
    String payload;
    serializeJson(doc, payload);
    mqtt.get_client().publish(topic.c_str(), payload.c_str(), false);
  }

  // Update tracking
  last_ir_raw_data = IrReceiver.decodedIRData.decodedRawData;
  last_ir_timestamp = millis();
  IrReceiver.resume();
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 5: ALL OTHER FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────────────
// Capability Manifest Builder
// ──────────────────────────────────────────────────────────────────────────────

void build_capability_manifest()
{
  manifest.set_controller_info(
      firmware::UNIQUE_ID,
      controller_friendly_name,
      firmware::VERSION,
      room_id,
      controller_id);

  // Auto-generate entire manifest from device registry - ONE LINE!
  deviceRegistry.buildManifest(manifest);

  // That's it! No manual device/topic registration needed!
  // All devices and topics are defined once in the Device Registry section above.
}

// ──────────────────────────────────────────────────────────────────────────────
// MQTT Configuration Builder
// ──────────────────────────────────────────────────────────────────────────────

SentientMQTTConfig build_mqtt_config()
{
  SentientMQTTConfig cfg{};
  if (mqtt_host && mqtt_host[0] != '\0')
  {
    cfg.brokerHost = mqtt_host;
  }
  cfg.brokerIp = mqtt_broker_ip;
  cfg.brokerPort = mqtt_port;
  cfg.username = mqtt_user;
  cfg.password = mqtt_password;
  cfg.namespaceId = mqtt_namespace;
  cfg.roomId = room_id;
  cfg.controllerId = controller_id;
  cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
  cfg.displayName = controller_friendly_name;
  cfg.hostnamePrefix = "CL"; // Clockwork room prefix
  cfg.useDhcp = true;
  cfg.heartbeatIntervalMs = heartbeat_interval_ms;
  cfg.publishJsonCapacity = metadata_json_capacity;
  cfg.autoHeartbeat = true;
  return cfg;
}

// ──────────────────────────────────────────────────────────────────────────────
// Heartbeat Builder
// ──────────────────────────────────────────────────────────────────────────────

bool build_heartbeat_payload(JsonDocument &doc, void *ctx)
{
  doc["uid"] = firmware::UNIQUE_ID;
  doc["fw"] = firmware::VERSION;
  doc["up"] = millis();
  return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Status Publishing
// ──────────────────────────────────────────────────────────────────────────────

void publish_hardware_status()
{
  JsonDocument doc;
  doc["uid"] = firmware::UNIQUE_ID;
  doc["tv_power"] = tv_power_on;
  doc["tv_lift"] = tv_lift_state;
  doc["fog_power"] = fog_power_on;
  doc["fog_trigger"] = fog_trigger_on;
  doc["ultrasonic_water"] = ultrasonic_water_on;
  doc["barrel_maglock_locked"] = barrel_maglock_locked;
  doc["study_top_locked"] = study_door_top_locked;
  doc["study_bottom_a_locked"] = study_door_bottom_a_locked;
  doc["study_bottom_b_locked"] = study_door_bottom_b_locked;
  doc["gauge_progress"] = gauge_progress_level;
  doc["ir_active"] = ir_sensor_active;
  doc["ts"] = millis();

  mqtt.publishJson("status", "hardware", doc);
}

// ──────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ──────────────────────────────────────────────────────────────────────────────

bool parse_truth(const String &value)
{
  if (value.equalsIgnoreCase("on") || value.equalsIgnoreCase("true") || value.equalsIgnoreCase("1"))
  {
    return true;
  }
  return false;
}

String extract_command_value(const JsonDocument &payload)
{
  if (payload.is<JsonObjectConst>())
  {
    auto obj = payload.as<JsonObjectConst>();
    if (!obj["value"].isNull())
      return obj["value"].as<String>();
    if (!obj["state"].isNull())
      return obj["state"].as<String>();
  }
  else if (payload.is<const char *>())
  {
    return payload.as<const char *>();
  }
  else if (payload.is<int>())
  {
    return String(payload.as<int>());
  }
  return String();
}
