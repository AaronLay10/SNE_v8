// ══════════════════════════════════════════════════════════════════════════════
// Power Control Lower Right v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Power Control Lower Right - Zone Power Distribution Controller
 *
 * Controlled Systems:
 * - Gear Puzzle Power (24V, 12V, 5V)
 * - Floor Puzzle Power (24V, 12V, 5V)
 * - Riddle RPi & Puzzle Power (RPi 5V, RPi 12V, Riddle 5V)
 * - Boiler Room Subpanel Power (24V, 12V, 5V)
 * - Lab Room Subpanel Power (24V, 12V, 5V)
 * - Study Room Subpanel Power (24V, 12V, 5V)
 * - Gun Drawers Power (24V, 12V, 5V)
 * - Keys Puzzle Power (5V)
 * - Reserved Relays (2x for future expansion)
 *
 * Total: 24 Relay Outputs
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
// Pin Assignments (24 relay outputs)
// ──────────────────────────────────────────────────────────────────────────────
const int power_led_pin = 13;

// Gear Puzzle Power Rails
const int gear_24v_pin = 11;
const int gear_12v_pin = 10;
const int gear_5v_pin = 9;

// Floor Puzzle Power Rails
const int floor_24v_pin = 8;
const int floor_12v_pin = 7;
const int floor_5v_pin = 6;

// Riddle RPi & Puzzle Power
const int riddle_rpi_5v_pin = 5;
const int riddle_rpi_12v_pin = 4;
const int riddle_5v_pin = 3;

// Boiler Room Subpanel Power Rails
const int boiler_room_subpanel_24v_pin = 1;
const int boiler_room_subpanel_12v_pin = 0;
const int boiler_room_subpanel_5v_pin = 2;

// Lab Room Subpanel Power Rails
const int lab_room_subpanel_24v_pin = 26;
const int lab_room_subpanel_12v_pin = 25;
const int lab_room_subpanel_5v_pin = 24;

// Study Room Subpanel Power Rails
const int study_room_subpanel_24v_pin = 29;
const int study_room_subpanel_12v_pin = 28;
const int study_room_subpanel_5v_pin = 27;

// Gun Drawers Power Rails
const int gun_drawers_24v_pin = 32;
const int gun_drawers_12v_pin = 31;
const int gun_drawers_5v_pin = 30;

// Keys Puzzle Power
const int keys_5v_pin = 33;

// Reserved/Empty Relays
const int empty_35_pin = 35;
const int empty_34_pin = 34;

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
bool gear_24v_state = false;
bool gear_12v_state = false;
bool gear_5v_state = false;
bool floor_24v_state = false;
bool floor_12v_state = false;
bool floor_5v_state = false;
bool riddle_rpi_5v_state = false;
bool riddle_rpi_12v_state = false;
bool riddle_5v_state = false;
bool boiler_room_subpanel_24v_state = false;
bool boiler_room_subpanel_12v_state = false;
bool boiler_room_subpanel_5v_state = false;
bool lab_room_subpanel_24v_state = false;
bool lab_room_subpanel_12v_state = false;
bool lab_room_subpanel_5v_state = false;
bool study_room_subpanel_24v_state = false;
bool study_room_subpanel_12v_state = false;
bool study_room_subpanel_5v_state = false;
bool gun_drawers_24v_state = false;
bool gun_drawers_12v_state = false;
bool gun_drawers_5v_state = false;
bool keys_5v_state = false;
bool empty_35_state = false;
bool empty_34_state = false;

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

// Create device definitions for all 24 relays
SentientDeviceDef dev_gear_24v(naming::DEV_GEAR_24V, naming::FRIENDLY_GEAR_24V, "relay", power_commands, 2);
SentientDeviceDef dev_gear_12v(naming::DEV_GEAR_12V, naming::FRIENDLY_GEAR_12V, "relay", power_commands, 2);
SentientDeviceDef dev_gear_5v(naming::DEV_GEAR_5V, naming::FRIENDLY_GEAR_5V, "relay", power_commands, 2);
SentientDeviceDef dev_floor_24v(naming::DEV_FLOOR_24V, naming::FRIENDLY_FLOOR_24V, "relay", power_commands, 2);
SentientDeviceDef dev_floor_12v(naming::DEV_FLOOR_12V, naming::FRIENDLY_FLOOR_12V, "relay", power_commands, 2);
SentientDeviceDef dev_floor_5v(naming::DEV_FLOOR_5V, naming::FRIENDLY_FLOOR_5V, "relay", power_commands, 2);
SentientDeviceDef dev_riddle_rpi_5v(naming::DEV_RIDDLE_RPI_5V, naming::FRIENDLY_RIDDLE_RPI_5V, "relay", power_commands, 2);
SentientDeviceDef dev_riddle_rpi_12v(naming::DEV_RIDDLE_RPI_12V, naming::FRIENDLY_RIDDLE_RPI_12V, "relay", power_commands, 2);
SentientDeviceDef dev_riddle_5v(naming::DEV_RIDDLE_5V, naming::FRIENDLY_RIDDLE_5V, "relay", power_commands, 2);
SentientDeviceDef dev_boiler_room_subpanel_24v(naming::DEV_BOILER_ROOM_SUBPANEL_24V, naming::FRIENDLY_BOILER_ROOM_SUBPANEL_24V, "relay", power_commands, 2);
SentientDeviceDef dev_boiler_room_subpanel_12v(naming::DEV_BOILER_ROOM_SUBPANEL_12V, naming::FRIENDLY_BOILER_ROOM_SUBPANEL_12V, "relay", power_commands, 2);
SentientDeviceDef dev_boiler_room_subpanel_5v(naming::DEV_BOILER_ROOM_SUBPANEL_5V, naming::FRIENDLY_BOILER_ROOM_SUBPANEL_5V, "relay", power_commands, 2);
SentientDeviceDef dev_lab_room_subpanel_24v(naming::DEV_LAB_ROOM_SUBPANEL_24V, naming::FRIENDLY_LAB_ROOM_SUBPANEL_24V, "relay", power_commands, 2);
SentientDeviceDef dev_lab_room_subpanel_12v(naming::DEV_LAB_ROOM_SUBPANEL_12V, naming::FRIENDLY_LAB_ROOM_SUBPANEL_12V, "relay", power_commands, 2);
SentientDeviceDef dev_lab_room_subpanel_5v(naming::DEV_LAB_ROOM_SUBPANEL_5V, naming::FRIENDLY_LAB_ROOM_SUBPANEL_5V, "relay", power_commands, 2);
SentientDeviceDef dev_study_room_subpanel_24v(naming::DEV_STUDY_ROOM_SUBPANEL_24V, naming::FRIENDLY_STUDY_ROOM_SUBPANEL_24V, "relay", power_commands, 2);
SentientDeviceDef dev_study_room_subpanel_12v(naming::DEV_STUDY_ROOM_SUBPANEL_12V, naming::FRIENDLY_STUDY_ROOM_SUBPANEL_12V, "relay", power_commands, 2);
SentientDeviceDef dev_study_room_subpanel_5v(naming::DEV_STUDY_ROOM_SUBPANEL_5V, naming::FRIENDLY_STUDY_ROOM_SUBPANEL_5V, "relay", power_commands, 2);
SentientDeviceDef dev_gun_drawers_24v(naming::DEV_GUN_DRAWERS_24V, naming::FRIENDLY_GUN_DRAWERS_24V, "relay", power_commands, 2);
SentientDeviceDef dev_gun_drawers_12v(naming::DEV_GUN_DRAWERS_12V, naming::FRIENDLY_GUN_DRAWERS_12V, "relay", power_commands, 2);
SentientDeviceDef dev_gun_drawers_5v(naming::DEV_GUN_DRAWERS_5V, naming::FRIENDLY_GUN_DRAWERS_5V, "relay", power_commands, 2);
SentientDeviceDef dev_keys_5v(naming::DEV_KEYS_5V, naming::FRIENDLY_KEYS_5V, "relay", power_commands, 2);
SentientDeviceDef dev_empty_35(naming::DEV_EMPTY_35, naming::FRIENDLY_EMPTY_35, "relay", power_commands, 2);
SentientDeviceDef dev_empty_34(naming::DEV_EMPTY_34, naming::FRIENDLY_EMPTY_34, "relay", power_commands, 2);
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

    Serial.println(F("=== Power Control Lower Right v2.0.1 - STATELESS MODE ==="));
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
    pinMode(gear_24v_pin, OUTPUT);
    pinMode(gear_12v_pin, OUTPUT);
    pinMode(gear_5v_pin, OUTPUT);
    pinMode(floor_24v_pin, OUTPUT);
    pinMode(floor_12v_pin, OUTPUT);
    pinMode(floor_5v_pin, OUTPUT);
    pinMode(riddle_rpi_5v_pin, OUTPUT);
    pinMode(riddle_rpi_12v_pin, OUTPUT);
    pinMode(riddle_5v_pin, OUTPUT);
    pinMode(boiler_room_subpanel_24v_pin, OUTPUT);
    pinMode(boiler_room_subpanel_12v_pin, OUTPUT);
    pinMode(boiler_room_subpanel_5v_pin, OUTPUT);
    pinMode(lab_room_subpanel_24v_pin, OUTPUT);
    pinMode(lab_room_subpanel_12v_pin, OUTPUT);
    pinMode(lab_room_subpanel_5v_pin, OUTPUT);
    pinMode(study_room_subpanel_24v_pin, OUTPUT);
    pinMode(study_room_subpanel_12v_pin, OUTPUT);
    pinMode(study_room_subpanel_5v_pin, OUTPUT);
    pinMode(gun_drawers_24v_pin, OUTPUT);
    pinMode(gun_drawers_12v_pin, OUTPUT);
    pinMode(gun_drawers_5v_pin, OUTPUT);
    pinMode(keys_5v_pin, OUTPUT);
    pinMode(empty_35_pin, OUTPUT);
    pinMode(empty_34_pin, OUTPUT);

    // Set all relays to OFF
    digitalWrite(gear_24v_pin, LOW);
    digitalWrite(gear_12v_pin, LOW);
    digitalWrite(gear_5v_pin, LOW);
    digitalWrite(floor_24v_pin, LOW);
    digitalWrite(floor_12v_pin, LOW);
    digitalWrite(floor_5v_pin, LOW);
    digitalWrite(riddle_rpi_5v_pin, LOW);
    digitalWrite(riddle_rpi_12v_pin, LOW);
    digitalWrite(riddle_5v_pin, LOW);
    digitalWrite(boiler_room_subpanel_24v_pin, LOW);
    digitalWrite(boiler_room_subpanel_12v_pin, LOW);
    digitalWrite(boiler_room_subpanel_5v_pin, LOW);
    digitalWrite(lab_room_subpanel_24v_pin, LOW);
    digitalWrite(lab_room_subpanel_12v_pin, LOW);
    digitalWrite(lab_room_subpanel_5v_pin, LOW);
    digitalWrite(study_room_subpanel_24v_pin, LOW);
    digitalWrite(study_room_subpanel_12v_pin, LOW);
    digitalWrite(study_room_subpanel_5v_pin, LOW);
    digitalWrite(gun_drawers_24v_pin, LOW);
    digitalWrite(gun_drawers_12v_pin, LOW);
    digitalWrite(gun_drawers_5v_pin, LOW);
    digitalWrite(keys_5v_pin, LOW);
    digitalWrite(empty_35_pin, LOW);
    digitalWrite(empty_34_pin, LOW);

    Serial.println(F("[PowerCtrl] All 24 relays initialized to OFF"));

    // Register all devices
    Serial.println(F("[PowerCtrl] Registering devices..."));
    deviceRegistry.addDevice(&dev_gear_24v);
    deviceRegistry.addDevice(&dev_gear_12v);
    deviceRegistry.addDevice(&dev_gear_5v);
    deviceRegistry.addDevice(&dev_floor_24v);
    deviceRegistry.addDevice(&dev_floor_12v);
    deviceRegistry.addDevice(&dev_floor_5v);
    deviceRegistry.addDevice(&dev_riddle_rpi_5v);
    deviceRegistry.addDevice(&dev_riddle_rpi_12v);
    deviceRegistry.addDevice(&dev_riddle_5v);
    deviceRegistry.addDevice(&dev_boiler_room_subpanel_24v);
    deviceRegistry.addDevice(&dev_boiler_room_subpanel_12v);
    deviceRegistry.addDevice(&dev_boiler_room_subpanel_5v);
    deviceRegistry.addDevice(&dev_lab_room_subpanel_24v);
    deviceRegistry.addDevice(&dev_lab_room_subpanel_12v);
    deviceRegistry.addDevice(&dev_lab_room_subpanel_5v);
    deviceRegistry.addDevice(&dev_study_room_subpanel_24v);
    deviceRegistry.addDevice(&dev_study_room_subpanel_12v);
    deviceRegistry.addDevice(&dev_study_room_subpanel_5v);
    deviceRegistry.addDevice(&dev_gun_drawers_24v);
    deviceRegistry.addDevice(&dev_gun_drawers_12v);
    deviceRegistry.addDevice(&dev_gun_drawers_5v);
    deviceRegistry.addDevice(&dev_keys_5v);
    deviceRegistry.addDevice(&dev_empty_35);
    deviceRegistry.addDevice(&dev_empty_34);
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
        if (device == naming::DEV_GEAR_24V)
        {
            if (command == "power_on")
                set_relay_state(gear_24v_pin, true, gear_24v_state, "Gear 24V", naming::DEV_GEAR_24V, "power_on");
            else if (command == "power_off")
                set_relay_state(gear_24v_pin, false, gear_24v_state, "Gear 24V", naming::DEV_GEAR_24V, "power_off");
        }
        else if (device == naming::DEV_GEAR_12V)
        {
            if (command == "power_on")
                set_relay_state(gear_12v_pin, true, gear_12v_state, "Gear 12V", naming::DEV_GEAR_12V, "power_on");
            else if (command == "power_off")
                set_relay_state(gear_12v_pin, false, gear_12v_state, "Gear 12V", naming::DEV_GEAR_12V, "power_off");
        }
        else if (device == naming::DEV_GEAR_5V)
        {
            if (command == "power_on")
                set_relay_state(gear_5v_pin, true, gear_5v_state, "Gear 5V", naming::DEV_GEAR_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(gear_5v_pin, false, gear_5v_state, "Gear 5V", naming::DEV_GEAR_5V, "power_off");
        }
        else if (device == naming::DEV_FLOOR_24V)
        {
            if (command == "power_on")
                set_relay_state(floor_24v_pin, true, floor_24v_state, "Floor 24V", naming::DEV_FLOOR_24V, "power_on");
            else if (command == "power_off")
                set_relay_state(floor_24v_pin, false, floor_24v_state, "Floor 24V", naming::DEV_FLOOR_24V, "power_off");
        }
        else if (device == naming::DEV_FLOOR_12V)
        {
            if (command == "power_on")
                set_relay_state(floor_12v_pin, true, floor_12v_state, "Floor 12V", naming::DEV_FLOOR_12V, "power_on");
            else if (command == "power_off")
                set_relay_state(floor_12v_pin, false, floor_12v_state, "Floor 12V", naming::DEV_FLOOR_12V, "power_off");
        }
        else if (device == naming::DEV_FLOOR_5V)
        {
            if (command == "power_on")
                set_relay_state(floor_5v_pin, true, floor_5v_state, "Floor 5V", naming::DEV_FLOOR_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(floor_5v_pin, false, floor_5v_state, "Floor 5V", naming::DEV_FLOOR_5V, "power_off");
        }
        else if (device == naming::DEV_RIDDLE_RPI_5V)
        {
            if (command == "power_on")
                set_relay_state(riddle_rpi_5v_pin, true, riddle_rpi_5v_state, "Riddle RPi 5V", naming::DEV_RIDDLE_RPI_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(riddle_rpi_5v_pin, false, riddle_rpi_5v_state, "Riddle RPi 5V", naming::DEV_RIDDLE_RPI_5V, "power_off");
        }
        else if (device == naming::DEV_RIDDLE_RPI_12V)
        {
            if (command == "power_on")
                set_relay_state(riddle_rpi_12v_pin, true, riddle_rpi_12v_state, "Riddle RPi 12V", naming::DEV_RIDDLE_RPI_12V, "power_on");
            else if (command == "power_off")
                set_relay_state(riddle_rpi_12v_pin, false, riddle_rpi_12v_state, "Riddle RPi 12V", naming::DEV_RIDDLE_RPI_12V, "power_off");
        }
        else if (device == naming::DEV_RIDDLE_5V)
        {
            if (command == "power_on")
                set_relay_state(riddle_5v_pin, true, riddle_5v_state, "Riddle 5V", naming::DEV_RIDDLE_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(riddle_5v_pin, false, riddle_5v_state, "Riddle 5V", naming::DEV_RIDDLE_5V, "power_off");
        }
        else if (device == naming::DEV_BOILER_ROOM_SUBPANEL_24V)
        {
            if (command == "power_on")
                set_relay_state(boiler_room_subpanel_24v_pin, true, boiler_room_subpanel_24v_state, "Boiler Subpanel 24V", naming::DEV_BOILER_ROOM_SUBPANEL_24V, "power_on");
            else if (command == "power_off")
                set_relay_state(boiler_room_subpanel_24v_pin, false, boiler_room_subpanel_24v_state, "Boiler Subpanel 24V", naming::DEV_BOILER_ROOM_SUBPANEL_24V, "power_off");
        }
        else if (device == naming::DEV_BOILER_ROOM_SUBPANEL_12V)
        {
            if (command == "power_on")
                set_relay_state(boiler_room_subpanel_12v_pin, true, boiler_room_subpanel_12v_state, "Boiler Subpanel 12V", naming::DEV_BOILER_ROOM_SUBPANEL_12V, "power_on");
            else if (command == "power_off")
                set_relay_state(boiler_room_subpanel_12v_pin, false, boiler_room_subpanel_12v_state, "Boiler Subpanel 12V", naming::DEV_BOILER_ROOM_SUBPANEL_12V, "power_off");
        }
        else if (device == naming::DEV_BOILER_ROOM_SUBPANEL_5V)
        {
            if (command == "power_on")
                set_relay_state(boiler_room_subpanel_5v_pin, true, boiler_room_subpanel_5v_state, "Boiler Subpanel 5V", naming::DEV_BOILER_ROOM_SUBPANEL_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(boiler_room_subpanel_5v_pin, false, boiler_room_subpanel_5v_state, "Boiler Subpanel 5V", naming::DEV_BOILER_ROOM_SUBPANEL_5V, "power_off");
        }
        else if (device == naming::DEV_LAB_ROOM_SUBPANEL_24V)
        {
            if (command == "power_on")
                set_relay_state(lab_room_subpanel_24v_pin, true, lab_room_subpanel_24v_state, "Lab Subpanel 24V", naming::DEV_LAB_ROOM_SUBPANEL_24V, "power_on");
            else if (command == "power_off")
                set_relay_state(lab_room_subpanel_24v_pin, false, lab_room_subpanel_24v_state, "Lab Subpanel 24V", naming::DEV_LAB_ROOM_SUBPANEL_24V, "power_off");
        }
        else if (device == naming::DEV_LAB_ROOM_SUBPANEL_12V)
        {
            if (command == "power_on")
                set_relay_state(lab_room_subpanel_12v_pin, true, lab_room_subpanel_12v_state, "Lab Subpanel 12V", naming::DEV_LAB_ROOM_SUBPANEL_12V, "power_on");
            else if (command == "power_off")
                set_relay_state(lab_room_subpanel_12v_pin, false, lab_room_subpanel_12v_state, "Lab Subpanel 12V", naming::DEV_LAB_ROOM_SUBPANEL_12V, "power_off");
        }
        else if (device == naming::DEV_LAB_ROOM_SUBPANEL_5V)
        {
            if (command == "power_on")
                set_relay_state(lab_room_subpanel_5v_pin, true, lab_room_subpanel_5v_state, "Lab Subpanel 5V", naming::DEV_LAB_ROOM_SUBPANEL_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(lab_room_subpanel_5v_pin, false, lab_room_subpanel_5v_state, "Lab Subpanel 5V", naming::DEV_LAB_ROOM_SUBPANEL_5V, "power_off");
        }
        else if (device == naming::DEV_STUDY_ROOM_SUBPANEL_24V)
        {
            if (command == "power_on")
                set_relay_state(study_room_subpanel_24v_pin, true, study_room_subpanel_24v_state, "Study Subpanel 24V", naming::DEV_STUDY_ROOM_SUBPANEL_24V, "power_on");
            else if (command == "power_off")
                set_relay_state(study_room_subpanel_24v_pin, false, study_room_subpanel_24v_state, "Study Subpanel 24V", naming::DEV_STUDY_ROOM_SUBPANEL_24V, "power_off");
        }
        else if (device == naming::DEV_STUDY_ROOM_SUBPANEL_12V)
        {
            if (command == "power_on")
                set_relay_state(study_room_subpanel_12v_pin, true, study_room_subpanel_12v_state, "Study Subpanel 12V", naming::DEV_STUDY_ROOM_SUBPANEL_12V, "power_on");
            else if (command == "power_off")
                set_relay_state(study_room_subpanel_12v_pin, false, study_room_subpanel_12v_state, "Study Subpanel 12V", naming::DEV_STUDY_ROOM_SUBPANEL_12V, "power_off");
        }
        else if (device == naming::DEV_STUDY_ROOM_SUBPANEL_5V)
        {
            if (command == "power_on")
                set_relay_state(study_room_subpanel_5v_pin, true, study_room_subpanel_5v_state, "Study Subpanel 5V", naming::DEV_STUDY_ROOM_SUBPANEL_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(study_room_subpanel_5v_pin, false, study_room_subpanel_5v_state, "Study Subpanel 5V", naming::DEV_STUDY_ROOM_SUBPANEL_5V, "power_off");
        }
        else if (device == naming::DEV_GUN_DRAWERS_24V)
        {
            if (command == "power_on")
                set_relay_state(gun_drawers_24v_pin, true, gun_drawers_24v_state, "Gun Drawers 24V", naming::DEV_GUN_DRAWERS_24V, "power_on");
            else if (command == "power_off")
                set_relay_state(gun_drawers_24v_pin, false, gun_drawers_24v_state, "Gun Drawers 24V", naming::DEV_GUN_DRAWERS_24V, "power_off");
        }
        else if (device == naming::DEV_GUN_DRAWERS_12V)
        {
            if (command == "power_on")
                set_relay_state(gun_drawers_12v_pin, true, gun_drawers_12v_state, "Gun Drawers 12V", naming::DEV_GUN_DRAWERS_12V, "power_on");
            else if (command == "power_off")
                set_relay_state(gun_drawers_12v_pin, false, gun_drawers_12v_state, "Gun Drawers 12V", naming::DEV_GUN_DRAWERS_12V, "power_off");
        }
        else if (device == naming::DEV_GUN_DRAWERS_5V)
        {
            if (command == "power_on")
                set_relay_state(gun_drawers_5v_pin, true, gun_drawers_5v_state, "Gun Drawers 5V", naming::DEV_GUN_DRAWERS_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(gun_drawers_5v_pin, false, gun_drawers_5v_state, "Gun Drawers 5V", naming::DEV_GUN_DRAWERS_5V, "power_off");
        }
        else if (device == naming::DEV_KEYS_5V)
        {
            if (command == "power_on")
                set_relay_state(keys_5v_pin, true, keys_5v_state, "Keys 5V", naming::DEV_KEYS_5V, "power_on");
            else if (command == "power_off")
                set_relay_state(keys_5v_pin, false, keys_5v_state, "Keys 5V", naming::DEV_KEYS_5V, "power_off");
        }
        else if (device == naming::DEV_EMPTY_35)
        {
            if (command == "power_on")
                set_relay_state(empty_35_pin, true, empty_35_state, "Empty 35", naming::DEV_EMPTY_35, "power_on");
            else if (command == "power_off")
                set_relay_state(empty_35_pin, false, empty_35_state, "Empty 35", naming::DEV_EMPTY_35, "power_off");
        }
        else if (device == naming::DEV_EMPTY_34)
        {
            if (command == "power_on")
                set_relay_state(empty_34_pin, true, empty_34_state, "Empty 34", naming::DEV_EMPTY_34, "power_on");
            else if (command == "power_off")
                set_relay_state(empty_34_pin, false, empty_34_state, "Empty 34", naming::DEV_EMPTY_34, "power_off");
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
    set_relay_state(gear_24v_pin, true, gear_24v_state, "Gear 24V", naming::DEV_GEAR_24V, nullptr);
    set_relay_state(gear_12v_pin, true, gear_12v_state, "Gear 12V", naming::DEV_GEAR_12V, nullptr);
    set_relay_state(gear_5v_pin, true, gear_5v_state, "Gear 5V", naming::DEV_GEAR_5V, nullptr);
    set_relay_state(floor_24v_pin, true, floor_24v_state, "Floor 24V", naming::DEV_FLOOR_24V, nullptr);
    set_relay_state(floor_12v_pin, true, floor_12v_state, "Floor 12V", naming::DEV_FLOOR_12V, nullptr);
    set_relay_state(floor_5v_pin, true, floor_5v_state, "Floor 5V", naming::DEV_FLOOR_5V, nullptr);
    set_relay_state(riddle_rpi_5v_pin, true, riddle_rpi_5v_state, "Riddle RPi 5V", naming::DEV_RIDDLE_RPI_5V, nullptr);
    set_relay_state(riddle_rpi_12v_pin, true, riddle_rpi_12v_state, "Riddle RPi 12V", naming::DEV_RIDDLE_RPI_12V, nullptr);
    set_relay_state(riddle_5v_pin, true, riddle_5v_state, "Riddle 5V", naming::DEV_RIDDLE_5V, nullptr);
    set_relay_state(boiler_room_subpanel_24v_pin, true, boiler_room_subpanel_24v_state, "Boiler Room Subpanel 24V", naming::DEV_BOILER_ROOM_SUBPANEL_24V, nullptr);
    set_relay_state(boiler_room_subpanel_12v_pin, true, boiler_room_subpanel_12v_state, "Boiler Room Subpanel 12V", naming::DEV_BOILER_ROOM_SUBPANEL_12V, nullptr);
    set_relay_state(boiler_room_subpanel_5v_pin, true, boiler_room_subpanel_5v_state, "Boiler Room Subpanel 5V", naming::DEV_BOILER_ROOM_SUBPANEL_5V, nullptr);
    set_relay_state(lab_room_subpanel_24v_pin, true, lab_room_subpanel_24v_state, "Lab Room Subpanel 24V", naming::DEV_LAB_ROOM_SUBPANEL_24V, nullptr);
    set_relay_state(lab_room_subpanel_12v_pin, true, lab_room_subpanel_12v_state, "Lab Room Subpanel 12V", naming::DEV_LAB_ROOM_SUBPANEL_12V, nullptr);
    set_relay_state(lab_room_subpanel_5v_pin, true, lab_room_subpanel_5v_state, "Lab Room Subpanel 5V", naming::DEV_LAB_ROOM_SUBPANEL_5V, nullptr);
    set_relay_state(study_room_subpanel_24v_pin, true, study_room_subpanel_24v_state, "Study Room Subpanel 24V", naming::DEV_STUDY_ROOM_SUBPANEL_24V, nullptr);
    set_relay_state(study_room_subpanel_12v_pin, true, study_room_subpanel_12v_state, "Study Room Subpanel 12V", naming::DEV_STUDY_ROOM_SUBPANEL_12V, nullptr);
    set_relay_state(study_room_subpanel_5v_pin, true, study_room_subpanel_5v_state, "Study Room Subpanel 5V", naming::DEV_STUDY_ROOM_SUBPANEL_5V, nullptr);
    set_relay_state(gun_drawers_24v_pin, true, gun_drawers_24v_state, "Gun Drawers 24V", naming::DEV_GUN_DRAWERS_24V, nullptr);
    set_relay_state(gun_drawers_12v_pin, true, gun_drawers_12v_state, "Gun Drawers 12V", naming::DEV_GUN_DRAWERS_12V, nullptr);
    set_relay_state(gun_drawers_5v_pin, true, gun_drawers_5v_state, "Gun Drawers 5V", naming::DEV_GUN_DRAWERS_5V, nullptr);
    set_relay_state(keys_5v_pin, true, keys_5v_state, "Keys 5V", naming::DEV_KEYS_5V, nullptr);
    set_relay_state(empty_35_pin, true, empty_35_state, "Empty 35", naming::DEV_EMPTY_35, nullptr);
    set_relay_state(empty_34_pin, true, empty_34_state, "Empty 34", naming::DEV_EMPTY_34, nullptr);

    Serial.println(F("[PowerCtrl] All relays powered ON"));
}

void all_relays_off()
{
    set_relay_state(gear_24v_pin, false, gear_24v_state, "Gear 24V", naming::DEV_GEAR_24V, nullptr);
    set_relay_state(gear_12v_pin, false, gear_12v_state, "Gear 12V", naming::DEV_GEAR_12V, nullptr);
    set_relay_state(gear_5v_pin, false, gear_5v_state, "Gear 5V", naming::DEV_GEAR_5V, nullptr);
    set_relay_state(floor_24v_pin, false, floor_24v_state, "Floor 24V", naming::DEV_FLOOR_24V, nullptr);
    set_relay_state(floor_12v_pin, false, floor_12v_state, "Floor 12V", naming::DEV_FLOOR_12V, nullptr);
    set_relay_state(floor_5v_pin, false, floor_5v_state, "Floor 5V", naming::DEV_FLOOR_5V, nullptr);
    set_relay_state(riddle_rpi_5v_pin, false, riddle_rpi_5v_state, "Riddle RPi 5V", naming::DEV_RIDDLE_RPI_5V, nullptr);
    set_relay_state(riddle_rpi_12v_pin, false, riddle_rpi_12v_state, "Riddle RPi 12V", naming::DEV_RIDDLE_RPI_12V, nullptr);
    set_relay_state(riddle_5v_pin, false, riddle_5v_state, "Riddle 5V", naming::DEV_RIDDLE_5V, nullptr);
    set_relay_state(boiler_room_subpanel_24v_pin, false, boiler_room_subpanel_24v_state, "Boiler Room Subpanel 24V", naming::DEV_BOILER_ROOM_SUBPANEL_24V, nullptr);
    set_relay_state(boiler_room_subpanel_12v_pin, false, boiler_room_subpanel_12v_state, "Boiler Room Subpanel 12V", naming::DEV_BOILER_ROOM_SUBPANEL_12V, nullptr);
    set_relay_state(boiler_room_subpanel_5v_pin, false, boiler_room_subpanel_5v_state, "Boiler Room Subpanel 5V", naming::DEV_BOILER_ROOM_SUBPANEL_5V, nullptr);
    set_relay_state(lab_room_subpanel_24v_pin, false, lab_room_subpanel_24v_state, "Lab Room Subpanel 24V", naming::DEV_LAB_ROOM_SUBPANEL_24V, nullptr);
    set_relay_state(lab_room_subpanel_12v_pin, false, lab_room_subpanel_12v_state, "Lab Room Subpanel 12V", naming::DEV_LAB_ROOM_SUBPANEL_12V, nullptr);
    set_relay_state(lab_room_subpanel_5v_pin, false, lab_room_subpanel_5v_state, "Lab Room Subpanel 5V", naming::DEV_LAB_ROOM_SUBPANEL_5V, nullptr);
    set_relay_state(study_room_subpanel_24v_pin, false, study_room_subpanel_24v_state, "Study Room Subpanel 24V", naming::DEV_STUDY_ROOM_SUBPANEL_24V, nullptr);
    set_relay_state(study_room_subpanel_12v_pin, false, study_room_subpanel_12v_state, "Study Room Subpanel 12V", naming::DEV_STUDY_ROOM_SUBPANEL_12V, nullptr);
    set_relay_state(study_room_subpanel_5v_pin, false, study_room_subpanel_5v_state, "Study Room Subpanel 5V", naming::DEV_STUDY_ROOM_SUBPANEL_5V, nullptr);
    set_relay_state(gun_drawers_24v_pin, false, gun_drawers_24v_state, "Gun Drawers 24V", naming::DEV_GUN_DRAWERS_24V, nullptr);
    set_relay_state(gun_drawers_12v_pin, false, gun_drawers_12v_state, "Gun Drawers 12V", naming::DEV_GUN_DRAWERS_12V, nullptr);
    set_relay_state(gun_drawers_5v_pin, false, gun_drawers_5v_state, "Gun Drawers 5V", naming::DEV_GUN_DRAWERS_5V, nullptr);
    set_relay_state(keys_5v_pin, false, keys_5v_state, "Keys 5V", naming::DEV_KEYS_5V, nullptr);
    set_relay_state(empty_35_pin, false, empty_35_state, "Empty 35", naming::DEV_EMPTY_35, nullptr);
    set_relay_state(empty_34_pin, false, empty_34_state, "Empty 34", naming::DEV_EMPTY_34, nullptr);

    Serial.println(F("[PowerCtrl] All relays powered OFF"));
}

void emergency_power_off()
{
    Serial.println(F("[PowerCtrl] !!! EMERGENCY POWER OFF !!!"));

    // Immediately cut all power
    digitalWrite(gear_24v_pin, LOW);
    digitalWrite(gear_12v_pin, LOW);
    digitalWrite(gear_5v_pin, LOW);
    digitalWrite(floor_24v_pin, LOW);
    digitalWrite(floor_12v_pin, LOW);
    digitalWrite(floor_5v_pin, LOW);
    digitalWrite(riddle_rpi_5v_pin, LOW);
    digitalWrite(riddle_rpi_12v_pin, LOW);
    digitalWrite(riddle_5v_pin, LOW);
    digitalWrite(boiler_room_subpanel_24v_pin, LOW);
    digitalWrite(boiler_room_subpanel_12v_pin, LOW);
    digitalWrite(boiler_room_subpanel_5v_pin, LOW);
    digitalWrite(lab_room_subpanel_24v_pin, LOW);
    digitalWrite(lab_room_subpanel_12v_pin, LOW);
    digitalWrite(lab_room_subpanel_5v_pin, LOW);
    digitalWrite(study_room_subpanel_24v_pin, LOW);
    digitalWrite(study_room_subpanel_12v_pin, LOW);
    digitalWrite(study_room_subpanel_5v_pin, LOW);
    digitalWrite(gun_drawers_24v_pin, LOW);
    digitalWrite(gun_drawers_12v_pin, LOW);
    digitalWrite(gun_drawers_5v_pin, LOW);
    digitalWrite(keys_5v_pin, LOW);
    digitalWrite(empty_35_pin, LOW);
    digitalWrite(empty_34_pin, LOW);

    // Update all state variables
    gear_24v_state = false;
    gear_12v_state = false;
    gear_5v_state = false;
    floor_24v_state = false;
    floor_12v_state = false;
    floor_5v_state = false;
    riddle_rpi_5v_state = false;
    riddle_rpi_12v_state = false;
    riddle_5v_state = false;
    boiler_room_subpanel_24v_state = false;
    boiler_room_subpanel_12v_state = false;
    boiler_room_subpanel_5v_state = false;
    lab_room_subpanel_24v_state = false;
    lab_room_subpanel_12v_state = false;
    lab_room_subpanel_5v_state = false;
    study_room_subpanel_24v_state = false;
    study_room_subpanel_12v_state = false;
    study_room_subpanel_5v_state = false;
    gun_drawers_24v_state = false;
    gun_drawers_12v_state = false;
    gun_drawers_5v_state = false;
    keys_5v_state = false;
    empty_35_state = false;
    empty_34_state = false;

    // Publish individual relay OFF states for real-time UI updates
    if (mqtt.isConnected())
    {
        publish_relay_state(naming::DEV_GEAR_24V, false);
        publish_relay_state(naming::DEV_GEAR_12V, false);
        publish_relay_state(naming::DEV_GEAR_5V, false);
        publish_relay_state(naming::DEV_FLOOR_24V, false);
        publish_relay_state(naming::DEV_FLOOR_12V, false);
        publish_relay_state(naming::DEV_FLOOR_5V, false);
        publish_relay_state(naming::DEV_RIDDLE_RPI_5V, false);
        publish_relay_state(naming::DEV_RIDDLE_RPI_12V, false);
        publish_relay_state(naming::DEV_RIDDLE_5V, false);
        publish_relay_state(naming::DEV_BOILER_ROOM_SUBPANEL_24V, false);
        publish_relay_state(naming::DEV_BOILER_ROOM_SUBPANEL_12V, false);
        publish_relay_state(naming::DEV_BOILER_ROOM_SUBPANEL_5V, false);
        publish_relay_state(naming::DEV_LAB_ROOM_SUBPANEL_24V, false);
        publish_relay_state(naming::DEV_LAB_ROOM_SUBPANEL_12V, false);
        publish_relay_state(naming::DEV_LAB_ROOM_SUBPANEL_5V, false);
        publish_relay_state(naming::DEV_STUDY_ROOM_SUBPANEL_24V, false);
        publish_relay_state(naming::DEV_STUDY_ROOM_SUBPANEL_12V, false);
        publish_relay_state(naming::DEV_STUDY_ROOM_SUBPANEL_5V, false);
        publish_relay_state(naming::DEV_GUN_DRAWERS_24V, false);
        publish_relay_state(naming::DEV_GUN_DRAWERS_12V, false);
        publish_relay_state(naming::DEV_GUN_DRAWERS_5V, false);
        publish_relay_state(naming::DEV_KEYS_5V, false);
        publish_relay_state(naming::DEV_EMPTY_35, false);
        publish_relay_state(naming::DEV_EMPTY_34, false);
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
    doc["gear_24v"] = gear_24v_state;
    doc["gear_12v"] = gear_12v_state;
    doc["gear_5v"] = gear_5v_state;
    doc["floor_24v"] = floor_24v_state;
    doc["floor_12v"] = floor_12v_state;
    doc["floor_5v"] = floor_5v_state;
    doc["riddle_rpi_5v"] = riddle_rpi_5v_state;
    doc["riddle_rpi_12v"] = riddle_rpi_12v_state;
    doc["riddle_5v"] = riddle_5v_state;
    doc["boiler_room_subpanel_24v"] = boiler_room_subpanel_24v_state;
    doc["boiler_room_subpanel_12v"] = boiler_room_subpanel_12v_state;
    doc["boiler_room_subpanel_5v"] = boiler_room_subpanel_5v_state;
    doc["lab_room_subpanel_24v"] = lab_room_subpanel_24v_state;
    doc["lab_room_subpanel_12v"] = lab_room_subpanel_12v_state;
    doc["lab_room_subpanel_5v"] = lab_room_subpanel_5v_state;
    doc["study_room_subpanel_24v"] = study_room_subpanel_24v_state;
    doc["study_room_subpanel_12v"] = study_room_subpanel_12v_state;
    doc["study_room_subpanel_5v"] = study_room_subpanel_5v_state;
    doc["gun_drawers_24v"] = gun_drawers_24v_state;
    doc["gun_drawers_12v"] = gun_drawers_12v_state;
    doc["gun_drawers_5v"] = gun_drawers_5v_state;
    doc["keys_5v"] = keys_5v_state;
    doc["empty_35"] = empty_35_state;
    doc["empty_34"] = empty_34_state;
    doc["ts"] = millis();
    doc["uid"] = naming::CONTROLLER_ID;

    mqtt.publishJson(naming::CAT_STATUS, naming::ITEM_HARDWARE, doc);
}

void publish_full_status()
{
    JsonDocument doc;

    // All relay states
    doc["gear_24v"] = gear_24v_state;
    doc["gear_12v"] = gear_12v_state;
    doc["gear_5v"] = gear_5v_state;
    doc["floor_24v"] = floor_24v_state;
    doc["floor_12v"] = floor_12v_state;
    doc["floor_5v"] = floor_5v_state;
    doc["riddle_rpi_5v"] = riddle_rpi_5v_state;
    doc["riddle_rpi_12v"] = riddle_rpi_12v_state;
    doc["riddle_5v"] = riddle_5v_state;
    doc["boiler_room_subpanel_24v"] = boiler_room_subpanel_24v_state;
    doc["boiler_room_subpanel_12v"] = boiler_room_subpanel_12v_state;
    doc["boiler_room_subpanel_5v"] = boiler_room_subpanel_5v_state;
    doc["lab_room_subpanel_24v"] = lab_room_subpanel_24v_state;
    doc["lab_room_subpanel_12v"] = lab_room_subpanel_12v_state;
    doc["lab_room_subpanel_5v"] = lab_room_subpanel_5v_state;
    doc["study_room_subpanel_24v"] = study_room_subpanel_24v_state;
    doc["study_room_subpanel_12v"] = study_room_subpanel_12v_state;
    doc["study_room_subpanel_5v"] = study_room_subpanel_5v_state;
    doc["gun_drawers_24v"] = gun_drawers_24v_state;
    doc["gun_drawers_12v"] = gun_drawers_12v_state;
    doc["gun_drawers_5v"] = gun_drawers_5v_state;
    doc["keys_5v"] = keys_5v_state;
    doc["empty_35"] = empty_35_state;
    doc["empty_34"] = empty_34_state;

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

    // Read and report all 24 relays
    gear_24v_state = digitalRead(gear_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_GEAR_24V, gear_24v_state);

    gear_12v_state = digitalRead(gear_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_GEAR_12V, gear_12v_state);

    gear_5v_state = digitalRead(gear_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_GEAR_5V, gear_5v_state);

    floor_24v_state = digitalRead(floor_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_FLOOR_24V, floor_24v_state);

    floor_12v_state = digitalRead(floor_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_FLOOR_12V, floor_12v_state);

    floor_5v_state = digitalRead(floor_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_FLOOR_5V, floor_5v_state);

    riddle_rpi_5v_state = digitalRead(riddle_rpi_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_RIDDLE_RPI_5V, riddle_rpi_5v_state);

    riddle_rpi_12v_state = digitalRead(riddle_rpi_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_RIDDLE_RPI_12V, riddle_rpi_12v_state);

    riddle_5v_state = digitalRead(riddle_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_RIDDLE_5V, riddle_5v_state);

    boiler_room_subpanel_24v_state = digitalRead(boiler_room_subpanel_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_BOILER_ROOM_SUBPANEL_24V, boiler_room_subpanel_24v_state);

    boiler_room_subpanel_12v_state = digitalRead(boiler_room_subpanel_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_BOILER_ROOM_SUBPANEL_12V, boiler_room_subpanel_12v_state);

    boiler_room_subpanel_5v_state = digitalRead(boiler_room_subpanel_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_BOILER_ROOM_SUBPANEL_5V, boiler_room_subpanel_5v_state);

    lab_room_subpanel_24v_state = digitalRead(lab_room_subpanel_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_LAB_ROOM_SUBPANEL_24V, lab_room_subpanel_24v_state);

    lab_room_subpanel_12v_state = digitalRead(lab_room_subpanel_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_LAB_ROOM_SUBPANEL_12V, lab_room_subpanel_12v_state);

    lab_room_subpanel_5v_state = digitalRead(lab_room_subpanel_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_LAB_ROOM_SUBPANEL_5V, lab_room_subpanel_5v_state);

    study_room_subpanel_24v_state = digitalRead(study_room_subpanel_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_STUDY_ROOM_SUBPANEL_24V, study_room_subpanel_24v_state);

    study_room_subpanel_12v_state = digitalRead(study_room_subpanel_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_STUDY_ROOM_SUBPANEL_12V, study_room_subpanel_12v_state);

    study_room_subpanel_5v_state = digitalRead(study_room_subpanel_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_STUDY_ROOM_SUBPANEL_5V, study_room_subpanel_5v_state);

    gun_drawers_24v_state = digitalRead(gun_drawers_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_GUN_DRAWERS_24V, gun_drawers_24v_state);

    gun_drawers_12v_state = digitalRead(gun_drawers_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_GUN_DRAWERS_12V, gun_drawers_12v_state);

    gun_drawers_5v_state = digitalRead(gun_drawers_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_GUN_DRAWERS_5V, gun_drawers_5v_state);

    keys_5v_state = digitalRead(keys_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_KEYS_5V, keys_5v_state);

    empty_35_state = digitalRead(empty_35_pin) == HIGH;
    publish_relay_state(naming::DEV_EMPTY_35, empty_35_state);

    empty_34_state = digitalRead(empty_34_pin) == HIGH;
    publish_relay_state(naming::DEV_EMPTY_34, empty_34_state);

    Serial.println(F("[PowerCtrl] === All 24 Relay States Reported ==="));

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
