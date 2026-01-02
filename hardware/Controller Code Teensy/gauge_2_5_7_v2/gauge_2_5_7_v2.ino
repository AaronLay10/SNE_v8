// ══════════════════════════════════════════════════════════════════════════════
// Gauge 2-5-7 Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Gauge 2-5-7 Controller
 *
 * Hardware:
 * - 3x Stepper motors (gauges 2, 5, 7)
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
 * - Gauge 2: DIR 3, STEP 2, ENABLE 4, POT A11
 * - Gauge 5: DIR 11, STEP 10, ENABLE 12, POT A12
 * - Gauge 7: DIR 7, STEP 6, ENABLE 8, POT A10
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
// Gauge 2
const int gauge_2_dir_pin = 3;
const int gauge_2_step_pin = 2;
const int gauge_2_enable_pin = 4;
const int valve_2_pot_pin = A11;

// Gauge 5
const int gauge_5_dir_pin = 11;
const int gauge_5_step_pin = 10;
const int gauge_5_enable_pin = 12;
const int valve_5_pot_pin = A12;

// Gauge 7
const int gauge_7_dir_pin = 7;
const int gauge_7_step_pin = 6;
const int gauge_7_enable_pin = 8;
const int valve_7_pot_pin = A10;

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
const int valve_2_zero = 9;
const int valve_2_max = 600;
const int valve_5_zero = 35;
const int valve_5_max = 730;
const int valve_7_zero = 5;
const int valve_7_max = 932;

// Signal filtering
const int num_analog_readings = 3;
const float filter_alpha = 0.25f;
const int psi_deadband = 1;                 // PSI must change by this amount to trigger movement
const unsigned long movement_delay_ms = 75; // Minimum time between movements

// EEPROM addresses for storing last known positions
const int eeprom_addr_gauge2 = 0;
const int eeprom_addr_gauge5 = 4;
const int eeprom_addr_gauge7 = 8;

// ──────────────────────────────────────────────────────────────────────────────
// Hardware State Variables
// ──────────────────────────────────────────────────────────────────────────────
bool gauges_active = false;

// Current valve PSI readings (from potentiometers)
int valve_2_psi = 0;
int valve_5_psi = 0;
int valve_7_psi = 0;

// Current gauge needle positions (from stepper positions)
int gauge_2_psi = 0;
int gauge_5_psi = 0;
int gauge_7_psi = 0;

// Last published PSI values (for change detection)
int last_published_gauge_2_psi = -1;
int last_published_gauge_5_psi = -1;
int last_published_gauge_7_psi = -1;
int last_published_valve_2_psi = -1;
int last_published_valve_5_psi = -1;
int last_published_valve_7_psi = -1;

// Periodic sensor publishing
unsigned long last_sensor_publish_time = 0;
const unsigned long sensor_publish_interval_ms = 60000;

// Filtered analog values
float filtered_analog_2 = 0.0f;
float filtered_analog_5 = 0.0f;
float filtered_analog_7 = 0.0f;
bool filters_initialized = false;

// Movement tracking
int previous_target_psi_2 = -1;
int previous_target_psi_5 = -1;
int previous_target_psi_7 = -1;
unsigned long last_move_time_2 = 0;
unsigned long last_move_time_5 = 0;
unsigned long last_move_time_7 = 0;

// ──────────────────────────────────────────────────────────────────────────────
// Stepper Motors
// ──────────────────────────────────────────────────────────────────────────────
AccelStepper stepper_2(AccelStepper::DRIVER, gauge_2_step_pin, gauge_2_dir_pin);
AccelStepper stepper_5(AccelStepper::DRIVER, gauge_5_step_pin, gauge_5_dir_pin);
AccelStepper stepper_7(AccelStepper::DRIVER, gauge_7_step_pin, gauge_7_dir_pin);

// ============================================================================
// DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!) — Using controller_naming.h
// ============================================================================

// Gauge device commands (all gauges respond to these)
const char *gauge_commands[] = {
    naming::CMD_ACTIVATE_GAUGES,
    naming::CMD_INACTIVATE_GAUGES,
    naming::CMD_ADJUST_GAUGE_ZERO,
    naming::CMD_SET_CURRENT_AS_ZERO};

// Sensor arrays for each gauge (valve PSI sensor)
const char *gauge_2_sensors[] = {naming::SENSOR_VALVE_2_PSI};
const char *gauge_5_sensors[] = {naming::SENSOR_VALVE_5_PSI};
const char *gauge_7_sensors[] = {naming::SENSOR_VALVE_7_PSI};

// Create device definitions (bidirectional: commands + sensors)
SentientDeviceDef dev_gauge_2(
    naming::DEV_GAUGE_2,
    naming::FRIENDLY_GAUGE_2,
    "gauge_assembly",
    gauge_commands, 4,
    gauge_2_sensors, 1);

SentientDeviceDef dev_gauge_5(
    naming::DEV_GAUGE_5,
    naming::FRIENDLY_GAUGE_5,
    "gauge_assembly",
    gauge_commands, 4,
    gauge_5_sensors, 1);

SentientDeviceDef dev_gauge_7(
    naming::DEV_GAUGE_7,
    naming::FRIENDLY_GAUGE_7,
    "gauge_assembly",
    gauge_commands, 4,
    gauge_7_sensors, 1);

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
    Serial.println("║      Sentient Engine - Gauge 2-5-7 Controller v2          ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.print("[Gauge 2-5-7] Firmware Version: ");
    Serial.println(firmware::VERSION);
    Serial.print("[Gauge 2-5-7] Unique ID: ");
    Serial.println(firmware::UNIQUE_ID);
    Serial.print("[Gauge 2-5-7] Controller ID: ");
    Serial.println(naming::CONTROLLER_ID);
    Serial.println();

    // ========================================
    // Initialize stepper enable pins
    // ========================================
    pinMode(gauge_2_enable_pin, OUTPUT);
    pinMode(gauge_5_enable_pin, OUTPUT);
    pinMode(gauge_7_enable_pin, OUTPUT);

    // Disable all steppers initially
    digitalWrite(gauge_2_enable_pin, HIGH);
    digitalWrite(gauge_5_enable_pin, HIGH);
    digitalWrite(gauge_7_enable_pin, HIGH);

    // ========================================
    // Configure steppers
    // ========================================
    stepper_2.setPinsInverted(false, false, false);
    stepper_5.setPinsInverted(true, false, false);
    stepper_7.setPinsInverted(true, false, false);

    stepper_2.setMaxSpeed(stepper_max_speed);
    stepper_2.setAcceleration(stepper_acceleration);
    stepper_5.setMaxSpeed(stepper_max_speed);
    stepper_5.setAcceleration(stepper_acceleration);
    stepper_7.setMaxSpeed(stepper_max_speed);
    stepper_7.setAcceleration(stepper_acceleration);

    // ========================================
    // Load last known positions from EEPROM
    // ========================================
    load_gauge_positions();

    // ========================================
    // Auto-zero gauges on startup
    // ========================================
    Serial.println("[Gauge 2-5-7] Auto-zeroing gauges...");
    stepper_2.moveTo(gauge_min_steps);
    stepper_5.moveTo(gauge_min_steps);
    stepper_7.moveTo(gauge_min_steps);

    // Wait for zero (blocking on startup)
    while (stepper_2.distanceToGo() != 0 || stepper_5.distanceToGo() != 0 || stepper_7.distanceToGo() != 0)
    {
        stepper_2.run();
        stepper_5.run();
        stepper_7.run();
    }

    Serial.println("[Gauge 2-5-7] Gauges zeroed");

    // Save zero positions
    save_gauge_position(2);
    save_gauge_position(5);
    save_gauge_position(7);

    // ========================================
    // Register Devices
    // ========================================
    Serial.println("[Gauge 2-5-7] Registering devices...");
    deviceRegistry.addDevice(&dev_gauge_2);
    deviceRegistry.addDevice(&dev_gauge_5);
    deviceRegistry.addDevice(&dev_gauge_7);
    deviceRegistry.printSummary();

    // ========================================
    // Build Capability Manifest
    // ========================================
    Serial.println("[Gauge 2-5-7] Building capability manifest...");
    build_capability_manifest();
    Serial.println("[Gauge 2-5-7] Manifest built successfully");

    // ========================================
    // Initialize MQTT
    // ========================================
    Serial.println("[Gauge 2-5-7] Initializing MQTT...");
    if (!mqtt.begin())
    {
        Serial.println("[Gauge 2-5-7] MQTT initialization failed - continuing without network");
    }
    else
    {
        Serial.println("[Gauge 2-5-7] MQTT initialization successful");
        mqtt.setCommandCallback(handle_mqtt_command);
        mqtt.setHeartbeatBuilder(build_heartbeat_payload);

        // Wait for broker connection (max 5 seconds)
        Serial.println("[Gauge 2-5-7] Waiting for broker connection...");
        unsigned long connection_start = millis();
        while (!mqtt.isConnected() && (millis() - connection_start < 5000))
        {
            mqtt.loop();
            delay(100);
        }

        if (mqtt.isConnected())
        {
            Serial.println("[Gauge 2-5-7] Broker connected!");

            // Register with Sentient system
            Serial.println("[Gauge 2-5-7] Registering with Sentient system...");
            if (manifest.publish_registration(mqtt.get_client(), naming::ROOM_ID, naming::CONTROLLER_ID))
            {
                Serial.println("[Gauge 2-5-7] Registration successful!");
            }
            else
            {
                Serial.println("[Gauge 2-5-7] Registration failed - will retry later");
            }

            // Publish initial status
            publish_hardware_status();
        }
        else
        {
            Serial.println("[Gauge 2-5-7] Broker connection timeout - will retry in main loop");
        }
    }

    Serial.println("[Gauge 2-5-7] Ready - awaiting Sentient commands");
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
    stepper_2.run();
    stepper_5.run();
    stepper_7.run();
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
        digitalWrite(gauge_2_enable_pin, LOW);
        digitalWrite(gauge_5_enable_pin, LOW);
        digitalWrite(gauge_7_enable_pin, LOW);
        gauges_active = true;

        Serial.println("[GAUGES] Activated - tracking valve positions");
        publish_hardware_status();
        publish_command_acknowledgement(naming::CONTROLLER_ID, command);
    }

    // ========================================
    // Inactivate Gauges (move to zero)
    // ========================================
    else if (cmd.equals(naming::CMD_INACTIVATE_GAUGES))
    {
        gauges_active = false;

        // Move all gauges to zero
        stepper_2.moveTo(gauge_min_steps);
        stepper_5.moveTo(gauge_min_steps);
        stepper_7.moveTo(gauge_min_steps);

        // Reset tracking variables
        previous_target_psi_2 = -1;
        previous_target_psi_5 = -1;
        previous_target_psi_7 = -1;
        filters_initialized = false;

        Serial.println("[GAUGES] Inactivated - moving to zero");
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
            Serial.println("[ERROR] Example: {\"gauge\": 2, \"steps\": 10}");
            return;
        }

        int gauge_num = payload["gauge"];
        int steps = payload["steps"]; // Positive = move forward, negative = move back

        switch (gauge_num)
        {
        case 2:
            stepper_2.move(steps);
            Serial.print("[CALIBRATION] Adjusting Gauge 2 by ");
            Serial.print(steps);
            Serial.println(" steps");
            break;
        case 5:
            stepper_5.move(steps);
            Serial.print("[CALIBRATION] Adjusting Gauge 5 by ");
            Serial.print(steps);
            Serial.println(" steps");
            break;
        case 7:
            stepper_7.move(steps);
            Serial.print("[CALIBRATION] Adjusting Gauge 7 by ");
            Serial.print(steps);
            Serial.println(" steps");
            break;
        default:
            Serial.print("[ERROR] Invalid gauge number: ");
            Serial.println(gauge_num);
            Serial.println("[ERROR] Valid gauge numbers: 2, 5, 7");
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
            Serial.println("[ERROR] Example: {\"gauge\": 2}");
            return;
        }

        int gauge_num = payload["gauge"];

        switch (gauge_num)
        {
        case 2:
            stepper_2.setCurrentPosition(gauge_min_steps);
            save_gauge_position(2);
            Serial.println("[CALIBRATION] Gauge 2 - current position set as zero");
            break;
        case 5:
            stepper_5.setCurrentPosition(gauge_min_steps);
            save_gauge_position(5);
            Serial.println("[CALIBRATION] Gauge 5 - current position set as zero");
            break;
        case 7:
            stepper_7.setCurrentPosition(gauge_min_steps);
            save_gauge_position(7);
            Serial.println("[CALIBRATION] Gauge 7 - current position set as zero");
            break;
        default:
            Serial.print("[ERROR] Invalid gauge number: ");
            Serial.println(gauge_num);
            Serial.println("[ERROR] Valid gauge numbers: 2, 5, 7");
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
        doc["gauge_2_psi"] = gauge_2_psi;
        doc["gauge_5_psi"] = gauge_5_psi;
        doc["gauge_7_psi"] = gauge_7_psi;
        doc["valve_2_psi"] = valve_2_psi;
        doc["valve_5_psi"] = valve_5_psi;
        doc["valve_7_psi"] = valve_7_psi;
        doc["stepper_2_pos"] = stepper_2.currentPosition();
        doc["stepper_5_pos"] = stepper_5.currentPosition();
        doc["stepper_7_pos"] = stepper_7.currentPosition();
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

        stepper_2.moveTo(gauge_min_steps);
        stepper_5.moveTo(gauge_min_steps);
        stepper_7.moveTo(gauge_min_steps);

        // Wait for movement to complete
        while (stepper_2.distanceToGo() != 0 || stepper_5.distanceToGo() != 0 || stepper_7.distanceToGo() != 0)
        {
            stepper_2.run();
            stepper_5.run();
            stepper_7.run();
        }

        // Disable motors
        digitalWrite(gauge_2_enable_pin, HIGH);
        digitalWrite(gauge_5_enable_pin, HIGH);
        digitalWrite(gauge_7_enable_pin, HIGH);

        // Save positions
        save_gauge_position(2);
        save_gauge_position(5);
        save_gauge_position(7);

        previous_target_psi_2 = -1;
        previous_target_psi_5 = -1;
        previous_target_psi_7 = -1;
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
    case 2:
        position = stepper_2.currentPosition();
        address = eeprom_addr_gauge2;
        break;
    case 5:
        position = stepper_5.currentPosition();
        address = eeprom_addr_gauge5;
        break;
    case 7:
        position = stepper_7.currentPosition();
        address = eeprom_addr_gauge7;
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
    int position2, position5, position7;

    EEPROM.get(eeprom_addr_gauge2, position2);
    EEPROM.get(eeprom_addr_gauge5, position5);
    EEPROM.get(eeprom_addr_gauge7, position7);

    // Sanity check (if EEPROM is uninitialized, values might be garbage)
    if (position2 < -5000 || position2 > 5000)
        position2 = 0;
    if (position5 < -5000 || position5 > 5000)
        position5 = 0;
    if (position7 < -5000 || position7 > 5000)
        position7 = 0;

    stepper_2.setCurrentPosition(position2);
    stepper_5.setCurrentPosition(position5);
    stepper_7.setCurrentPosition(position7);

    Serial.println("[EEPROM] Loaded last known positions:");
    Serial.print("  Gauge 2: ");
    Serial.println(position2);
    Serial.print("  Gauge 5: ");
    Serial.println(position5);
    Serial.print("  Gauge 7: ");
    Serial.println(position7);
}

// ──────────────────────────────────────────────────────────────────────────────
// Valve Position Reading
// ──────────────────────────────────────────────────────────────────────────────
void read_valve_positions()
{
    // Average readings for noise reduction
    int sum2 = 0, sum5 = 0, sum7 = 0;
    for (int i = 0; i < num_analog_readings; i++)
    {
        sum2 += analogRead(valve_2_pot_pin);
        sum5 += analogRead(valve_5_pot_pin);
        sum7 += analogRead(valve_7_pot_pin);
    }

    float raw2 = sum2 / (float)num_analog_readings;
    float raw5 = sum5 / (float)num_analog_readings;
    float raw7 = sum7 / (float)num_analog_readings;

    // Initialize filters on first run
    if (!filters_initialized)
    {
        filtered_analog_2 = raw2;
        filtered_analog_5 = raw5;
        filtered_analog_7 = raw7;
        filters_initialized = true;
    }

    // Apply exponential moving average filter
    filtered_analog_2 = (filter_alpha * raw2) + ((1.0f - filter_alpha) * filtered_analog_2);
    filtered_analog_5 = (filter_alpha * raw5) + ((1.0f - filter_alpha) * filtered_analog_5);
    filtered_analog_7 = (filter_alpha * raw7) + ((1.0f - filter_alpha) * filtered_analog_7);

    // Convert to PSI
    valve_2_psi = map((int)filtered_analog_2, valve_2_zero, valve_2_max, psi_min, psi_max);
    valve_5_psi = map((int)filtered_analog_5, valve_5_zero, valve_5_max, psi_min, psi_max);
    valve_7_psi = map((int)filtered_analog_7, valve_7_zero, valve_7_max, psi_min, psi_max);

    // Constrain to valid range
    valve_2_psi = constrain(valve_2_psi, psi_min, psi_max);
    valve_5_psi = constrain(valve_5_psi, psi_min, psi_max);
    valve_7_psi = constrain(valve_7_psi, psi_min, psi_max);
}

// ──────────────────────────────────────────────────────────────────────────────
// Gauge Movement (Autonomous Tracking)
// ──────────────────────────────────────────────────────────────────────────────
void move_gauges()
{
    if (!gauges_active)
        return;

    unsigned long current_time = millis();

    // Move gauge 2 with deadband and time delay
    if ((abs(valve_2_psi - previous_target_psi_2) >= psi_deadband || previous_target_psi_2 == -1) &&
        (current_time - last_move_time_2 >= movement_delay_ms || previous_target_psi_2 == -1))
    {
        stepper_2.moveTo(map(valve_2_psi, psi_min, psi_max, gauge_min_steps, gauge_max_steps));
        previous_target_psi_2 = valve_2_psi;
        last_move_time_2 = current_time;
    }

    // Move gauge 5
    if ((abs(valve_5_psi - previous_target_psi_5) >= psi_deadband || previous_target_psi_5 == -1) &&
        (current_time - last_move_time_5 >= movement_delay_ms || previous_target_psi_5 == -1))
    {
        stepper_5.moveTo(map(valve_5_psi, psi_min, psi_max, gauge_min_steps, gauge_max_steps));
        previous_target_psi_5 = valve_5_psi;
        last_move_time_5 = current_time;
    }

    // Move gauge 7
    if ((abs(valve_7_psi - previous_target_psi_7) >= psi_deadband || previous_target_psi_7 == -1) &&
        (current_time - last_move_time_7 >= movement_delay_ms || previous_target_psi_7 == -1))
    {
        stepper_7.moveTo(map(valve_7_psi, psi_min, psi_max, gauge_min_steps, gauge_max_steps));
        previous_target_psi_7 = valve_7_psi;
        last_move_time_7 = current_time;
    }

    // Update current gauge PSI from stepper positions
    gauge_2_psi = map(stepper_2.currentPosition(), gauge_min_steps, gauge_max_steps, psi_min, psi_max);
    gauge_5_psi = map(stepper_5.currentPosition(), gauge_min_steps, gauge_max_steps, psi_min, psi_max);
    gauge_7_psi = map(stepper_7.currentPosition(), gauge_min_steps, gauge_max_steps, psi_min, psi_max);
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
    // Gauge 2 - Valve PSI (potentiometer)
    // ========================================
    if (valve_2_psi != last_published_valve_2_psi || force_publish)
    {
        JsonDocument doc;
        doc["psi"] = valve_2_psi;
        doc["gauge_psi"] = gauge_2_psi;
        doc["ts"] = millis();

        String item = String(naming::DEV_GAUGE_2) + "/" + naming::SENSOR_VALVE_2_PSI;
        mqtt.publishJson(naming::CAT_SENSORS, item.c_str(), doc);
        last_published_valve_2_psi = valve_2_psi;
    }

    // ========================================
    // Gauge 5 - Valve PSI
    // ========================================
    if (valve_5_psi != last_published_valve_5_psi || force_publish)
    {
        JsonDocument doc;
        doc["psi"] = valve_5_psi;
        doc["gauge_psi"] = gauge_5_psi;
        doc["ts"] = millis();

        String item = String(naming::DEV_GAUGE_5) + "/" + naming::SENSOR_VALVE_5_PSI;
        mqtt.publishJson(naming::CAT_SENSORS, item.c_str(), doc);
        last_published_valve_5_psi = valve_5_psi;
    }

    // ========================================
    // Gauge 7 - Valve PSI
    // ========================================
    if (valve_7_psi != last_published_valve_7_psi || force_publish)
    {
        JsonDocument doc;
        doc["psi"] = valve_7_psi;
        doc["gauge_psi"] = gauge_7_psi;
        doc["ts"] = millis();

        String item = String(naming::DEV_GAUGE_7) + "/" + naming::SENSOR_VALVE_7_PSI;
        mqtt.publishJson(naming::CAT_SENSORS, item.c_str(), doc);
        last_published_valve_7_psi = valve_7_psi;
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
    doc["gauge_2_psi"] = gauge_2_psi;
    doc["gauge_5_psi"] = gauge_5_psi;
    doc["gauge_7_psi"] = gauge_7_psi;
    doc["valve_2_psi"] = valve_2_psi;
    doc["valve_5_psi"] = valve_5_psi;
    doc["valve_7_psi"] = valve_7_psi;
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
