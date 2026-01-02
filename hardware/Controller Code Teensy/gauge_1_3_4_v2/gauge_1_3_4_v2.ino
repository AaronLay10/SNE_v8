// ══════════════════════════════════════════════════════════════════════════════
// Gauge 1-3-4 Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Gauge 1-3-4 Controller
 *
 * Hardware:
 * - 3x Stepper motors (gauges 1, 3, 4)
 * - 3x Analog potentiometers (ball valve position sensors)
 * - AccelStepper library for smooth motion
 *
 * STATELESS ARCHITECTURE:
 * - When ACTIVE: Gauges autonomously track valve potentiometer positions
 * - When INACTIVE: Gauges move to zero position
 * - Separate MQTT topics for each gauge PSI reading
 * - EEPROM position storage for auto-zeroing on startup
 * - Manual calibration commands for fine-tuning zero position
 *
 * Gauge Assignments:
 * - Gauge 1: DIR 7, STEP 6, ENABLE 8, POT A10
 * - Gauge 3: DIR 11, STEP 10, ENABLE 12, POT A12
 * - Gauge 4: DIR 3, STEP 2, ENABLE 4, POT A11
 */

#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <SentientCapabilityManifest.h>
#include <AccelStepper.h>
#include <EEPROM.h>

#include "FirmwareMetadata.h"
#include "controller_naming.h"

// ──────────────────────────────────────────────────────────────────────────────
// MQTT Configuration
// ──────────────────────────────────────────────────────────────────────────────
const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_user = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";

// ──────────────────────────────────────────────────────────────────────────────
// Pin Definitions
// ──────────────────────────────────────────────────────────────────────────────
// Gauge 1
const int gauge_1_dir_pin = 7;
const int gauge_1_step_pin = 6;
const int gauge_1_enable_pin = 8;
const int valve_1_pot_pin = A10;

// Gauge 3
const int gauge_3_dir_pin = 11;
const int gauge_3_step_pin = 10;
const int gauge_3_enable_pin = 12;
const int valve_3_pot_pin = A12;

// Gauge 4
const int gauge_4_dir_pin = 3;
const int gauge_4_step_pin = 2;
const int gauge_4_enable_pin = 4;
const int valve_4_pot_pin = A11;

// Power LED
const int power_led_pin = 13;

// ──────────────────────────────────────────────────────────────────────────────
// Gauge Configuration
// ──────────────────────────────────────────────────────────────────────────────
// Stepper settings
const int stepper_max_speed = 700;
const int stepper_acceleration = 350;

// Gauge position range (in steps)
const int gauge_min_steps = 0;
const int gauge_max_steps = 2300;

// PSI range
const int psi_min = 0;
const int psi_max = 125;

// Valve calibration (analog values for 0 and 125 PSI)
const int valve_1_zero = 10;
const int valve_1_max = 750;
const int valve_3_zero = 15;
const int valve_3_max = 896;
const int valve_4_zero = 10;
const int valve_4_max = 960;

// Signal filtering
const int num_analog_readings = 3;
const float filter_alpha = 0.25f;
const int psi_deadband = 1;                 // PSI must change by this amount to trigger movement
const unsigned long movement_delay_ms = 75; // Minimum time between movements

// EEPROM addresses for storing last known positions
const int eeprom_addr_gauge1 = 0;
const int eeprom_addr_gauge3 = 4;
const int eeprom_addr_gauge4 = 8;

// ──────────────────────────────────────────────────────────────────────────────
// Hardware State Variables
// ──────────────────────────────────────────────────────────────────────────────
bool gauges_active = false;

// Current valve PSI readings (from potentiometers)
int valve_1_psi = 0;
int valve_3_psi = 0;
int valve_4_psi = 0;

// Current gauge needle positions (from stepper positions)
int gauge_1_psi = 0;
int gauge_3_psi = 0;
int gauge_4_psi = 0;

// Last published PSI values (for change detection)
int last_published_gauge_1_psi = -1;
int last_published_gauge_3_psi = -1;
int last_published_gauge_4_psi = -1;
int last_published_valve_1_psi = -1;
int last_published_valve_3_psi = -1;
int last_published_valve_4_psi = -1;

// Periodic sensor publishing
unsigned long last_sensor_publish_time = 0;
const unsigned long sensor_publish_interval_ms = 60000;

// Filtered analog values
float filtered_analog_1 = 0.0f;
float filtered_analog_3 = 0.0f;
float filtered_analog_4 = 0.0f;
bool filters_initialized = false;

// Movement tracking
int previous_target_psi_1 = -1;
int previous_target_psi_3 = -1;
int previous_target_psi_4 = -1;
unsigned long last_move_time_1 = 0;
unsigned long last_move_time_3 = 0;
unsigned long last_move_time_4 = 0;

// ──────────────────────────────────────────────────────────────────────────────
// Stepper Motors
// ──────────────────────────────────────────────────────────────────────────────
AccelStepper stepper_1(AccelStepper::DRIVER, gauge_1_step_pin, gauge_1_dir_pin);
AccelStepper stepper_3(AccelStepper::DRIVER, gauge_3_step_pin, gauge_3_dir_pin);
AccelStepper stepper_4(AccelStepper::DRIVER, gauge_4_step_pin, gauge_4_dir_pin);

// ============================================================================
// DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!) — Using controller_naming.h
// ============================================================================

// Gauge device commands (all gauges respond to these)
const char *gauge_commands[] = {
    naming::CMD_ACTIVATE_GAUGES,
    naming::CMD_DEACTIVATE_GAUGES,
    naming::CMD_ADJUST_GAUGE_ZERO,
    naming::CMD_SET_CURRENT_AS_ZERO};

// Sensor arrays for each gauge (valve PSI sensor)
const char *gauge_1_sensors[] = {naming::SENSOR_VALVE_1_PSI};
const char *gauge_3_sensors[] = {naming::SENSOR_VALVE_3_PSI};
const char *gauge_4_sensors[] = {naming::SENSOR_VALVE_4_PSI};

// Create device definitions (bidirectional: commands + sensors)
SentientDeviceDef dev_gauge_1(
    naming::DEV_GAUGE_1,
    naming::FRIENDLY_GAUGE_1,
    "gauge_assembly",
    gauge_commands, 4,
    gauge_1_sensors, 1);

SentientDeviceDef dev_gauge_3(
    naming::DEV_GAUGE_3,
    naming::FRIENDLY_GAUGE_3,
    "gauge_assembly",
    gauge_commands, 4,
    gauge_3_sensors, 1);

SentientDeviceDef dev_gauge_4(
    naming::DEV_GAUGE_4,
    naming::FRIENDLY_GAUGE_4,
    "gauge_assembly",
    gauge_commands, 4,
    gauge_4_sensors, 1);

// Create the device registry
SentientDeviceRegistry deviceRegistry;

// ──────────────────────────────────────────────────────────────────────────────
// Forward Declarations
// ──────────────────────────────────────────────────────────────────────────────
void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);
void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void read_valve_positions();
void move_gauges();
void check_and_publish_sensor_changes();
void publish_hardware_status();
void save_gauge_position(int gauge_number);
void load_gauge_positions();
String extract_command_value(const JsonDocument &payload);
void publish_command_acknowledgement(const char *device_id, const char *command);

// MQTT objects
SentientCapabilityManifest manifest;
SentientMQTT mqtt(build_mqtt_config());

// ============================================================================
// SECTION 2: SETUP FUNCTION
// ============================================================================

void setup()
{
    // Power LED
    pinMode(power_led_pin, OUTPUT);
    digitalWrite(power_led_pin, HIGH);

    // Serial console
    Serial.begin(115200);
    unsigned long waited = 0;
    while (!Serial && waited < 2000)
    {
        delay(10);
        waited += 10;
    }

    Serial.println();
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║      Sentient Engine - Gauge 1-3-4 Controller v2          ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.print("[Gauge 1-3-4] Firmware Version: ");
    Serial.println(firmware::VERSION);
    Serial.print("[Gauge 1-3-4] Unique ID: ");
    Serial.println(firmware::UNIQUE_ID);
    Serial.print("[Gauge 1-3-4] Controller ID: ");
    Serial.println(naming::CONTROLLER_ID);
    Serial.println();

    // ========================================
    // Initialize stepper enable pins
    // ========================================
    pinMode(gauge_1_enable_pin, OUTPUT);
    pinMode(gauge_3_enable_pin, OUTPUT);
    pinMode(gauge_4_enable_pin, OUTPUT);

    // Disable all steppers initially
    digitalWrite(gauge_1_enable_pin, HIGH);
    digitalWrite(gauge_3_enable_pin, HIGH);
    digitalWrite(gauge_4_enable_pin, HIGH);

    // ========================================
    // Configure steppers
    // ========================================
    stepper_1.setPinsInverted(true, false, false);
    stepper_3.setPinsInverted(true, false, false);
    stepper_4.setPinsInverted(false, false, false);

    stepper_1.setMaxSpeed(stepper_max_speed);
    stepper_1.setAcceleration(stepper_acceleration);
    stepper_3.setMaxSpeed(stepper_max_speed);
    stepper_3.setAcceleration(stepper_acceleration);
    stepper_4.setMaxSpeed(stepper_max_speed);
    stepper_4.setAcceleration(stepper_acceleration);

    // ========================================
    // Load last known positions from EEPROM
    // ========================================
    load_gauge_positions();

    // ========================================
    // Auto-zero gauges on startup
    // ========================================
    Serial.println("[Gauge 1-3-4] Auto-zeroing gauges...");
    stepper_1.moveTo(gauge_min_steps);
    stepper_3.moveTo(gauge_min_steps);
    stepper_4.moveTo(gauge_min_steps);

    // Wait for zero (blocking on startup)
    while (stepper_1.distanceToGo() != 0 || stepper_3.distanceToGo() != 0 || stepper_4.distanceToGo() != 0)
    {
        stepper_1.run();
        stepper_3.run();
        stepper_4.run();
    }

    Serial.println("[Gauge 1-3-4] Gauges zeroed");

    // Save zero positions
    save_gauge_position(1);
    save_gauge_position(3);
    save_gauge_position(4);

    // ========================================
    // Register Devices
    // ========================================
    Serial.println("[Gauge 1-3-4] Registering devices...");
    deviceRegistry.addDevice(&dev_gauge_1);
    deviceRegistry.addDevice(&dev_gauge_3);
    deviceRegistry.addDevice(&dev_gauge_4);
    deviceRegistry.printSummary();

    // ========================================
    // Build Capability Manifest
    // ========================================
    Serial.println("[Gauge 1-3-4] Building capability manifest...");
    build_capability_manifest();
    Serial.println("[Gauge 1-3-4] Manifest built successfully");

    // ========================================
    // Initialize MQTT
    // ========================================
    Serial.println("[Gauge 1-3-4] Initializing MQTT...");
    if (!mqtt.begin())
    {
        Serial.println("[Gauge 1-3-4] MQTT initialization failed - continuing without network");
    }
    else
    {
        Serial.println("[Gauge 1-3-4] MQTT initialization successful");
        mqtt.setCommandCallback(handle_mqtt_command);
        mqtt.setHeartbeatBuilder(build_heartbeat_payload);

        // Wait for broker connection (max 5 seconds)
        Serial.println("[Gauge 1-3-4] Waiting for broker connection...");
        unsigned long connection_start = millis();
        while (!mqtt.isConnected() && (millis() - connection_start < 5000))
        {
            mqtt.loop();
            delay(100);
        }

        if (mqtt.isConnected())
        {
            Serial.println("[Gauge 1-3-4] Broker connected!");

            // Register with Sentient system
            Serial.println("[Gauge 1-3-4] Registering with Sentient system...");
            if (manifest.publish_registration(mqtt.get_client(), naming::ROOM_ID, naming::CONTROLLER_ID))
            {
                Serial.println("[Gauge 1-3-4] Registration successful!");
            }
            else
            {
                Serial.println("[Gauge 1-3-4] Registration failed - will retry later");
            }

            // Publish initial status
            publish_hardware_status();
        }
        else
        {
            Serial.println("[Gauge 1-3-4] Broker connection timeout - will retry in main loop");
        }
    }

    Serial.println("[Gauge 1-3-4] Ready - awaiting Sentient commands");
    Serial.println();
}

// ============================================================================
// SECTION 3: LOOP FUNCTION
// ============================================================================

void loop()
{
    // 1. LISTEN - Process MQTT commands from Sentient
    mqtt.loop();

    // 2. DETECT - Read valve positions and publish changes
    read_valve_positions();
    check_and_publish_sensor_changes();

    // 3. EXECUTE - Move gauges (autonomous tracking when active)
    move_gauges();

    // Always run steppers (non-blocking)
    stepper_1.run();
    stepper_3.run();
    stepper_4.run();
}

// ============================================================================
// SECTION 4: COMMANDS AND ASSOCIATED FUNCTIONS
// ============================================================================

void handle_mqtt_command(const char *command, const JsonDocument &payload, void * /*ctx*/)
{
    String cmd(command);
    String value = extract_command_value(payload);

    Serial.print("[COMMAND] ");
    Serial.print(cmd);
    Serial.print(" = ");
    Serial.println(value);

    // ========================================
    // Activate Gauges (enable tracking)
    // ========================================
    if (cmd.equals(naming::CMD_ACTIVATE_GAUGES))
    {
        digitalWrite(gauge_1_enable_pin, LOW);
        digitalWrite(gauge_3_enable_pin, LOW);
        digitalWrite(gauge_4_enable_pin, LOW);
        gauges_active = true;

        Serial.println("[GAUGES] Activated - tracking valve positions");
        publish_hardware_status();
        publish_command_acknowledgement(naming::CONTROLLER_ID, command);
    }

    // ========================================
    // Deactivate Gauges (move to zero)
    // ========================================
    else if (cmd.equals(naming::CMD_DEACTIVATE_GAUGES))
    {
        gauges_active = false;

        // Move all gauges to zero
        stepper_1.moveTo(gauge_min_steps);
        stepper_3.moveTo(gauge_min_steps);
        stepper_4.moveTo(gauge_min_steps);

        // Reset tracking variables
        previous_target_psi_1 = -1;
        previous_target_psi_3 = -1;
        previous_target_psi_4 = -1;
        filters_initialized = false;

        Serial.println("[GAUGES] Deactivated - moving to zero");
        publish_hardware_status();
        publish_command_acknowledgement(naming::CONTROLLER_ID, command);
    }

    // ========================================
    // Adjust Gauge Zero (fine-tune position)
    // ========================================
    else if (cmd.equals(naming::CMD_ADJUST_GAUGE_ZERO))
    {
        // REQUIRED: Validate JSON parameters
        if (!payload["gauge"].is<int>() || !payload["steps"].is<int>())
        {
            Serial.println("[ERROR] adjust_gauge_zero requires 'gauge' and 'steps' parameters");
            Serial.println("[ERROR] Example: {\"gauge\": 1, \"steps\": 10}");
            return;
        }

        int gauge_num = payload["gauge"];
        int steps = payload["steps"]; // Positive = move forward, negative = move back

        switch (gauge_num)
        {
        case 1:
            stepper_1.move(steps);
            Serial.print("[CALIBRATION] Adjusting Gauge 1 by ");
            Serial.print(steps);
            Serial.println(" steps");
            break;
        case 3:
            stepper_3.move(steps);
            Serial.print("[CALIBRATION] Adjusting Gauge 3 by ");
            Serial.print(steps);
            Serial.println(" steps");
            break;
        case 4:
            stepper_4.move(steps);
            Serial.print("[CALIBRATION] Adjusting Gauge 4 by ");
            Serial.print(steps);
            Serial.println(" steps");
            break;
        default:
            Serial.print("[ERROR] Invalid gauge number: ");
            Serial.println(gauge_num);
            Serial.println("[ERROR] Valid gauge numbers: 1, 3, 4");
            return;
        }
    }

    // ========================================
    // Set Current Position as Zero
    // ========================================
    else if (cmd.equals(naming::CMD_SET_CURRENT_AS_ZERO))
    {
        // REQUIRED: Validate JSON parameter
        if (!payload["gauge"].is<int>())
        {
            Serial.println("[ERROR] set_current_as_zero requires 'gauge' parameter");
            Serial.println("[ERROR] Example: {\"gauge\": 1}");
            return;
        }

        int gauge_num = payload["gauge"];

        switch (gauge_num)
        {
        case 1:
            stepper_1.setCurrentPosition(gauge_min_steps);
            save_gauge_position(1);
            Serial.println("[CALIBRATION] Gauge 1 - current position set as zero");
            break;
        case 3:
            stepper_3.setCurrentPosition(gauge_min_steps);
            save_gauge_position(3);
            Serial.println("[CALIBRATION] Gauge 3 - current position set as zero");
            break;
        case 4:
            stepper_4.setCurrentPosition(gauge_min_steps);
            save_gauge_position(4);
            Serial.println("[CALIBRATION] Gauge 4 - current position set as zero");
            break;
        default:
            Serial.print("[ERROR] Invalid gauge number: ");
            Serial.println(gauge_num);
            Serial.println("[ERROR] Valid gauge numbers: 1, 3, 4");
            return;
        }

        publish_hardware_status();
    }

    // ========================================
    // Request Status (on-demand full state)
    // ========================================
    else if (cmd.equalsIgnoreCase("requestStatus") || cmd.equalsIgnoreCase("request_status"))
    {
        JsonDocument doc;
        doc["uid"] = naming::CONTROLLER_ID;
        doc["fw"] = firmware::VERSION;
        doc["gauges_active"] = gauges_active;
        doc["gauge_1_psi"] = gauge_1_psi;
        doc["gauge_3_psi"] = gauge_3_psi;
        doc["gauge_4_psi"] = gauge_4_psi;
        doc["valve_1_psi"] = valve_1_psi;
        doc["valve_3_psi"] = valve_3_psi;
        doc["valve_4_psi"] = valve_4_psi;
        doc["stepper_1_pos"] = stepper_1.currentPosition();
        doc["stepper_3_pos"] = stepper_3.currentPosition();
        doc["stepper_4_pos"] = stepper_4.currentPosition();
        doc["ts"] = millis();
        mqtt.publishJson(naming::CAT_STATUS, "full", doc);
        Serial.println("[STATUS] Full status published");
    }

    // ========================================
    // Reset (move to zero, disable)
    // ========================================
    else if (cmd.equalsIgnoreCase("reset"))
    {
        gauges_active = false;

        stepper_1.moveTo(gauge_min_steps);
        stepper_3.moveTo(gauge_min_steps);
        stepper_4.moveTo(gauge_min_steps);

        // Wait for movement to complete
        while (stepper_1.distanceToGo() != 0 || stepper_3.distanceToGo() != 0 || stepper_4.distanceToGo() != 0)
        {
            stepper_1.run();
            stepper_3.run();
            stepper_4.run();
        }

        // Disable motors
        digitalWrite(gauge_1_enable_pin, HIGH);
        digitalWrite(gauge_3_enable_pin, HIGH);
        digitalWrite(gauge_4_enable_pin, HIGH);

        // Save positions
        save_gauge_position(1);
        save_gauge_position(3);
        save_gauge_position(4);

        previous_target_psi_1 = -1;
        previous_target_psi_3 = -1;
        previous_target_psi_4 = -1;
        filters_initialized = false;

        publish_hardware_status();
        Serial.println("[RESET] All gauges at zero, motors disabled");
    }

    else
    {
        Serial.print("[UNKNOWN COMMAND] ");
        Serial.println(cmd);
    }
}

// ============================================================================
// SECTION 5: ALL OTHER FUNCTIONS
// ============================================================================

// ──────────────────────────────────────────────────────────────────────────────
// Capability Manifest Builder
// ──────────────────────────────────────────────────────────────────────────────
void build_capability_manifest()
{
    // Controller metadata using naming constants
    manifest.set_controller_info(
        naming::CONTROLLER_ID,
        naming::CONTROLLER_FRIENDLY_NAME,
        firmware::VERSION,
        naming::ROOM_ID,
        naming::CONTROLLER_ID);

    // Auto-generate entire manifest from device registry
    deviceRegistry.buildManifest(manifest);
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
    cfg.namespaceId = naming::CLIENT_ID;
    cfg.roomId = naming::ROOM_ID;
    cfg.controllerId = naming::CONTROLLER_ID;
    cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
    cfg.displayName = naming::CONTROLLER_FRIENDLY_NAME;
    cfg.publishJsonCapacity = 1536;
    cfg.heartbeatIntervalMs = 5000;
    cfg.autoHeartbeat = true;
#if !defined(ESP32)
    cfg.useDhcp = true;
#endif
    return cfg;
}

// ──────────────────────────────────────────────────────────────────────────────
// Heartbeat Builder
// ──────────────────────────────────────────────────────────────────────────────
bool build_heartbeat_payload(JsonDocument &doc, void * /*ctx*/)
{
    doc["uid"] = naming::CONTROLLER_ID;
    doc["fw"] = firmware::VERSION;
    doc["up"] = millis();
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// EEPROM Position Storage
// ──────────────────────────────────────────────────────────────────────────────
void save_gauge_position(int gauge_number)
{
    int position = 0;
    int address = 0;

    switch (gauge_number)
    {
    case 1:
        position = stepper_1.currentPosition();
        address = eeprom_addr_gauge1;
        break;
    case 3:
        position = stepper_3.currentPosition();
        address = eeprom_addr_gauge3;
        break;
    case 4:
        position = stepper_4.currentPosition();
        address = eeprom_addr_gauge4;
        break;
    default:
        return;
    }

    EEPROM.put(address, position);
    Serial.print("[EEPROM] Saved Gauge ");
    Serial.print(gauge_number);
    Serial.print(" position: ");
    Serial.println(position);
}

void load_gauge_positions()
{
    int position1, position3, position4;

    EEPROM.get(eeprom_addr_gauge1, position1);
    EEPROM.get(eeprom_addr_gauge3, position3);
    EEPROM.get(eeprom_addr_gauge4, position4);

    // Sanity check (if EEPROM is uninitialized, values might be garbage)
    if (position1 < -5000 || position1 > 5000)
        position1 = 0;
    if (position3 < -5000 || position3 > 5000)
        position3 = 0;
    if (position4 < -5000 || position4 > 5000)
        position4 = 0;

    stepper_1.setCurrentPosition(position1);
    stepper_3.setCurrentPosition(position3);
    stepper_4.setCurrentPosition(position4);

    Serial.println("[EEPROM] Loaded last known positions:");
    Serial.print("  Gauge 1: ");
    Serial.println(position1);
    Serial.print("  Gauge 3: ");
    Serial.println(position3);
    Serial.print("  Gauge 4: ");
    Serial.println(position4);
}

// ──────────────────────────────────────────────────────────────────────────────
// Valve Position Reading
// ──────────────────────────────────────────────────────────────────────────────
void read_valve_positions()
{
    // Average readings for noise reduction
    int sum1 = 0, sum3 = 0, sum4 = 0;
    for (int i = 0; i < num_analog_readings; i++)
    {
        sum1 += analogRead(valve_1_pot_pin);
        sum3 += analogRead(valve_3_pot_pin);
        sum4 += analogRead(valve_4_pot_pin);
    }

    float raw1 = sum1 / (float)num_analog_readings;
    float raw3 = sum3 / (float)num_analog_readings;
    float raw4 = sum4 / (float)num_analog_readings;

    // Initialize filters on first run
    if (!filters_initialized)
    {
        filtered_analog_1 = raw1;
        filtered_analog_3 = raw3;
        filtered_analog_4 = raw4;
        filters_initialized = true;
    }

    // Apply exponential moving average filter
    filtered_analog_1 = (filter_alpha * raw1) + ((1.0f - filter_alpha) * filtered_analog_1);
    filtered_analog_3 = (filter_alpha * raw3) + ((1.0f - filter_alpha) * filtered_analog_3);
    filtered_analog_4 = (filter_alpha * raw4) + ((1.0f - filter_alpha) * filtered_analog_4);

    // Convert to PSI
    valve_1_psi = map((int)filtered_analog_1, valve_1_zero, valve_1_max, psi_min, psi_max);
    valve_3_psi = map((int)filtered_analog_3, valve_3_zero, valve_3_max, psi_min, psi_max);
    valve_4_psi = map((int)filtered_analog_4, valve_4_zero, valve_4_max, psi_min, psi_max);

    // Constrain to valid range
    valve_1_psi = constrain(valve_1_psi, psi_min, psi_max);
    valve_3_psi = constrain(valve_3_psi, psi_min, psi_max);
    valve_4_psi = constrain(valve_4_psi, psi_min, psi_max);
}

// ──────────────────────────────────────────────────────────────────────────────
// Gauge Movement (Autonomous Tracking)
// ──────────────────────────────────────────────────────────────────────────────
void move_gauges()
{
    if (!gauges_active)
        return;

    unsigned long current_time = millis();

    // Move gauge 1 with deadband and time delay
    if ((abs(valve_1_psi - previous_target_psi_1) >= psi_deadband || previous_target_psi_1 == -1) &&
        (current_time - last_move_time_1 >= movement_delay_ms || previous_target_psi_1 == -1))
    {
        stepper_1.moveTo(map(valve_1_psi, psi_min, psi_max, gauge_min_steps, gauge_max_steps));
        previous_target_psi_1 = valve_1_psi;
        last_move_time_1 = current_time;
    }

    // Move gauge 3
    if ((abs(valve_3_psi - previous_target_psi_3) >= psi_deadband || previous_target_psi_3 == -1) &&
        (current_time - last_move_time_3 >= movement_delay_ms || previous_target_psi_3 == -1))
    {
        stepper_3.moveTo(map(valve_3_psi, psi_min, psi_max, gauge_min_steps, gauge_max_steps));
        previous_target_psi_3 = valve_3_psi;
        last_move_time_3 = current_time;
    }

    // Move gauge 4
    if ((abs(valve_4_psi - previous_target_psi_4) >= psi_deadband || previous_target_psi_4 == -1) &&
        (current_time - last_move_time_4 >= movement_delay_ms || previous_target_psi_4 == -1))
    {
        stepper_4.moveTo(map(valve_4_psi, psi_min, psi_max, gauge_min_steps, gauge_max_steps));
        previous_target_psi_4 = valve_4_psi;
        last_move_time_4 = current_time;
    }

    // Update current gauge PSI from stepper positions
    gauge_1_psi = map(stepper_1.currentPosition(), gauge_min_steps, gauge_max_steps, psi_min, psi_max);
    gauge_3_psi = map(stepper_3.currentPosition(), gauge_min_steps, gauge_max_steps, psi_min, psi_max);
    gauge_4_psi = map(stepper_4.currentPosition(), gauge_min_steps, gauge_max_steps, psi_min, psi_max);
}

// ──────────────────────────────────────────────────────────────────────────────
// Sensor Change Detection & Publishing (Separate Topics)
// ──────────────────────────────────────────────────────────────────────────────
void check_and_publish_sensor_changes()
{
    // Check if periodic publish interval has elapsed
    unsigned long current_time = millis();
    bool force_publish = (current_time - last_sensor_publish_time >= sensor_publish_interval_ms);

    // ========================================
    // Gauge 1 - Valve PSI (potentiometer)
    // ========================================
    if (valve_1_psi != last_published_valve_1_psi || force_publish)
    {
        JsonDocument doc;
        doc["psi"] = valve_1_psi;
        doc["gauge_psi"] = gauge_1_psi;
        doc["ts"] = millis();

        String item = String(naming::DEV_GAUGE_1) + "/" + naming::SENSOR_VALVE_1_PSI;
        mqtt.publishJson(naming::CAT_SENSORS, item.c_str(), doc);
        last_published_valve_1_psi = valve_1_psi;
    }

    // ========================================
    // Gauge 3 - Valve PSI
    // ========================================
    if (valve_3_psi != last_published_valve_3_psi || force_publish)
    {
        JsonDocument doc;
        doc["psi"] = valve_3_psi;
        doc["gauge_psi"] = gauge_3_psi;
        doc["ts"] = millis();

        String item = String(naming::DEV_GAUGE_3) + "/" + naming::SENSOR_VALVE_3_PSI;
        mqtt.publishJson(naming::CAT_SENSORS, item.c_str(), doc);
        last_published_valve_3_psi = valve_3_psi;
    }

    // ========================================
    // Gauge 4 - Valve PSI
    // ========================================
    if (valve_4_psi != last_published_valve_4_psi || force_publish)
    {
        JsonDocument doc;
        doc["psi"] = valve_4_psi;
        doc["gauge_psi"] = gauge_4_psi;
        doc["ts"] = millis();

        String item = String(naming::DEV_GAUGE_4) + "/" + naming::SENSOR_VALVE_4_PSI;
        mqtt.publishJson(naming::CAT_SENSORS, item.c_str(), doc);
        last_published_valve_4_psi = valve_4_psi;
    }

    // Update timestamp if we force published
    if (force_publish)
    {
        last_sensor_publish_time = current_time;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Status Publishing
// ──────────────────────────────────────────────────────────────────────────────
void publish_hardware_status()
{
    JsonDocument doc;
    doc["gauges_active"] = gauges_active;
    doc["gauge_1_psi"] = gauge_1_psi;
    doc["gauge_3_psi"] = gauge_3_psi;
    doc["gauge_4_psi"] = gauge_4_psi;
    doc["valve_1_psi"] = valve_1_psi;
    doc["valve_3_psi"] = valve_3_psi;
    doc["valve_4_psi"] = valve_4_psi;
    doc["ts"] = millis();
    mqtt.publishJson(naming::CAT_STATUS, naming::ITEM_HARDWARE, doc);
}

// ──────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ──────────────────────────────────────────────────────────────────────────────
String extract_command_value(const JsonDocument &payload)
{
    if (payload["state"].is<const char *>())
    {
        return String(payload["state"].as<const char *>());
    }
    else if (payload["value"].is<const char *>())
    {
        return String(payload["value"].as<const char *>());
    }
    else if (payload.is<const char *>())
    {
        return String(payload.as<const char *>());
    }
    return "";
}

// ──────────────────────────────────────────────────────────────────────────────
// Command Acknowledgement
// ──────────────────────────────────────────────────────────────────────────────
void publish_command_acknowledgement(const char *device_id, const char *command)
{
    if (!mqtt.isConnected())
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

    mqtt.get_client().publish(ackTopic.c_str(), buf, false);

    Serial.print(F("[ACK] -> "));
    Serial.println(ackTopic);
}
