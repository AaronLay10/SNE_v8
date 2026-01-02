// ══════════════════════════════════════════════════════════════════════════════
// Power Control Lower Left v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Power Control Lower Left - Zone Power Distribution Controller
 *
 * Controlled Systems:
 * - Lever Riddle Cube Power (24V, 12V, 5V)
 * - Clock Puzzle Power (24V, 12V, 5V)
 *
 * Total: 6 Relay Outputs
 *
 * STATELESS ARCHITECTURE:
 * - Sentient decides all power state changes
 * - Controller executes ON/OFF commands and reports status
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

#include "FirmwareMetadata.h"
#include "controller_naming.h"

// ──────────────────────────────────────────────────────────────────────────────
// Pin Assignments (6 relay outputs)
// ──────────────────────────────────────────────────────────────────────────────
const int power_led_pin = 13;

// Lever Riddle Cube Power Rails
const int lever_riddle_cube_24v_pin = 33;
const int lever_riddle_cube_12v_pin = 34;
const int lever_riddle_cube_5v_pin = 35;

// Clock Puzzle Power Rails
const int clock_24v_pin = 36;
const int clock_12v_pin = 37;
const int clock_5v_pin = 38;

// ──────────────────────────────────────────────────────────────────────────────
// Configuration Constants
// ──────────────────────────────────────────────────────────────────────────────
const unsigned long heartbeat_interval_ms = 5000;
static const size_t metadata_json_capacity = 1024;

// ──────────────────────────────────────────────────────────────────────────────
// MQTT Configuration
// ──────────────────────────────────────────────────────────────────────────────
const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_user = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";

// ──────────────────────────────────────────────────────────────────────────────
// Hardware State Variables (relay states: true = ON/energized, false = OFF)
// ──────────────────────────────────────────────────────────────────────────────
bool lever_riddle_cube_24v_state = false;
bool lever_riddle_cube_12v_state = false;
bool lever_riddle_cube_5v_state = false;
bool clock_24v_state = false;
bool clock_12v_state = false;
bool clock_5v_state = false;

// ============================================================================
// DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!)
// ============================================================================

// Standard power commands (on/off)
const char *power_commands[] = {
    naming::CMD_POWER_ON,
    naming::CMD_POWER_OFF};

// Controller-level commands
const char *controller_commands[] = {
    naming::CMD_ALL_ON,
    naming::CMD_ALL_OFF,
    naming::CMD_EMERGENCY_OFF,
    naming::CMD_RESET,
    naming::CMD_REQUEST_STATUS};

// Create device definitions for all 6 relays
SentientDeviceDef dev_lever_riddle_cube_24v(naming::DEV_LEVER_RIDDLE_CUBE_24V, naming::FRIENDLY_LEVER_RIDDLE_CUBE_24V, "relay", power_commands, 2);
SentientDeviceDef dev_lever_riddle_cube_12v(naming::DEV_LEVER_RIDDLE_CUBE_12V, naming::FRIENDLY_LEVER_RIDDLE_CUBE_12V, "relay", power_commands, 2);
SentientDeviceDef dev_lever_riddle_cube_5v(naming::DEV_LEVER_RIDDLE_CUBE_5V, naming::FRIENDLY_LEVER_RIDDLE_CUBE_5V, "relay", power_commands, 2);
SentientDeviceDef dev_clock_24v(naming::DEV_CLOCK_24V, naming::FRIENDLY_CLOCK_24V, "relay", power_commands, 2);
SentientDeviceDef dev_clock_12v(naming::DEV_CLOCK_12V, naming::FRIENDLY_CLOCK_12V, "relay", power_commands, 2);
SentientDeviceDef dev_clock_5v(naming::DEV_CLOCK_5V, naming::FRIENDLY_CLOCK_5V, "relay", power_commands, 2);
SentientDeviceDef dev_controller(naming::DEV_CONTROLLER, naming::FRIENDLY_CONTROLLER, "controller", controller_commands, 5);

// Create the device registry
SentientDeviceRegistry deviceRegistry;

// ──────────────────────────────────────────────────────────────────────────────
// Forward Declarations
// ──────────────────────────────────────────────────────────────────────────────
void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);
void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void set_relay_state(int pin, bool state, bool &state_var, const char *device_name, const char *device_id, const char *command);
void publish_relay_state(const char *device_id, bool state);
void publish_command_acknowledgement(const char *device_id, const char *command, bool state);
void publish_hardware_status();
void publish_full_status();
void report_actual_relay_states();
void all_relays_on();
void all_relays_off();
void emergency_power_off();
const char *build_device_identifier();
const char *get_hardware_label();

// MQTT objects
SentientCapabilityManifest manifest;
SentientMQTT mqtt(build_mqtt_config());

// ============================================================================
// SECTION 2: SETUP FUNCTION
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

    Serial.println(F("=== Power Control Lower Left v2.0.1 - STATELESS MODE ==="));
    Serial.print(F("Board: "));
    Serial.println(teensyBoardVersion());
    Serial.print(F("USB SN: "));
    Serial.println(teensyUsbSN());
    Serial.print(F("MAC: "));
    Serial.println(teensyMAC());
    Serial.print(F("Firmware: "));
    Serial.println(firmware::VERSION);
    Serial.print(F("Controller ID: "));
    Serial.println(naming::CONTROLLER_ID);

    // Initialize power LED
    pinMode(power_led_pin, OUTPUT);
    digitalWrite(power_led_pin, HIGH);

    // Initialize all relay pins (LOW = OFF by default)
    pinMode(lever_riddle_cube_24v_pin, OUTPUT);
    pinMode(lever_riddle_cube_12v_pin, OUTPUT);
    pinMode(lever_riddle_cube_5v_pin, OUTPUT);
    pinMode(clock_24v_pin, OUTPUT);
    pinMode(clock_12v_pin, OUTPUT);
    pinMode(clock_5v_pin, OUTPUT);

    // Set all relays to OFF
    digitalWrite(lever_riddle_cube_24v_pin, LOW);
    digitalWrite(lever_riddle_cube_12v_pin, LOW);
    digitalWrite(lever_riddle_cube_5v_pin, LOW);
    digitalWrite(clock_24v_pin, LOW);
    digitalWrite(clock_12v_pin, LOW);
    digitalWrite(clock_5v_pin, LOW);

    Serial.println(F("[PowerCtrl] All 6 relays initialized to OFF"));

    // Register all devices
    Serial.println(F("[PowerCtrl] Registering devices..."));
    deviceRegistry.addDevice(&dev_lever_riddle_cube_24v);
    deviceRegistry.addDevice(&dev_lever_riddle_cube_12v);
    deviceRegistry.addDevice(&dev_lever_riddle_cube_5v);
    deviceRegistry.addDevice(&dev_clock_24v);
    deviceRegistry.addDevice(&dev_clock_12v);
    deviceRegistry.addDevice(&dev_clock_5v);
    deviceRegistry.addDevice(&dev_controller);
    deviceRegistry.printSummary();

    // Build capability manifest
    Serial.println(F("[PowerCtrl] Building capability manifest..."));
    build_capability_manifest();
    Serial.println(F("[PowerCtrl] Manifest built successfully"));

    // Initialize MQTT
    Serial.println(F("[PowerCtrl] Initializing MQTT..."));
    if (!mqtt.begin())
    {
        Serial.println(F("[PowerCtrl] MQTT initialization failed - continuing without network"));
    }
    else
    {
        Serial.println(F("[PowerCtrl] MQTT initialization successful"));
        mqtt.setHeartbeatBuilder(build_heartbeat_payload);

        // Wait for broker connection (max 5 seconds)
        Serial.println(F("[PowerCtrl] Waiting for broker connection..."));
        unsigned long connection_start = millis();
        while (!mqtt.isConnected() && (millis() - connection_start < 5000))
        {
            mqtt.loop();
            delay(100);
        }

        if (mqtt.isConnected())
        {
            Serial.println(F("[PowerCtrl] Broker connected!"));

            // Register with Sentient system
            Serial.println(F("[PowerCtrl] Registering with Sentient system..."));
            if (manifest.publish_registration(mqtt.get_client(), naming::ROOM_ID, naming::CONTROLLER_ID))
            {
                Serial.println(F("[PowerCtrl] Registration successful!"));
            }
            else
            {
                Serial.println(F("[PowerCtrl] Registration failed - will retry later"));
            }

            // Subscribe to device-scoped commands
            String topic = String(naming::CLIENT_ID) + "/" + naming::ROOM_ID + "/commands/" + naming::CONTROLLER_ID + "/+/+";
            mqtt.get_client().subscribe(topic.c_str());
            Serial.print(F("[PowerCtrl] Subscribed to: "));
            Serial.println(topic);

            // Set custom message handler for device routing
            mqtt.get_client().setCallback(handle_mqtt_command);

            // Report actual physical relay states as single source of truth
            // This ensures database and cache reflect actual hardware state after power-up
            Serial.println(F("[PowerCtrl] Reporting actual relay states..."));
            report_actual_relay_states();
        }
        else
        {
            Serial.println(F("[PowerCtrl] Broker connection timeout - will retry in main loop"));
        }
    }

    Serial.println(F("[PowerCtrl] Ready - awaiting Sentient commands"));
    Serial.print(F("[PowerCtrl] Firmware: "));
    Serial.println(firmware::VERSION);
}

// ============================================================================
// SECTION 3: LOOP FUNCTION
// ============================================================================

void loop()
{
    // LISTEN for commands from Sentient
    mqtt.loop();
}

// ============================================================================
// SECTION 4: COMMAND HANDLER
// ============================================================================

void handle_mqtt_command(char *topic, uint8_t *payload, unsigned int length)
{
    // Parse topic to extract device and command
    String device, command;

    // Parse topic segments: client/room/commands/controller/device/command
    int seg = 0;
    const char *p = topic;
    const char *segStart[8] = {nullptr};
    size_t segLen[8] = {0};
    segStart[0] = p;

    while (*p && seg < 8)
    {
        if (*p == '/')
        {
            segLen[seg] = p - segStart[seg];
            seg++;
            if (seg < 8)
                segStart[seg] = p + 1;
        }
        p++;
    }
    if (seg < 8 && segStart[seg])
    {
        segLen[seg] = p - segStart[seg];
        seg++;
    }

    auto equalsSeg = [&](int idx, const char *lit) -> bool
    {
        size_t litLen = strlen(lit);
        return segLen[idx] == litLen && strncmp(segStart[idx], lit, litLen) == 0;
    };

    // Validate topic structure and extract device/command
    if (seg >= 6 && equalsSeg(0, naming::CLIENT_ID) && equalsSeg(1, naming::ROOM_ID) &&
        equalsSeg(2, "commands") && equalsSeg(3, naming::CONTROLLER_ID))
    {
        device = String(segStart[4]).substring(0, segLen[4]);
        command = String(segStart[5]).substring(0, segLen[5]);

        Serial.print(F("[PowerCtrl] Device: "));
        Serial.print(device);
        Serial.print(F(" Command: "));
        Serial.println(command);

        // Route to device-specific handlers
        if (device == naming::DEV_LEVER_RIDDLE_CUBE_24V)
        {
            if (command == "power_on")
                set_relay_state(lever_riddle_cube_24v_pin, true, lever_riddle_cube_24v_state, "Lever Riddle Cube 24V", naming::DEV_LEVER_RIDDLE_CUBE_24V, "power_on");
            else if (command == "power_off")
                set_relay_state(lever_riddle_cube_24v_pin, false, lever_riddle_cube_24v_state, "Lever Riddle Cube 24V", naming::DEV_LEVER_RIDDLE_CUBE_24V, "power_off");
        }
        else if (device == naming::DEV_LEVER_RIDDLE_CUBE_12V)
        {
            if (command == "power_on")
                set_relay_state(lever_riddle_cube_12v_pin, true, lever_riddle_cube_12v_state, "Lever Riddle Cube 12V", naming::DEV_LEVER_RIDDLE_CUBE_12V, "power_on");
            else if (command == "power_off")
                set_relay_state(lever_riddle_cube_12v_pin, false, lever_riddle_cube_12v_state, "Lever Riddle Cube 12V", naming::DEV_LEVER_RIDDLE_CUBE_12V, "power_off");
        }
        else if (device == naming::DEV_LEVER_RIDDLE_CUBE_5V)
        {
            if (command == "power_on")
                set_relay_state(lever_riddle_cube_5v_pin, true, lever_riddle_cube_5v_state, "Lever Riddle Cube 5V", naming::DEV_LEVER_RIDDLE_CUBE_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(lever_riddle_cube_5v_pin, false, lever_riddle_cube_5v_state, "Lever Riddle Cube 5V", naming::DEV_LEVER_RIDDLE_CUBE_5V, "power_off");
        }
        else if (device == naming::DEV_CLOCK_24V)
        {
            if (command == "power_on")
                set_relay_state(clock_24v_pin, true, clock_24v_state, "Clock 24V", naming::DEV_CLOCK_24V, "power_on");
            else if (command == "power_off")
                set_relay_state(clock_24v_pin, false, clock_24v_state, "Clock 24V", naming::DEV_CLOCK_24V, "power_off");
        }
        else if (device == naming::DEV_CLOCK_12V)
        {
            if (command == "power_on")
                set_relay_state(clock_12v_pin, true, clock_12v_state, "Clock 12V", naming::DEV_CLOCK_12V, "power_on");
            else if (command == "power_off")
                set_relay_state(clock_12v_pin, false, clock_12v_state, "Clock 12V", naming::DEV_CLOCK_12V, "power_off");
        }
        else if (device == naming::DEV_CLOCK_5V)
        {
            if (command == "power_on")
                set_relay_state(clock_5v_pin, true, clock_5v_state, "Clock 5V", naming::DEV_CLOCK_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(clock_5v_pin, false, clock_5v_state, "Clock 5V", naming::DEV_CLOCK_5V, "power_off");
        }
        else if (device == naming::DEV_CONTROLLER)
        {
            // Controller-level commands
            if (command == naming::CMD_ALL_ON)
            {
                Serial.println(F("[PowerCtrl] ALL ON command"));
                all_relays_on();
                publish_hardware_status();
            }
            else if (command == naming::CMD_ALL_OFF)
            {
                Serial.println(F("[PowerCtrl] ALL OFF command"));
                all_relays_off();
                publish_hardware_status();
            }
            else if (command == naming::CMD_EMERGENCY_OFF)
            {
                Serial.println(F("[PowerCtrl] EMERGENCY OFF command"));
                emergency_power_off();
                publish_hardware_status();
            }
            else if (command == naming::CMD_RESET)
            {
                Serial.println(F("[PowerCtrl] RESET command"));
                all_relays_off();
                publish_hardware_status();
            }
            else if (command == naming::CMD_REQUEST_STATUS)
            {
                Serial.println(F("[PowerCtrl] Status requested"));
                publish_full_status();
            }
        }
    }
}

// ============================================================================
// SECTION 5: RELAY CONTROL FUNCTIONS
// ============================================================================

/**
 * Publish individual relay state to MQTT
 * Topic: paragon/clockwork/status/{controller_id}/{device_id}/state
 * Payload: {"state": 1} or {"state": 0}
 */
void publish_relay_state(const char *device_id, bool state)
{
    JsonDocument doc;
    doc["state"] = state ? 1 : 0;
    doc["power"] = state;
    doc["ts"] = millis();

    // Publish to status/{device_id}/state
    String topic = String(naming::CLIENT_ID) + "/" + String(naming::ROOM_ID) + "/" +
                   String(naming::CAT_STATUS) + "/" + String(naming::CONTROLLER_ID) + "/" +
                   String(device_id) + "/state";

    char jsonBuffer[128];
    serializeJson(doc, jsonBuffer);

    mqtt.get_client().publish(topic.c_str(), jsonBuffer);

    Serial.print(F("[PowerCtrl] Published state for "));
    Serial.print(device_id);
    Serial.print(F(": "));
    Serial.println(state ? F("ON") : F("OFF"));
}

/**
 * Publish command acknowledgement to MQTT
 * Topic: paragon/clockwork/acknowledgement/{controller_id}/{device_id}/{command}
 * Payload: {"state": 1/0, "success": true, "ts": millis}
 *
 * This is published IMMEDIATELY after a command is executed to confirm receipt
 * and provide instant UI feedback separate from periodic status updates.
 */
void publish_command_acknowledgement(const char *device_id, const char *command, bool state)
{
    JsonDocument doc;
    doc["state"] = state ? 1 : 0;
    doc["power"] = state;
    doc["success"] = true;
    doc["command"] = command;
    doc["ts"] = millis();

    // Publish to acknowledgement/{controller_id}/{device_id}/{command}
    String topic = String(naming::CLIENT_ID) + "/" + String(naming::ROOM_ID) + "/" +
                   String(naming::CAT_ACKNOWLEDGEMENT) + "/" + String(naming::CONTROLLER_ID) + "/" +
                   String(device_id) + "/" + String(command);

    char jsonBuffer[128];
    serializeJson(doc, jsonBuffer);

    mqtt.get_client().publish(topic.c_str(), jsonBuffer);

    Serial.print(F("[PowerCtrl] Published ACK for "));
    Serial.print(device_id);
    Serial.print(F("/"));
    Serial.print(command);
    Serial.print(F(": "));
    Serial.println(state ? F("ON") : F("OFF"));
}

void set_relay_state(int pin, bool state, bool &state_var, const char *device_name, const char *device_id, const char *command)
{
    digitalWrite(pin, state ? HIGH : LOW);
    state_var = state;

    Serial.print(F("[PowerCtrl] "));
    Serial.print(device_name);
    Serial.print(F(": "));
    Serial.println(state ? F("ON") : F("OFF"));

    // Publish command acknowledgement for immediate UI feedback
    if (mqtt.isConnected() && command != nullptr)
    {
        publish_command_acknowledgement(device_id, command, state);
    }

    // Also publish individual relay state to MQTT for status updates
    if (mqtt.isConnected())
    {
        publish_relay_state(device_id, state);
    }
}

void all_relays_on()
{
    set_relay_state(lever_riddle_cube_24v_pin, true, lever_riddle_cube_24v_state, "Lever Riddle Cube 24V", naming::DEV_LEVER_RIDDLE_CUBE_24V, nullptr);
    set_relay_state(lever_riddle_cube_12v_pin, true, lever_riddle_cube_12v_state, "Lever Riddle Cube 12V", naming::DEV_LEVER_RIDDLE_CUBE_12V, nullptr);
    set_relay_state(lever_riddle_cube_5v_pin, true, lever_riddle_cube_5v_state, "Lever Riddle Cube 5V", naming::DEV_LEVER_RIDDLE_CUBE_5V, nullptr);
    set_relay_state(clock_24v_pin, true, clock_24v_state, "Clock 24V", naming::DEV_CLOCK_24V, nullptr);
    set_relay_state(clock_12v_pin, true, clock_12v_state, "Clock 12V", naming::DEV_CLOCK_12V, nullptr);
    set_relay_state(clock_5v_pin, true, clock_5v_state, "Clock 5V", naming::DEV_CLOCK_5V, nullptr);

    Serial.println(F("[PowerCtrl] All relays powered ON"));
}

void all_relays_off()
{
    set_relay_state(lever_riddle_cube_24v_pin, false, lever_riddle_cube_24v_state, "Lever Riddle Cube 24V", naming::DEV_LEVER_RIDDLE_CUBE_24V, nullptr);
    set_relay_state(lever_riddle_cube_12v_pin, false, lever_riddle_cube_12v_state, "Lever Riddle Cube 12V", naming::DEV_LEVER_RIDDLE_CUBE_12V, nullptr);
    set_relay_state(lever_riddle_cube_5v_pin, false, lever_riddle_cube_5v_state, "Lever Riddle Cube 5V", naming::DEV_LEVER_RIDDLE_CUBE_5V, nullptr);
    set_relay_state(clock_24v_pin, false, clock_24v_state, "Clock 24V", naming::DEV_CLOCK_24V, nullptr);
    set_relay_state(clock_12v_pin, false, clock_12v_state, "Clock 12V", naming::DEV_CLOCK_12V, nullptr);
    set_relay_state(clock_5v_pin, false, clock_5v_state, "Clock 5V", naming::DEV_CLOCK_5V, nullptr);

    Serial.println(F("[PowerCtrl] All relays powered OFF"));
}

void emergency_power_off()
{
    Serial.println(F("[PowerCtrl] !!! EMERGENCY POWER OFF !!!"));

    // Immediately cut all power
    digitalWrite(lever_riddle_cube_24v_pin, LOW);
    digitalWrite(lever_riddle_cube_12v_pin, LOW);
    digitalWrite(lever_riddle_cube_5v_pin, LOW);
    digitalWrite(clock_24v_pin, LOW);
    digitalWrite(clock_12v_pin, LOW);
    digitalWrite(clock_5v_pin, LOW);

    // Update all state variables
    lever_riddle_cube_24v_state = false;
    lever_riddle_cube_12v_state = false;
    lever_riddle_cube_5v_state = false;
    clock_24v_state = false;
    clock_12v_state = false;
    clock_5v_state = false;

    // Publish individual relay OFF states for real-time UI updates
    if (mqtt.isConnected())
    {
        publish_relay_state(naming::DEV_LEVER_RIDDLE_CUBE_24V, false);
        publish_relay_state(naming::DEV_LEVER_RIDDLE_CUBE_12V, false);
        publish_relay_state(naming::DEV_LEVER_RIDDLE_CUBE_5V, false);
        publish_relay_state(naming::DEV_CLOCK_24V, false);
        publish_relay_state(naming::DEV_CLOCK_12V, false);
        publish_relay_state(naming::DEV_CLOCK_5V, false);
    }

    // Publish emergency event
    JsonDocument doc;
    doc["event"] = "emergency_power_off";
    doc["controller"] = naming::CONTROLLER_ID;
    doc["ts"] = millis();
    mqtt.publishJson(naming::CAT_EVENTS, "emergency", doc);
}

// ============================================================================
// SECTION 6: STATUS PUBLISHING
// ============================================================================

void publish_hardware_status()
{
    JsonDocument doc;
    doc["lever_riddle_cube_24v"] = lever_riddle_cube_24v_state;
    doc["lever_riddle_cube_12v"] = lever_riddle_cube_12v_state;
    doc["lever_riddle_cube_5v"] = lever_riddle_cube_5v_state;
    doc["clock_24v"] = clock_24v_state;
    doc["clock_12v"] = clock_12v_state;
    doc["clock_5v"] = clock_5v_state;
    doc["ts"] = millis();
    doc["uid"] = naming::CONTROLLER_ID;

    mqtt.publishJson(naming::CAT_STATUS, naming::ITEM_HARDWARE, doc);
}

void publish_full_status()
{
    JsonDocument doc;

    // All relay states
    doc["lever_riddle_cube_24v"] = lever_riddle_cube_24v_state;
    doc["lever_riddle_cube_12v"] = lever_riddle_cube_12v_state;
    doc["lever_riddle_cube_5v"] = lever_riddle_cube_5v_state;
    doc["clock_24v"] = clock_24v_state;
    doc["clock_12v"] = clock_12v_state;
    doc["clock_5v"] = clock_5v_state;

    // Controller metadata
    doc["uptime"] = millis();
    doc["ts"] = millis();
    doc["uid"] = naming::CONTROLLER_ID;
    doc["fw"] = firmware::VERSION;

    mqtt.publishJson(naming::CAT_STATUS, "full", doc);
    Serial.println(F("[PowerCtrl] Full status published"));
}

/**
 * Report actual physical relay states after power-up
 *
 * CRITICAL: This establishes hardware as the single source of truth.
 * After power outages, this ensures database/cache reflect actual physical state.
 *
 * Reads actual pin states with digitalRead() and publishes to MQTT/database.
 */
void report_actual_relay_states()
{
    Serial.println(F("[PowerCtrl] === Reading Actual Physical Relay States ==="));

    // Read and report all 6 relays
    lever_riddle_cube_24v_state = digitalRead(lever_riddle_cube_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_LEVER_RIDDLE_CUBE_24V, lever_riddle_cube_24v_state);

    lever_riddle_cube_12v_state = digitalRead(lever_riddle_cube_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_LEVER_RIDDLE_CUBE_12V, lever_riddle_cube_12v_state);

    lever_riddle_cube_5v_state = digitalRead(lever_riddle_cube_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_LEVER_RIDDLE_CUBE_5V, lever_riddle_cube_5v_state);

    clock_24v_state = digitalRead(clock_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_CLOCK_24V, clock_24v_state);

    clock_12v_state = digitalRead(clock_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_CLOCK_12V, clock_12v_state);

    clock_5v_state = digitalRead(clock_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_CLOCK_5V, clock_5v_state);

    Serial.println(F("[PowerCtrl] === All 6 Relay States Reported ==="));

    // Also publish consolidated status for backward compatibility
    publish_hardware_status();
}

// ============================================================================
// SECTION 7: MQTT CONFIGURATION
// ============================================================================

void build_capability_manifest()
{
    manifest.set_controller_info(
        naming::CONTROLLER_ID,
        naming::CONTROLLER_FRIENDLY_NAME,
        firmware::VERSION,
        naming::ROOM_ID,
        naming::CONTROLLER_ID);

    // Auto-generate entire manifest from device registry
    deviceRegistry.buildManifest(manifest);
}

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
    cfg.namespaceId = naming::CLIENT_ID;
    cfg.roomId = naming::ROOM_ID;
    cfg.controllerId = naming::CONTROLLER_ID;
    cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
    cfg.displayName = naming::CONTROLLER_FRIENDLY_NAME;
    cfg.publishJsonCapacity = 1536;
    cfg.heartbeatIntervalMs = heartbeat_interval_ms;
    cfg.autoHeartbeat = true;
#if !defined(ESP32)
    cfg.useDhcp = true;
#endif
    return cfg;
}

bool build_heartbeat_payload(JsonDocument &doc, void *ctx)
{
    doc["uid"] = naming::CONTROLLER_ID;
    doc["fw"] = firmware::VERSION;
    doc["up"] = millis();
    return true;
}

const char *build_device_identifier()
{
    static char buffer[32];
    String board = String(teensyBoardVersion());
    board.trim();
    if (board.length() == 0)
    {
        board = "Teensy Controller";
    }
    board.replace("Teensy", "teensy");
    board.toLowerCase();
    board.replace(" ", "");
    board.replace("-", "");
    board.replace(".", "");
    board.replace("/", "");
    board.toCharArray(buffer, sizeof(buffer));
    return buffer;
}

const char *get_hardware_label()
{
    static char label[32];
    static bool initialized = false;
    if (!initialized)
    {
        String board = String(teensyBoardVersion());
        board.trim();
        if (board.length() == 0)
        {
            board = "Teensy Controller";
        }
        board.toCharArray(label, sizeof(label));
        initialized = true;
    }
    return label;
}
