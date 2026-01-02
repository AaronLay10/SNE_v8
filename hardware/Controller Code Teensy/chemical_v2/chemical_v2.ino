// ══════════════════════════════════════════════════════════════════════════════
// Chemical Puzzle Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Chemical Puzzle - RFID Tag Reader & Actuator Controller
 *
 * Controlled Systems:
 * - 6 RFID Readers (Serial1-6) with Tag-in-Range (TIR) sensors
 * - Linear Actuator (Forward/Reverse/Stop control)
 * - Door Maglocks (Lock/Unlock control)
 *
 * STATELESS ARCHITECTURE:
 * - RFID readers REPORT tag presence/removal (does NOT make decisions)
 * - Sentient decides all actions (actuator movement, maglock control, etc.)
 * - No autonomous behavior - pure input/output controller
 */

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 1024 // Raised for larger control/registration packets
#endif

#include <SentientCapabilityManifest.h>
#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#if __has_include(<NativeEthernet.h>)
#include <NativeEthernet.h>
#define SENTIENT_HAS_NATIVE_ETHERNET 1
#else
#define SENTIENT_HAS_NATIVE_ETHERNET 0
#endif

#include "FirmwareMetadata.h"
#include "controller_naming.h"

// ──────────────────────────────────────────────────────────────────────────────
// Device Definitions
// ──────────────────────────────────────────────────────────────────────────────
// *** 6 RFID Readers - Serial ports with Tag-in-Range (TIR) sensors
// *** Linear Actuator - Forward/Reverse movement control
// *** Door Maglocks - Lock/Unlock control

// ──────────────────────────────────────────────────────────────────────────────
// Pin Assignments
// ──────────────────────────────────────────────────────────────────────────────
const int power_led_pin = 13;

// RFID Serial Ports
#define RFID_C Serial1 // Pins 1(TX) - Green, 0(RX) - White - Verified
#define RFID_E Serial2 // Pins 8(TX) - Green, 7(RX) - White - Verified
#define RFID_A Serial3 // Pins 14(TX) - Green, 15(RX) - White - Verified
#define RFID_F Serial4 // Pins 17(TX) - Green, 16(RX) - White - Verified
#define RFID_B Serial5 // Pins 20(TX) - Green, 21(RX) - White - Verified
#define RFID_D Serial6 // Pins 24(TX) - Green, 25(RX) - White - Verified

// RFID - Tag in Range Sensor (TIR) pins
const int tir_pin_a = 41;
const int tir_pin_b = 19;
const int tir_pin_c = 2;
const int tir_pin_d = 26;
const int tir_pin_e = 9;
const int tir_pin_f = 18;

// Actuator and Maglock pins
const int actuator_fwd_pin = 22;
const int actuator_rwd_pin = 23;
const int maglocks_pin = 36;

// ──────────────────────────────────────────────────────────────────────────────
// Configuration Constants
// ──────────────────────────────────────────────────────────────────────────────
const unsigned long heartbeat_interval_ms = 5000;
const int id_buffer_len = 20;

// ──────────────────────────────────────────────────────────────────────────────
// MQTT Configuration
// ──────────────────────────────────────────────────────────────────────────────
const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_username = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
static const size_t metadata_json_capacity = 2048;

// ──────────────────────────────────────────────────────────────────────────────
// Hardware State Variables (NOT game state - just hardware execution flags)
// ──────────────────────────────────────────────────────────────────────────────
// RFID tag buffers
char tag_buffer_a[id_buffer_len] = "";
char tag_buffer_b[id_buffer_len] = "";
char tag_buffer_c[id_buffer_len] = "";
char tag_buffer_d[id_buffer_len] = "";
char tag_buffer_e[id_buffer_len] = "";
char tag_buffer_f[id_buffer_len] = "";

// Tag presence flags
bool tag_present_a = false;
bool tag_present_b = false;
bool tag_present_c = false;
bool tag_present_d = false;
bool tag_present_e = false;
bool tag_present_f = false;

// Last published states for change detection
char last_tag_a[id_buffer_len] = "";
char last_tag_b[id_buffer_len] = "";
char last_tag_c[id_buffer_len] = "";
char last_tag_d[id_buffer_len] = "";
char last_tag_e[id_buffer_len] = "";
char last_tag_f[id_buffer_len] = "";

// Last TIR states for change detection
bool last_tir_a = false;
bool last_tir_b = false;
bool last_tir_c = false;
bool last_tir_d = false;
bool last_tir_e = false;
bool last_tir_f = false;

// Actuator state
bool actuator_moving = false;

// ──────────────────────────────────────────────────────────────────────────────
// Forward Declarations
// ──────────────────────────────────────────────────────────────────────────────
void handle_rfid_reader(HardwareSerial &rfid, char *tag_buffer, bool *tag_present,
                        bool *last_tir, int tir_pin, const char *reader_name, const char *sensor_name);
void process_rfid_byte(char incoming_char, char *tag_buffer);
void publish_rfid_state(const char *reader_name, const char *sensor_name, const char *tag_id, bool is_present);
void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void publish_command_acknowledgement(const char *device_id, const char *command);

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 2: DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!)
// ══════════════════════════════════════════════════════════════════════════════

// Define command arrays
const char *actuator_commands[] = {
    naming::CMD_ACTUATOR_FORWARD,
    naming::CMD_ACTUATOR_REVERSE,
    naming::CMD_ACTUATOR_STOP};

const char *maglock_commands[] = {
    naming::CMD_MAGLOCKS_LOCK,
    naming::CMD_MAGLOCKS_UNLOCK};

// Define sensor arrays for each RFID reader
const char *rfid_a_sensors[] = {
    naming::SENSOR_RFID_TAG_A,
    naming::SENSOR_TIR_A};

const char *rfid_b_sensors[] = {
    naming::SENSOR_RFID_TAG_B,
    naming::SENSOR_TIR_B};

const char *rfid_c_sensors[] = {
    naming::SENSOR_RFID_TAG_C,
    naming::SENSOR_TIR_C};

const char *rfid_d_sensors[] = {
    naming::SENSOR_RFID_TAG_D,
    naming::SENSOR_TIR_D};

const char *rfid_e_sensors[] = {
    naming::SENSOR_RFID_TAG_E,
    naming::SENSOR_TIR_E};

const char *rfid_f_sensors[] = {
    naming::SENSOR_RFID_TAG_F,
    naming::SENSOR_TIR_F};

// Create device definitions with canonical IDs and friendly names
SentientDeviceDef dev_rfid_a(
    naming::DEV_RFID_A,
    naming::FRIENDLY_RFID_A,
    "sensor",
    rfid_a_sensors, 2, true); // true = input device (sensor only)

SentientDeviceDef dev_rfid_b(
    naming::DEV_RFID_B,
    naming::FRIENDLY_RFID_B,
    "sensor",
    rfid_b_sensors, 2, true); // true = input device (sensor only)

SentientDeviceDef dev_rfid_c(
    naming::DEV_RFID_C,
    naming::FRIENDLY_RFID_C,
    "sensor",
    rfid_c_sensors, 2, true); // true = input device (sensor only)

SentientDeviceDef dev_rfid_d(
    naming::DEV_RFID_D,
    naming::FRIENDLY_RFID_D,
    "sensor",
    rfid_d_sensors, 2, true); // true = input device (sensor only)

SentientDeviceDef dev_rfid_e(
    naming::DEV_RFID_E,
    naming::FRIENDLY_RFID_E,
    "sensor",
    rfid_e_sensors, 2, true); // true = input device (sensor only)

SentientDeviceDef dev_rfid_f(
    naming::DEV_RFID_F,
    naming::FRIENDLY_RFID_F,
    "sensor",
    rfid_f_sensors, 2, true); // true = input device (sensor only)

SentientDeviceDef dev_actuator(
    naming::DEV_ACTUATOR,
    naming::FRIENDLY_ACTUATOR,
    "actuator",
    actuator_commands, 3);

SentientDeviceDef dev_maglocks(
    naming::DEV_MAGLOCKS,
    naming::FRIENDLY_MAGLOCKS,
    "relay",
    maglock_commands, 2);

// Create the device registry
SentientDeviceRegistry deviceRegistry;

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 3: SENTIENT MQTT INITIALIZATION
// ══════════════════════════════════════════════════════════════════════════════

SentientMQTTConfig make_mqtt_config()
{
  SentientMQTTConfig cfg;
  cfg.brokerHost = mqtt_host;
  cfg.brokerIp = mqtt_broker_ip;
  cfg.brokerPort = mqtt_port;
  cfg.username = mqtt_username;
  cfg.password = mqtt_password;
  cfg.namespaceId = naming::CLIENT_ID;
  cfg.roomId = naming::ROOM_ID;
  cfg.controllerId = naming::CONTROLLER_ID;
  cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
  cfg.displayName = naming::CONTROLLER_FRIENDLY_NAME;
  cfg.useDhcp = true;
  cfg.heartbeatIntervalMs = heartbeat_interval_ms;
  cfg.publishJsonCapacity = metadata_json_capacity;
  cfg.autoHeartbeat = true;
  return cfg;
}

SentientMQTT sentient(make_mqtt_config());

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 3.5: CAPABILITY MANIFEST
// ══════════════════════════════════════════════════════════════════════════════

SentientCapabilityManifest manifest;

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 4: SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("╔════════════════════════════════════════════════╗");
  Serial.println("║  Chemical Puzzle Controller v2.0.1            ║");
  Serial.println("║  Sentient Engine - Paragon Escape Games       ║");
  Serial.println("╚════════════════════════════════════════════════╝");
  Serial.println();

  // Initialize RFID Serial Ports
  RFID_A.begin(9600);
  RFID_B.begin(9600);
  RFID_C.begin(9600);
  RFID_D.begin(9600);
  RFID_E.begin(9600);
  RFID_F.begin(9600);

  // Initialize TIR sensor pins
  pinMode(tir_pin_a, INPUT_PULLDOWN);
  pinMode(tir_pin_b, INPUT_PULLDOWN);
  pinMode(tir_pin_c, INPUT_PULLDOWN);
  pinMode(tir_pin_d, INPUT_PULLDOWN);
  pinMode(tir_pin_e, INPUT_PULLDOWN);
  pinMode(tir_pin_f, INPUT_PULLDOWN);

  // Initialize actuator and maglock pins
  pinMode(actuator_fwd_pin, OUTPUT);
  pinMode(actuator_rwd_pin, OUTPUT);
  pinMode(maglocks_pin, OUTPUT);
  pinMode(power_led_pin, OUTPUT);

  // Set initial states
  digitalWrite(power_led_pin, HIGH);
  digitalWrite(actuator_fwd_pin, LOW);
  digitalWrite(actuator_rwd_pin, LOW);
  digitalWrite(maglocks_pin, HIGH); // Locked by default

  Serial.println("[INIT] Hardware initialized");

  // Register all devices
  Serial.println("[INIT] Registering devices...");
  deviceRegistry.addDevice(&dev_rfid_a);
  deviceRegistry.addDevice(&dev_rfid_b);
  deviceRegistry.addDevice(&dev_rfid_c);
  deviceRegistry.addDevice(&dev_rfid_d);
  deviceRegistry.addDevice(&dev_rfid_e);
  deviceRegistry.addDevice(&dev_rfid_f);
  deviceRegistry.addDevice(&dev_actuator);
  deviceRegistry.addDevice(&dev_maglocks);
  deviceRegistry.printSummary();

  // Build capability manifest
  Serial.println("[INIT] Building capability manifest...");
  manifest.set_controller_info(
      naming::CONTROLLER_ID,
      naming::CONTROLLER_FRIENDLY_NAME,
      firmware::VERSION,
      naming::ROOM_ID,
      naming::CONTROLLER_ID);

  // Auto-generate manifest from device registry
  deviceRegistry.buildManifest(manifest);
  Serial.println("[INIT] Manifest built from device registry");

  // Initialize Sentient MQTT
  sentient.begin();
  sentient.setCommandCallback(handle_mqtt_command);

  Serial.println("[INIT] Sentient MQTT initialized");
  Serial.println("[INIT] Waiting for network connection...");

  // Wait for broker connection (max 5 seconds)
  unsigned long connection_start = millis();
  while (!sentient.isConnected() && (millis() - connection_start < 5000))
  {
    sentient.loop();
    delay(100);
  }

  if (sentient.isConnected())
  {
    Serial.println("[INIT] Broker connected!");

    // Register with Sentient system
    Serial.println("[INIT] Registering with Sentient system...");
    if (manifest.publish_registration(sentient.get_client(), naming::ROOM_ID, naming::CONTROLLER_ID))
    {
      Serial.println("[INIT] Registration successful!");
    }
    else
    {
      Serial.println("[INIT] Registration failed - will retry later");
    }
  }
  else
  {
    Serial.println("[INIT] Broker connection timeout - will retry in main loop");
  }

  Serial.println("[INIT] Chemical controller ready");
  Serial.print("[INIT] Firmware: ");
  Serial.println(firmware::VERSION);
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 5: MAIN LOOP (Listen → Detect → Execute)
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
  // ────────────────────────────────────────────────────────────────────────────
  // LISTEN: Maintain MQTT connection and process incoming messages
  // ────────────────────────────────────────────────────────────────────────────
  sentient.loop();

  // ────────────────────────────────────────────────────────────────────────────
  // DETECT: Monitor all RFID readers and publish tag changes
  // ────────────────────────────────────────────────────────────────────────────
  handle_rfid_reader(RFID_A, tag_buffer_a, &tag_present_a, &last_tir_a, tir_pin_a, naming::DEV_RFID_A, naming::SENSOR_RFID_TAG_A);
  handle_rfid_reader(RFID_B, tag_buffer_b, &tag_present_b, &last_tir_b, tir_pin_b, naming::DEV_RFID_B, naming::SENSOR_RFID_TAG_B);
  handle_rfid_reader(RFID_C, tag_buffer_c, &tag_present_c, &last_tir_c, tir_pin_c, naming::DEV_RFID_C, naming::SENSOR_RFID_TAG_C);
  handle_rfid_reader(RFID_D, tag_buffer_d, &tag_present_d, &last_tir_d, tir_pin_d, naming::DEV_RFID_D, naming::SENSOR_RFID_TAG_D);
  handle_rfid_reader(RFID_E, tag_buffer_e, &tag_present_e, &last_tir_e, tir_pin_e, naming::DEV_RFID_E, naming::SENSOR_RFID_TAG_E);
  handle_rfid_reader(RFID_F, tag_buffer_f, &tag_present_f, &last_tir_f, tir_pin_f, naming::DEV_RFID_F, naming::SENSOR_RFID_TAG_F);

  // ────────────────────────────────────────────────────────────────────────────
  // EXECUTE: All command execution happens in execute_command() callback
  // ────────────────────────────────────────────────────────────────────────────
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 6: RFID READER FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

void handle_rfid_reader(HardwareSerial &rfid, char *tag_buffer, bool *tag_present,
                        bool *last_tir, int tir_pin, const char *reader_name, const char *sensor_name)
{
  // Process incoming RFID data
  while (rfid.available())
  {
    char incoming_char = rfid.read();
    process_rfid_byte(incoming_char, tag_buffer);
  }

  // Check Tag-in-Range (TIR) sensor
  bool current_tir = (digitalRead(tir_pin) == HIGH);

  // Update tag presence based on TIR sensor
  if (!current_tir)
  {
    // No tag in range
    if (*tag_present)
    {
      // Tag was present, now removed
      *tag_present = false;
      tag_buffer[0] = '\0';
      publish_rfid_state(reader_name, sensor_name, "", false);
    }
  }
  else
  {
    // Tag in range
    if (!*tag_present && tag_buffer[0] != '\0')
    {
      // New tag detected
      *tag_present = true;
      publish_rfid_state(reader_name, sensor_name, tag_buffer, true);
    }
  }

  *last_tir = current_tir;
}

void process_rfid_byte(char incoming_char, char *tag_buffer)
{
  static char temp_buffer[32];
  static int buffer_index = 0;
  static bool packet_started = false;

  if (incoming_char == 0x02)
  {
    // STX - Start of packet
    buffer_index = 0;
    packet_started = true;
  }
  else if (incoming_char == 0x03 && packet_started)
  {
    // ETX - End of packet
    temp_buffer[buffer_index] = '\0';

    // Remove trailing CR if present
    int len = strlen(temp_buffer);
    if (len > 0 && temp_buffer[len - 1] == '\r')
    {
      temp_buffer[len - 1] = '\0';
    }

    // Copy to tag buffer
    strncpy(tag_buffer, temp_buffer, id_buffer_len - 1);
    tag_buffer[id_buffer_len - 1] = '\0';

    packet_started = false;
  }
  else if (packet_started && buffer_index < (int)sizeof(temp_buffer) - 1)
  {
    // Store character
    temp_buffer[buffer_index++] = incoming_char;
  }
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 7: SENSOR PUBLISHING
// ══════════════════════════════════════════════════════════════════════════════

void publish_rfid_state(const char *reader_name, const char *sensor_name, const char *tag_id, bool is_present)
{
  if (!sentient.isConnected())
    return;

  // Build JSON document
  JsonDocument doc;
  doc["reader"] = reader_name;
  doc["tag_id"] = tag_id;
  doc["present"] = is_present;
  doc["timestamp"] = millis();

  // Publish to sensors/{reader_name}/{sensor_name}
  sentient.publishJson(naming::CAT_SENSORS, sensor_name, doc);

  Serial.print("[RFID] ");
  Serial.print(reader_name);
  Serial.print(": ");
  if (is_present)
  {
    Serial.print("Tag detected: ");
    Serial.println(tag_id);
  }
  else
  {
    Serial.println("Tag removed");
  }
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 8: COMMAND EXECUTION
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void * /*ctx*/)
{
  Serial.print("[CMD] Received: ");
  Serial.println(command);

  // Parse device_id/command format
  char device_id[32] = "";
  char cmd_name[32] = "";
  const char *slash = strchr(command, '/');
  if (slash)
  {
    int device_len = slash - command;
    strncpy(device_id, command, device_len);
    device_id[device_len] = '\0';
    strcpy(cmd_name, slash + 1);
  }
  else
  {
    strcpy(cmd_name, command);
  }

  Serial.print("[CMD] Device: ");
  Serial.print(device_id);
  Serial.print(", Action: ");
  Serial.println(cmd_name);

  // ────────────────────────────────────────────────────────────────────────────
  // Actuator Commands
  // ────────────────────────────────────────────────────────────────────────────
  if (strcmp(device_id, naming::DEV_ACTUATOR) == 0)
  {
    if (strcmp(cmd_name, naming::CMD_ACTUATOR_FORWARD) == 0)
    {
      Serial.println("[ACTUATOR] Moving FORWARD");
      digitalWrite(actuator_fwd_pin, HIGH);
      digitalWrite(actuator_rwd_pin, LOW);
      actuator_moving = true;
    }
    else if (strcmp(cmd_name, naming::CMD_ACTUATOR_REVERSE) == 0)
    {
      Serial.println("[ACTUATOR] Moving REVERSE");
      digitalWrite(actuator_fwd_pin, LOW);
      digitalWrite(actuator_rwd_pin, HIGH);
      actuator_moving = true;
    }
    else if (strcmp(cmd_name, naming::CMD_ACTUATOR_STOP) == 0)
    {
      Serial.println("[ACTUATOR] STOPPED");
      digitalWrite(actuator_fwd_pin, LOW);
      digitalWrite(actuator_rwd_pin, LOW);
      actuator_moving = false;
    }
  }
  // ────────────────────────────────────────────────────────────────────────────
  // Maglock Commands
  // ────────────────────────────────────────────────────────────────────────────
  else if (strcmp(device_id, naming::DEV_MAGLOCKS) == 0)
  {
    if (strcmp(cmd_name, naming::CMD_MAGLOCKS_LOCK) == 0)
    {
      Serial.println("[MAGLOCKS] LOCKED");
      digitalWrite(maglocks_pin, HIGH);
    }
    else if (strcmp(cmd_name, naming::CMD_MAGLOCKS_UNLOCK) == 0)
    {
      Serial.println("[MAGLOCKS] UNLOCKED");
      digitalWrite(maglocks_pin, LOW);
    }
  }
  else
  {
    Serial.print("[CMD] Unknown device: ");
    Serial.println(device_id);
    return; // Don't send ACK for unknown commands
  }

  // Publish command acknowledgement
  publish_command_acknowledgement(device_id, cmd_name);
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 9: COMMAND ACKNOWLEDGEMENT
// ══════════════════════════════════════════════════════════════════════════════

void publish_command_acknowledgement(const char *device_id, const char *command)
{
  if (!sentient.isConnected())
    return;

  StaticJsonDocument<160> ack;
  ack["controller_id"] = naming::CONTROLLER_ID;
  ack["device_id"] = device_id;
  ack["command"] = command;
  ack["success"] = true;
  ack["timestamp_ms"] = millis();

  char buf[196];
  serializeJson(ack, buf, sizeof(buf));

  // Topic: <tenant>/<room>/acknowledgement/<controller>/<device>/<command>
  String ackTopic = String(naming::CLIENT_ID) + "/" + String(naming::ROOM_ID) + "/" +
                    String(naming::CAT_ACKNOWLEDGEMENT) + "/" + String(naming::CONTROLLER_ID) + "/" +
                    String(device_id) + "/" + String(command);

  sentient.get_client().publish(ackTopic.c_str(), buf, false);

  Serial.print("[ACK] -> ");
  Serial.println(ackTopic);
}
