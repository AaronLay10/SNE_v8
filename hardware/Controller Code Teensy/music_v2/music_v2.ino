// ══════════════════════════════════════════════════════════════════════════════
// Music Puzzle Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/**
 * Music Puzzle Controller
 *
 * HARDWARE:
 * - 6x Button switches with pull-up resistors (active LOW)
 *
 * STATELESS ARCHITECTURE:
 * - Publishes button state changes on press/release
 * - No game logic - Sentient makes all decisions
 * - Change-based publishing reduces MQTT traffic
 *
 * AUTHOR: Sentient Engine Team
 * TARGET: Teensy 4.1
 * ============================================================================
 */

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include "controller_naming.h"
#include "FirmwareMetadata.h"

using namespace naming;

// ============================================================================
// MQTT CONFIGURATION
// ============================================================================

const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_user = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
const unsigned long heartbeat_interval_ms = 5000; // 5 seconds

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

const int powerLED = 13;

// Button pins (INPUT_PULLUP, active LOW)
const int button1_pin = 0;
const int button2_pin = 1;
const int button3_pin = 2;
const int button4_pin = 3;
const int button5_pin = 4;
const int button6_pin = 5;

// ============================================================================
// BUTTON STATE TRACKING
// ============================================================================

// Current button states
bool button1_state = false;
bool button2_state = false;
bool button3_state = false;
bool button4_state = false;
bool button5_state = false;
bool button6_state = false;

// Last published states (for change detection)
bool last_button1 = false;
bool last_button2 = false;
bool last_button3 = false;
bool last_button4 = false;
bool last_button5 = false;
bool last_button6 = false;

// Debounce timing
unsigned long last_debounce_time1 = 0;
unsigned long last_debounce_time2 = 0;
unsigned long last_debounce_time3 = 0;
unsigned long last_debounce_time4 = 0;
unsigned long last_debounce_time5 = 0;
unsigned long last_debounce_time6 = 0;
const unsigned long debounce_delay = 50; // 50ms

bool sensors_initialized = false;

// Periodic publishing
unsigned long last_sensor_publish_time = 0;
const unsigned long sensor_publish_interval = 60000; // 60 seconds

// ============================================================================
// DEVICE REGISTRY
// ============================================================================

// Device definitions for 6 music buttons
const char *button1_sensors[] = {SENSOR_BUTTON_1_PRESSED};
const char *button2_sensors[] = {SENSOR_BUTTON_2_PRESSED};
const char *button3_sensors[] = {SENSOR_BUTTON_3_PRESSED};
const char *button4_sensors[] = {SENSOR_BUTTON_4_PRESSED};
const char *button5_sensors[] = {SENSOR_BUTTON_5_PRESSED};
const char *button6_sensors[] = {SENSOR_BUTTON_6_PRESSED};

SentientDeviceDef dev_button1(
    DEV_BUTTON_1,
    FRIENDLY_BUTTON_1,
    "button_sensor",
    nullptr, 0,
    button1_sensors, 1);

SentientDeviceDef dev_button2(
    DEV_BUTTON_2,
    FRIENDLY_BUTTON_2,
    "button_sensor",
    nullptr, 0,
    button2_sensors, 1);

SentientDeviceDef dev_button3(
    DEV_BUTTON_3,
    FRIENDLY_BUTTON_3,
    "button_sensor",
    nullptr, 0,
    button3_sensors, 1);

SentientDeviceDef dev_button4(
    DEV_BUTTON_4,
    FRIENDLY_BUTTON_4,
    "button_sensor",
    nullptr, 0,
    button4_sensors, 1);

SentientDeviceDef dev_button5(
    DEV_BUTTON_5,
    FRIENDLY_BUTTON_5,
    "button_sensor",
    nullptr, 0,
    button5_sensors, 1);

SentientDeviceDef dev_button6(
    DEV_BUTTON_6,
    FRIENDLY_BUTTON_6,
    "button_sensor",
    nullptr, 0,
    button6_sensors, 1);

SentientDeviceRegistry deviceRegistry;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);
void read_buttons();
void publish_sensor_changes(bool force_publish);

// ============================================================================
// MQTT OBJECTS
// ============================================================================

SentientCapabilityManifest manifest;

// Registration retry state
bool registration_sent = false;
unsigned long last_registration_attempt_ms = 0;
const unsigned long registration_retry_interval_ms = 10000; // 10 seconds

// ============================================================================
// MQTT CONFIGURATION BUILDER
// ============================================================================

SentientMQTTConfig build_mqtt_config()
{
  SentientMQTTConfig cfg;
  cfg.brokerHost = mqtt_host;
  cfg.brokerPort = mqtt_port;
  cfg.username = mqtt_user;
  cfg.password = mqtt_password;
  cfg.namespaceId = CLIENT_ID;
  cfg.roomId = ROOM_ID;
  cfg.controllerId = CONTROLLER_ID;
  cfg.deviceId = nullptr;
  cfg.displayName = CONTROLLER_FRIENDLY_NAME;
  cfg.useDhcp = true;
  cfg.autoHeartbeat = true;
  cfg.heartbeatIntervalMs = heartbeat_interval_ms;
  return cfg;
}

SentientMQTT mqtt(build_mqtt_config());

// ============================================================================
// SETUP
// ============================================================================

void setup()
{
  Serial.begin(115200);
  unsigned long waited = 0;
  while (!Serial && waited < 2000)
  {
    delay(10);
    waited += 10;
  }

  Serial.println();
  Serial.println("╔════════════════════════════════════════════════════════════╗");
  Serial.println("║       Sentient Engine - Music Puzzle Controller v2        ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝");
  Serial.print("[Music] Firmware Version: ");
  Serial.println(firmware::VERSION);
  Serial.print("[Music] Build Date: ");
  Serial.println(firmware::BUILD_DATE);
  Serial.print("[Music] Controller ID: ");
  Serial.println(CONTROLLER_ID);
  Serial.println();

  // Power LED
  pinMode(powerLED, OUTPUT);
  digitalWrite(powerLED, HIGH);

  // Button setup with internal pull-up resistors
  Serial.println("[Music] Configuring button inputs...");
  pinMode(button1_pin, INPUT_PULLUP);
  pinMode(button2_pin, INPUT_PULLUP);
  pinMode(button3_pin, INPUT_PULLUP);
  pinMode(button4_pin, INPUT_PULLUP);
  pinMode(button5_pin, INPUT_PULLUP);
  pinMode(button6_pin, INPUT_PULLUP);

  // Register devices
  Serial.println("[Music] Registering devices...");
  deviceRegistry.addDevice(&dev_button1);
  deviceRegistry.addDevice(&dev_button2);
  deviceRegistry.addDevice(&dev_button3);
  deviceRegistry.addDevice(&dev_button4);
  deviceRegistry.addDevice(&dev_button5);
  deviceRegistry.addDevice(&dev_button6);

  // Build capability manifest
  Serial.println("[Music] Building capability manifest...");
  build_capability_manifest();

  // Initialize MQTT
  Serial.println("[Music] Connecting to MQTT broker...");
  if (!mqtt.begin())
  {
    Serial.println("[Music] ⚠ MQTT initialization failed - will retry in loop");
  }
  else
  {
    Serial.println("[Music] ✓ MQTT connected successfully");

    // Set heartbeat builder
    mqtt.setHeartbeatBuilder(build_heartbeat_payload);

    // Wait for broker connection
    Serial.println("[Music] Waiting for broker connection...");
    unsigned long connection_start = millis();
    while (!mqtt.isConnected() && (millis() - connection_start < 5000))
    {
      mqtt.loop();
      delay(100);
    }

    if (mqtt.isConnected())
    {
      Serial.println("[Music] Broker connected!");

      // Register with Sentient system
      Serial.println("[Music] Registering with Sentient system...");
      if (manifest.publish_registration(mqtt.get_client(), ROOM_ID, CONTROLLER_ID))
      {
        Serial.println("[Music] Registration successful!");
        registration_sent = true;
      }
      else
      {
        Serial.println("[Music] Registration failed - will retry later");
        registration_sent = false;
        last_registration_attempt_ms = millis();
      }

      // Publish initial sensor states
      publish_sensor_changes(true);
    }
    else
    {
      Serial.println("[Music] Broker connection timeout - continuing offline");
    }
  }

  Serial.println("[Music] ✓ Setup complete - entering main loop");
  Serial.println();
}

void loop()
{
  mqtt.loop();
  read_buttons();

  // Check for periodic publish
  unsigned long current_time = millis();
  bool force_publish = (current_time - last_sensor_publish_time >= sensor_publish_interval);

  publish_sensor_changes(force_publish);

  if (force_publish)
  {
    last_sensor_publish_time = current_time;
  }

  // Retry capability registration until successful
  if (mqtt.isConnected() && !registration_sent)
  {
    if (last_registration_attempt_ms == 0 || (millis() - last_registration_attempt_ms) >= registration_retry_interval_ms)
    {
      Serial.println("[Music] Retrying registration...");
      if (manifest.publish_registration(mqtt.get_client(), ROOM_ID, CONTROLLER_ID))
      {
        Serial.println("[Music] Registration successful (retry)!");
        registration_sent = true;
      }
      else
      {
        Serial.println("[Music] Registration still failing");
      }
      last_registration_attempt_ms = millis();
    }
  }
} // ============================================================================
// BUTTON READING
// ============================================================================

void read_buttons()
{
  // Read button 1
  bool reading1 = !digitalRead(button1_pin);
  if (reading1 != last_button1)
  {
    last_debounce_time1 = millis();
  }
  if ((millis() - last_debounce_time1) > debounce_delay)
  {
    if (reading1 != button1_state)
    {
      button1_state = reading1;
    }
  }
  last_button1 = reading1;

  // Read button 2
  bool reading2 = !digitalRead(button2_pin);
  if (reading2 != last_button2)
  {
    last_debounce_time2 = millis();
  }
  if ((millis() - last_debounce_time2) > debounce_delay)
  {
    if (reading2 != button2_state)
    {
      button2_state = reading2;
    }
  }
  last_button2 = reading2;

  // Read button 3
  bool reading3 = !digitalRead(button3_pin);
  if (reading3 != last_button3)
  {
    last_debounce_time3 = millis();
  }
  if ((millis() - last_debounce_time3) > debounce_delay)
  {
    if (reading3 != button3_state)
    {
      button3_state = reading3;
    }
  }
  last_button3 = reading3;

  // Read button 4
  bool reading4 = !digitalRead(button4_pin);
  if (reading4 != last_button4)
  {
    last_debounce_time4 = millis();
  }
  if ((millis() - last_debounce_time4) > debounce_delay)
  {
    if (reading4 != button4_state)
    {
      button4_state = reading4;
    }
  }
  last_button4 = reading4;

  // Read button 5
  bool reading5 = !digitalRead(button5_pin);
  if (reading5 != last_button5)
  {
    last_debounce_time5 = millis();
  }
  if ((millis() - last_debounce_time5) > debounce_delay)
  {
    if (reading5 != button5_state)
    {
      button5_state = reading5;
    }
  }
  last_button5 = reading5;

  // Read button 6
  bool reading6 = !digitalRead(button6_pin);
  if (reading6 != last_button6)
  {
    last_debounce_time6 = millis();
  }
  if ((millis() - last_debounce_time6) > debounce_delay)
  {
    if (reading6 != button6_state)
    {
      button6_state = reading6;
    }
  }
  last_button6 = reading6;
}

// ============================================================================
// SENSOR PUBLISHING
// ============================================================================

void publish_sensor_changes(bool force_publish)
{
  static bool last_published_button1 = false;
  static bool last_published_button2 = false;
  static bool last_published_button3 = false;
  static bool last_published_button4 = false;
  static bool last_published_button5 = false;
  static bool last_published_button6 = false;

  if (!sensors_initialized)
  {
    // Initial state publish
    sensors_initialized = true;
    force_publish = true;
  }

  StaticJsonDocument<64> doc;

  // Button 1
  if (force_publish || button1_state != last_published_button1)
  {
    doc.clear();
    doc["state"] = button1_state ? 1 : 0;
    mqtt.publishJson(CAT_SENSORS, (String(DEV_BUTTON_1) + "/" + SENSOR_BUTTON_1_PRESSED).c_str(), doc);
    last_published_button1 = button1_state;
    Serial.print("[Music] button_1: ");
    Serial.println(button1_state ? "pressed" : "released");
  }

  // Button 2
  if (force_publish || button2_state != last_published_button2)
  {
    doc.clear();
    doc["state"] = button2_state ? 1 : 0;
    mqtt.publishJson(CAT_SENSORS, (String(DEV_BUTTON_2) + "/" + SENSOR_BUTTON_2_PRESSED).c_str(), doc);
    last_published_button2 = button2_state;
    Serial.print("[Music] button_2: ");
    Serial.println(button2_state ? "pressed" : "released");
  }

  // Button 3
  if (force_publish || button3_state != last_published_button3)
  {
    doc.clear();
    doc["state"] = button3_state ? 1 : 0;
    mqtt.publishJson(CAT_SENSORS, (String(DEV_BUTTON_3) + "/" + SENSOR_BUTTON_3_PRESSED).c_str(), doc);
    last_published_button3 = button3_state;
    Serial.print("[Music] button_3: ");
    Serial.println(button3_state ? "pressed" : "released");
  }

  // Button 4
  if (force_publish || button4_state != last_published_button4)
  {
    doc.clear();
    doc["state"] = button4_state ? 1 : 0;
    mqtt.publishJson(CAT_SENSORS, (String(DEV_BUTTON_4) + "/" + SENSOR_BUTTON_4_PRESSED).c_str(), doc);
    last_published_button4 = button4_state;
    Serial.print("[Music] button_4: ");
    Serial.println(button4_state ? "pressed" : "released");
  }

  // Button 5
  if (force_publish || button5_state != last_published_button5)
  {
    doc.clear();
    doc["state"] = button5_state ? 1 : 0;
    mqtt.publishJson(CAT_SENSORS, (String(DEV_BUTTON_5) + "/" + SENSOR_BUTTON_5_PRESSED).c_str(), doc);
    last_published_button5 = button5_state;
    Serial.print("[Music] button_5: ");
    Serial.println(button5_state ? "pressed" : "released");
  }

  // Button 6
  if (force_publish || button6_state != last_published_button6)
  {
    doc.clear();
    doc["state"] = button6_state ? 1 : 0;
    mqtt.publishJson(CAT_SENSORS, (String(DEV_BUTTON_6) + "/" + SENSOR_BUTTON_6_PRESSED).c_str(), doc);
    last_published_button6 = button6_state;
    Serial.print("[Music] button_6: ");
    Serial.println(button6_state ? "pressed" : "released");
  }
} // ============================================================================
// CAPABILITY MANIFEST
// ============================================================================

void build_capability_manifest()
{
  manifest.set_controller_info(
      CONTROLLER_ID,
      CONTROLLER_FRIENDLY_NAME,
      firmware::VERSION,
      ROOM_ID,
      CONTROLLER_ID);

  deviceRegistry.buildManifest(manifest);
} // ============================================================================
// HEARTBEAT PAYLOAD
// ============================================================================

bool build_heartbeat_payload(JsonDocument &doc, void * /*ctx*/)
{
  doc["uid"] = CONTROLLER_ID;
  doc["fw"] = firmware::VERSION;
  doc["up"] = millis();
  return true;
}