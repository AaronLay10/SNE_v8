// ══════════════════════════════════════════════════════════════════════════════
// Gauge 6 + LEDs Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/**
 * Gauge 6 + LEDs Controller
 *
 * HARDWARE:
 * - 1x AccelStepper gauge motor (Gauge 6) with valve potentiometer sensor
 * - 7x Photoresistor ball valve lever sensors
 * - 219x WS2811 ceiling LEDs (clock face pattern, 9 sections)
 * - 7x Individual WS2812B gauge indicator LEDs with flicker animation
 *
 * STATELESS ARCHITECTURE:
 * - When ACTIVE: Gauge autonomously tracks valve potentiometer position
 * - When INACTIVE: Gauge moves to zero position
 * - Photoresistor sensors publish OPEN/CLOSED state on change
 * - Ceiling LEDs respond to pattern commands (3 patterns + off)
 * - Gauge indicator LEDs support flicker animation modes
 * - EEPROM position storage for gauge calibration
 *
 * AUTHOR: Sentient Engine Team
 * TARGET: Teensy 4.1
 * ============================================================================
 */

#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <SentientCapabilityManifest.h>
#include <AccelStepper.h>
#include <FastLED.h>
#include <EEPROM.h>

#include "FirmwareMetadata.h"
#include "controller_naming.h"

using namespace naming;

// ============================================================================
// MQTT CONFIGURATION
// ============================================================================

const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_user = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
const unsigned long heartbeat_interval_ms = 5000; // 5 seconds

// ============================================================================
// PIN DEFINITIONS
// ============================================================================

// Gauge 6 Stepper Motor
const int gauge_6_step_pin = 39;
const int gauge_6_dir_pin = 40;
const int gauge_6_enable_pin = 41;
const int valve_6_pot_pin = A4;

// Photoresistor Lever Sensors
const int lever_1_red_pin = A2;
const int lever_2_blue_pin = A3;
const int lever_3_green_pin = A5;
const int lever_4_white_pin = A6;
const int lever_5_orange_pin = A7;
const int lever_6_yellow_pin = A8;
const int lever_7_purple_pin = A9;

// LED Pins
const int ceiling_leds_pin = 7;
const int gauge_led_1_pin = 25;
const int gauge_led_2_pin = 26;
const int gauge_led_3_pin = 27;
const int gauge_led_4_pin = 28;
const int gauge_led_5_pin = 29;
const int gauge_led_6_pin = 30;
const int gauge_led_7_pin = 31;

// ============================================================================
// GAUGE CONFIGURATION
// ============================================================================

const int gauge_max_speed = 800;
const int gauge_acceleration = 500;
const int gauge_min_steps = 0;
const int gauge_max_steps = 700;

// Valve calibration (map potentiometer reading to steps)
const int valve_6_zero = 225;
const int valve_6_max = 500;

// EEPROM address for gauge position
const int eeprom_addr_gauge6 = 0;

// ============================================================================
// LED CONFIGURATION
// ============================================================================

// Ceiling LEDs
const int num_ceiling_leds = 219;
CRGB ceiling_leds[num_ceiling_leds];

// Ceiling LED sections (clock face pattern)
const int section_start[] = {0, 0, 25, 48, 73, 99, 125, 149, 174, 198};
const int section_length[] = {0, 25, 23, 25, 26, 26, 24, 25, 24, 21};

// Gauge indicator LEDs (7 individual LEDs)
CRGB gauge_leds[7][1];

// Color definitions
const uint32_t color_clock_red = 0xFF0000;
const uint32_t color_clock_blue = 0x0000FF;
const uint32_t color_clock_green = 0x00FF00;
const uint32_t color_clock_white = 0xFFFFFF;
const uint32_t color_clock_orange = 0xFF8000;
const uint32_t color_clock_yellow = 0xFFFF00;
const uint32_t color_clock_purple = 0x800080;

const uint32_t color_gauge_base = 0x221100;
const uint32_t flicker_red = 0xFF0000;
const uint32_t flicker_blue = 0x0000FF;
const uint32_t flicker_green = 0x00FF00;
const uint32_t flicker_white = 0xFFFFFF;
const uint32_t flicker_orange = 0xFF8000;
const uint32_t flicker_yellow = 0xFFFF00;
const uint32_t flicker_purple = 0x800080;

// ============================================================================
// FLICKER ANIMATION STATE
// ============================================================================

struct FlickerState
{
    bool enabled = false;
    bool active = false;
    unsigned long next_burst_at = 0;
    unsigned long flicker_end = 0;
    uint32_t color1 = 0;
    uint32_t color2 = 0;
    bool use_two_colors = false;
    bool use_second_color = false;
};

FlickerState gauge_flicker[7];

// ============================================================================
// SENSOR CONFIGURATION
// ============================================================================

const int photoresistor_threshold = 500;

// Sensor states
bool lever_1_red_open = false;
bool lever_2_blue_open = false;
bool lever_3_green_open = false;
bool lever_4_white_open = false;
bool lever_5_orange_open = false;
bool lever_6_yellow_open = false;
bool lever_7_purple_open = false;

// Last published states
bool last_published_lever_1_red_open = false;
bool last_published_lever_2_blue_open = false;
bool last_published_lever_3_green_open = false;
bool last_published_lever_4_white_open = false;
bool last_published_lever_5_orange_open = false;
bool last_published_lever_6_yellow_open = false;
bool last_published_lever_7_purple_open = false;

bool levers_initialized = false;

// Periodic publishing
unsigned long last_sensor_publish_time = 0;
const unsigned long sensor_publish_interval = 60000;

// ============================================================================
// HARDWARE STATE
// ============================================================================

bool gauges_active = false;
int valve_6_psi = 0;

AccelStepper stepper_6(AccelStepper::DRIVER, gauge_6_step_pin, gauge_6_dir_pin);

// ============================================================================
// DEVICE REGISTRY
// ============================================================================

// Gauge 6 commands and sensors
const char *gauge_6_commands[] = {
    CMD_ACTIVATE_GAUGES,
    CMD_DEACTIVATE_GAUGES,
    CMD_ADJUST_GAUGE_ZERO,
    CMD_SET_CURRENT_AS_ZERO};
const char *gauge_6_sensors[] = {SENSOR_VALVE_6_PSI};

// Lever sensors (no commands)
const char *lever_state_sensors[] = {"state"};

// Ceiling LED commands
const char *ceiling_led_commands[] = {
    CMD_CEILING_OFF,
    CMD_CEILING_PATTERN_1,
    CMD_CEILING_PATTERN_2,
    CMD_CEILING_PATTERN_3};

// Gauge indicator LED commands
const char *gauge_led_commands[] = {
    CMD_FLICKER_OFF,
    CMD_FLICKER_MODE_2,
    CMD_FLICKER_MODE_5,
    CMD_FLICKER_MODE_8,
    CMD_GAUGE_LEDS_ON,
    CMD_GAUGE_LEDS_OFF};

// Device definitions
SentientDeviceDef dev_gauge_6(
    DEV_GAUGE_6,
    FRIENDLY_GAUGE_6,
    "gauge_assembly",
    gauge_6_commands, 4,
    gauge_6_sensors, 1);

SentientDeviceDef dev_lever_1_red(
    DEV_LEVER_1_RED,
    FRIENDLY_LEVER_1_RED,
    "photoresistor",
    nullptr, 0,
    lever_state_sensors, 1);

SentientDeviceDef dev_lever_2_blue(
    DEV_LEVER_2_BLUE,
    FRIENDLY_LEVER_2_BLUE,
    "photoresistor",
    nullptr, 0,
    lever_state_sensors, 1);

SentientDeviceDef dev_lever_3_green(
    DEV_LEVER_3_GREEN,
    FRIENDLY_LEVER_3_GREEN,
    "photoresistor",
    nullptr, 0,
    lever_state_sensors, 1);

SentientDeviceDef dev_lever_4_white(
    DEV_LEVER_4_WHITE,
    FRIENDLY_LEVER_4_WHITE,
    "photoresistor",
    nullptr, 0,
    lever_state_sensors, 1);

SentientDeviceDef dev_lever_5_orange(
    DEV_LEVER_5_ORANGE,
    FRIENDLY_LEVER_5_ORANGE,
    "photoresistor",
    nullptr, 0,
    lever_state_sensors, 1);

SentientDeviceDef dev_lever_6_yellow(
    DEV_LEVER_6_YELLOW,
    FRIENDLY_LEVER_6_YELLOW,
    "photoresistor",
    nullptr, 0,
    lever_state_sensors, 1);

SentientDeviceDef dev_lever_7_purple(
    DEV_LEVER_7_PURPLE,
    FRIENDLY_LEVER_7_PURPLE,
    "photoresistor",
    nullptr, 0,
    lever_state_sensors, 1);

SentientDeviceDef dev_ceiling_leds(
    DEV_CEILING_LEDS,
    FRIENDLY_CEILING_LEDS,
    "led_strip",
    ceiling_led_commands, 4,
    nullptr, 0);

SentientDeviceDef dev_gauge_leds(
    DEV_GAUGE_LEDS,
    FRIENDLY_GAUGE_LEDS,
    "led_strip",
    gauge_led_commands, 6,
    nullptr, 0);

SentientDeviceRegistry deviceRegistry;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);
void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void update_gauge_tracking();
void monitor_sensors();
void publish_sensor_changes(bool force_publish);
void publish_hardware_status();
void save_gauge_position(int gauge_number);
void load_gauge_positions();
void set_ceiling_off();
void set_ceiling_pattern_1();
void set_ceiling_pattern_2();
void set_ceiling_pattern_3();
void set_flicker_off();
void set_flicker_mode_2();
void set_flicker_mode_5();
void set_flicker_mode_8();
void update_gauge_flicker();
String extract_command_value(const JsonDocument &payload);
void publish_command_acknowledgement(const char *device_id, const char *command);

// ============================================================================
// MQTT OBJECTS
// ============================================================================

SentientCapabilityManifest manifest;
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
    Serial.println("║      Sentient Engine - Gauge 6 & LEDs Controller v2       ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.print("[Gauge 6 LEDs] Firmware Version: ");
    Serial.println(firmware::VERSION);
    Serial.print("[Gauge 6 LEDs] Unique ID: ");
    Serial.println(firmware::UNIQUE_ID);
    Serial.print("[Gauge 6 LEDs] Controller ID: ");
    Serial.println(CONTROLLER_ID);
    Serial.println();

    // Configure stepper motor
    stepper_6.setMaxSpeed(gauge_max_speed);
    stepper_6.setAcceleration(gauge_acceleration);
    pinMode(gauge_6_enable_pin, OUTPUT);
    digitalWrite(gauge_6_enable_pin, HIGH); // Disable initially (active LOW)

    // Load saved gauge position
    load_gauge_positions();

    // Configure analog inputs
    pinMode(valve_6_pot_pin, INPUT);
    pinMode(lever_1_red_pin, INPUT);
    pinMode(lever_2_blue_pin, INPUT);
    pinMode(lever_3_green_pin, INPUT);
    pinMode(lever_4_white_pin, INPUT);
    pinMode(lever_5_orange_pin, INPUT);
    pinMode(lever_6_yellow_pin, INPUT);
    pinMode(lever_7_purple_pin, INPUT);

    // Initialize LEDs
    FastLED.addLeds<WS2811, ceiling_leds_pin, RGB>(ceiling_leds, num_ceiling_leds);
    FastLED.addLeds<WS2812B, gauge_led_1_pin, GRB>(gauge_leds[0], 1);
    FastLED.addLeds<WS2812B, gauge_led_2_pin, GRB>(gauge_leds[1], 1);
    FastLED.addLeds<WS2812B, gauge_led_3_pin, GRB>(gauge_leds[2], 1);
    FastLED.addLeds<WS2812B, gauge_led_4_pin, GRB>(gauge_leds[3], 1);
    FastLED.addLeds<WS2812B, gauge_led_5_pin, GRB>(gauge_leds[4], 1);
    FastLED.addLeds<WS2812B, gauge_led_6_pin, GRB>(gauge_leds[5], 1);
    FastLED.addLeds<WS2812B, gauge_led_7_pin, GRB>(gauge_leds[6], 1);
    FastLED.clear();
    FastLED.show();

    Serial.println("[Gauge 6 LEDs] FastLED initialized");
    Serial.print("[Gauge 6 LEDs] Ceiling LEDs: ");
    Serial.println(num_ceiling_leds);
    Serial.println("[Gauge 6 LEDs] Gauge indicator LEDs: 7");

    // Register devices
    Serial.println("[Gauge 6 LEDs] Registering devices...");
    deviceRegistry.addDevice(&dev_gauge_6);
    deviceRegistry.addDevice(&dev_lever_1_red);
    deviceRegistry.addDevice(&dev_lever_2_blue);
    deviceRegistry.addDevice(&dev_lever_3_green);
    deviceRegistry.addDevice(&dev_lever_4_white);
    deviceRegistry.addDevice(&dev_lever_5_orange);
    deviceRegistry.addDevice(&dev_lever_6_yellow);
    deviceRegistry.addDevice(&dev_lever_7_purple);
    deviceRegistry.addDevice(&dev_ceiling_leds);
    deviceRegistry.addDevice(&dev_gauge_leds);
    deviceRegistry.printSummary();

    // Build capability manifest
    Serial.println("[Gauge 6 LEDs] Building capability manifest...");
    build_capability_manifest();
    Serial.println("[Gauge 6 LEDs] Manifest built successfully");

    // Initialize MQTT
    Serial.println("[Gauge 6 LEDs] Initializing MQTT...");
    if (!mqtt.begin())
    {
        Serial.println("[Gauge 6 LEDs] MQTT initialization failed - continuing without network");
    }
    else
    {
        Serial.println("[Gauge 6 LEDs] MQTT initialization successful");
        mqtt.setCommandCallback(handle_mqtt_command);
        mqtt.setHeartbeatBuilder(build_heartbeat_payload);

        // Wait for broker connection
        Serial.println("[Gauge 6 LEDs] Waiting for broker connection...");
        unsigned long connection_start = millis();
        while (!mqtt.isConnected() && (millis() - connection_start < 5000))
        {
            mqtt.loop();
            delay(100);
        }

        if (mqtt.isConnected())
        {
            Serial.println("[Gauge 6 LEDs] Broker connected!");

            // Register with Sentient system
            Serial.println("[Gauge 6 LEDs] Registering with Sentient system...");
            if (manifest.publish_registration(mqtt.get_client(), ROOM_ID, CONTROLLER_ID))
            {
                Serial.println("[Gauge 6 LEDs] Registration successful!");
            }
            else
            {
                Serial.println("[Gauge 6 LEDs] Registration failed - will retry later");
            }

            // Publish initial sensor states
            publish_sensor_changes(true);
            publish_hardware_status();
        }
        else
        {
            Serial.println("[Gauge 6 LEDs] Broker connection timeout - continuing offline");
        }
    }

    Serial.println("════════════════════════════════════════════════════════════");
    Serial.println("Setup complete. Entering main loop...");
    Serial.println("════════════════════════════════════════════════════════════");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop()
{
    mqtt.loop();
    stepper_6.run();
    update_gauge_tracking();
    monitor_sensors();
    update_gauge_flicker();

    // Periodic status publish
    static unsigned long last_status_publish = 0;
    if (millis() - last_status_publish >= heartbeat_interval_ms)
    {
        publish_hardware_status();
        last_status_publish = millis();
    }
}

// ============================================================================
// MQTT CONFIGURATION
// ============================================================================

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
    cfg.namespaceId = CLIENT_ID;
    cfg.roomId = ROOM_ID;
    cfg.controllerId = CONTROLLER_ID;
    cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
    cfg.displayName = CONTROLLER_FRIENDLY_NAME;
    cfg.publishJsonCapacity = 1536;
    cfg.heartbeatIntervalMs = heartbeat_interval_ms;
    cfg.autoHeartbeat = true;
    cfg.useDhcp = true;
    return cfg;
}

void build_capability_manifest()
{
    manifest.set_controller_info(
        CONTROLLER_ID,
        CONTROLLER_FRIENDLY_NAME,
        firmware::VERSION,
        ROOM_ID,
        CONTROLLER_ID);

    deviceRegistry.buildManifest(manifest);
}

bool build_heartbeat_payload(JsonDocument &doc, void * /*ctx*/)
{
    doc["uid"] = CONTROLLER_ID;
    doc["fw"] = firmware::VERSION;
    doc["up"] = millis();
    return true;
}

// ============================================================================
// COMMAND HANDLER
// ============================================================================

void handle_mqtt_command(const char *command, const JsonDocument &payload, void * /*ctx*/)
{
    String value = extract_command_value(payload);
    String cmd = String(command);

    Serial.print(F("[COMMAND] Received: "));
    Serial.print(command);
    Serial.print(F(" Value: "));
    Serial.println(value);

    // =========================================
    // GAUGE 6 COMMANDS
    // =========================================

    if (cmd.equals(CMD_ACTIVATE_GAUGES))
    {
        digitalWrite(gauge_6_enable_pin, LOW); // Enable stepper (active LOW)
        gauges_active = true;
        Serial.println(F("[GAUGE 6] Activated - tracking valve position"));
        publish_hardware_status();
        publish_command_acknowledgement(CONTROLLER_ID, command);
    }
    else if (cmd.equals(CMD_DEACTIVATE_GAUGES))
    {
        gauges_active = false;
        stepper_6.moveTo(gauge_min_steps);
        Serial.println(F("[GAUGE 6] Deactivated - moving to zero"));
        publish_hardware_status();
        publish_command_acknowledgement(CONTROLLER_ID, command);
    }
    else if (cmd.equals(CMD_ADJUST_GAUGE_ZERO))
    {
        if (!payload["gauge"].is<int>() || !payload["steps"].is<int>())
        {
            Serial.println(F("[ERROR] adjust_gauge_zero requires 'gauge' and 'steps' parameters"));
            return;
        }

        int gauge_num = payload["gauge"];
        int steps = payload["steps"];

        if (gauge_num == 6)
        {
            stepper_6.move(steps);
            Serial.print(F("[CALIBRATION] Adjusting Gauge 6 by "));
            Serial.print(steps);
            Serial.println(F(" steps"));
        }
    }
    else if (cmd.equals(CMD_SET_CURRENT_AS_ZERO))
    {
        if (!payload["gauge"].is<int>())
        {
            Serial.println(F("[ERROR] set_current_as_zero requires 'gauge' parameter"));
            return;
        }

        int gauge_num = payload["gauge"];

        if (gauge_num == 6)
        {
            stepper_6.setCurrentPosition(gauge_min_steps);
            save_gauge_position(6);
            Serial.println(F("[CALIBRATION] Gauge 6 - current position set as zero"));
        }
    }

    // =========================================
    // CEILING LED COMMANDS
    // =========================================

    else if (cmd.equals(CMD_CEILING_OFF))
    {
        set_ceiling_off();
        Serial.println(F("[CEILING] All LEDs off"));
        publish_command_acknowledgement(DEV_CEILING_LEDS, command);
    }
    else if (cmd.equals(CMD_CEILING_PATTERN_1))
    {
        set_ceiling_pattern_1();
        Serial.println(F("[CEILING] Pattern 1"));
        publish_command_acknowledgement(DEV_CEILING_LEDS, command);
    }
    else if (cmd.equals(CMD_CEILING_PATTERN_2))
    {
        set_ceiling_pattern_2();
        Serial.println(F("[CEILING] Pattern 2"));
        publish_command_acknowledgement(DEV_CEILING_LEDS, command);
    }
    else if (cmd.equals(CMD_CEILING_PATTERN_3))
    {
        set_ceiling_pattern_3();
        Serial.println(F("[CEILING] Pattern 3"));
        publish_command_acknowledgement(DEV_CEILING_LEDS, command);
    }

    // =========================================
    // GAUGE INDICATOR LED COMMANDS
    // =========================================

    else if (cmd.equals(CMD_FLICKER_OFF))
    {
        set_flicker_off();
        Serial.println(F("[GAUGE LEDS] Flicker off"));
        publish_command_acknowledgement(DEV_GAUGE_LEDS, command);
    }
    else if (cmd.equals(CMD_FLICKER_MODE_2))
    {
        set_flicker_mode_2();
        Serial.println(F("[GAUGE LEDS] Flicker mode 1"));
        publish_command_acknowledgement(DEV_GAUGE_LEDS, command);
    }
    else if (cmd.equals(CMD_FLICKER_MODE_5))
    {
        set_flicker_mode_5();
        Serial.println(F("[GAUGE LEDS] Flicker mode 2"));
        publish_command_acknowledgement(DEV_GAUGE_LEDS, command);
    }
    else if (cmd.equals(CMD_FLICKER_MODE_8))
    {
        set_flicker_mode_8();
        Serial.println(F("[GAUGE LEDS] Flicker mode 3"));
        publish_command_acknowledgement(DEV_GAUGE_LEDS, command);
    }
    else if (cmd.equals(CMD_GAUGE_LEDS_ON))
    {
        for (int i = 0; i < 7; i++)
        {
            gauge_flicker[i].enabled = false;
            gauge_leds[i][0] = CRGB(color_gauge_base);
        }
        FastLED.show();
        Serial.println(F("[GAUGE LEDS] All ON (base color)"));
        publish_command_acknowledgement(DEV_GAUGE_LEDS, command);
    }
    else if (cmd.equals(CMD_GAUGE_LEDS_OFF))
    {
        for (int i = 0; i < 7; i++)
        {
            gauge_flicker[i].enabled = false;
            gauge_leds[i][0] = CRGB::Black;
        }
        FastLED.show();
        Serial.println(F("[GAUGE LEDS] All OFF"));
        publish_command_acknowledgement(DEV_GAUGE_LEDS, command);
    }
    else
    {
        Serial.print(F("[WARNING] Unknown command: "));
        Serial.println(cmd);
    }
}

// ============================================================================
// GAUGE TRACKING
// ============================================================================

void update_gauge_tracking()
{
    if (!gauges_active)
        return;

    int raw_reading = analogRead(valve_6_pot_pin);
    int target_steps = map(raw_reading, valve_6_zero, valve_6_max, gauge_min_steps, gauge_max_steps);
    target_steps = constrain(target_steps, gauge_min_steps, gauge_max_steps);

    stepper_6.moveTo(target_steps);
    valve_6_psi = raw_reading;
}

// ============================================================================
// SENSOR MONITORING
// ============================================================================

void monitor_sensors()
{
    unsigned long current_time = millis();

    // Read photoresistors
    lever_1_red_open = (analogRead(lever_1_red_pin) > photoresistor_threshold);
    lever_2_blue_open = (analogRead(lever_2_blue_pin) > photoresistor_threshold);
    lever_3_green_open = (analogRead(lever_3_green_pin) > photoresistor_threshold);
    lever_4_white_open = (analogRead(lever_4_white_pin) > photoresistor_threshold);
    lever_5_orange_open = (analogRead(lever_5_orange_pin) > photoresistor_threshold);
    lever_6_yellow_open = (analogRead(lever_6_yellow_pin) > photoresistor_threshold);
    lever_7_purple_open = (analogRead(lever_7_purple_pin) > photoresistor_threshold);

    bool force_publish = (current_time - last_sensor_publish_time >= sensor_publish_interval);
    publish_sensor_changes(force_publish);

    if (force_publish)
    {
        last_sensor_publish_time = current_time;
    }
}

void publish_sensor_changes(bool force_publish)
{
    if (!mqtt.isConnected())
        return;

    JsonDocument doc;

    if (lever_1_red_open != last_published_lever_1_red_open || !levers_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = lever_1_red_open ? "OPEN" : "CLOSED";
        mqtt.publishJson(CAT_SENSORS, (String(DEV_LEVER_1_RED) + "/state").c_str(), doc);
        last_published_lever_1_red_open = lever_1_red_open;
    }

    if (lever_2_blue_open != last_published_lever_2_blue_open || !levers_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = lever_2_blue_open ? "OPEN" : "CLOSED";
        mqtt.publishJson(CAT_SENSORS, (String(DEV_LEVER_2_BLUE) + "/state").c_str(), doc);
        last_published_lever_2_blue_open = lever_2_blue_open;
    }

    if (lever_3_green_open != last_published_lever_3_green_open || !levers_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = lever_3_green_open ? "OPEN" : "CLOSED";
        mqtt.publishJson(CAT_SENSORS, (String(DEV_LEVER_3_GREEN) + "/state").c_str(), doc);
        last_published_lever_3_green_open = lever_3_green_open;
    }

    if (lever_4_white_open != last_published_lever_4_white_open || !levers_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = lever_4_white_open ? "OPEN" : "CLOSED";
        mqtt.publishJson(CAT_SENSORS, (String(DEV_LEVER_4_WHITE) + "/state").c_str(), doc);
        last_published_lever_4_white_open = lever_4_white_open;
    }

    if (lever_5_orange_open != last_published_lever_5_orange_open || !levers_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = lever_5_orange_open ? "OPEN" : "CLOSED";
        mqtt.publishJson(CAT_SENSORS, (String(DEV_LEVER_5_ORANGE) + "/state").c_str(), doc);
        last_published_lever_5_orange_open = lever_5_orange_open;
    }

    if (lever_6_yellow_open != last_published_lever_6_yellow_open || !levers_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = lever_6_yellow_open ? "OPEN" : "CLOSED";
        mqtt.publishJson(CAT_SENSORS, (String(DEV_LEVER_6_YELLOW) + "/state").c_str(), doc);
        last_published_lever_6_yellow_open = lever_6_yellow_open;
    }

    if (lever_7_purple_open != last_published_lever_7_purple_open || !levers_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = lever_7_purple_open ? "OPEN" : "CLOSED";
        mqtt.publishJson(CAT_SENSORS, (String(DEV_LEVER_7_PURPLE) + "/state").c_str(), doc);
        last_published_lever_7_purple_open = lever_7_purple_open;
    }

    if (!levers_initialized)
    {
        levers_initialized = true;
    }
}

// ============================================================================
// CEILING LED PATTERNS
// ============================================================================

void set_ceiling_off()
{
    FastLED.clear();
    FastLED.show();
}

void set_ceiling_pattern_1()
{
    FastLED.clear();
    fill_solid(&ceiling_leds[section_start[2]], section_length[2], CRGB(color_clock_blue));
    fill_solid(&ceiling_leds[section_start[7]], section_length[7], CRGB(color_clock_green));
    FastLED.show();
}

void set_ceiling_pattern_2()
{
    FastLED.clear();
    fill_solid(&ceiling_leds[section_start[1]], section_length[1], CRGB(color_clock_red));
    fill_solid(&ceiling_leds[section_start[5]], section_length[5], CRGB(color_clock_white));
    fill_solid(&ceiling_leds[section_start[8]], section_length[8], CRGB(color_clock_orange));
    FastLED.show();
}

void set_ceiling_pattern_3()
{
    FastLED.clear();
    fill_solid(&ceiling_leds[section_start[1]], section_length[1], CRGB(color_clock_purple));
    fill_solid(&ceiling_leds[section_start[2]], section_length[2], CRGB(color_clock_blue));
    fill_solid(&ceiling_leds[section_start[5]], section_length[5], CRGB(color_clock_yellow));
    fill_solid(&ceiling_leds[section_start[7]], section_length[7], CRGB(color_clock_green));
    FastLED.show();
}

// ============================================================================
// GAUGE INDICATOR LED FLICKER MODES
// ============================================================================

void set_flicker_off()
{
    for (int i = 0; i < 7; i++)
    {
        gauge_flicker[i].enabled = false;
        gauge_leds[i][0] = CRGB::Black;
    }
    FastLED.show();
}

void set_flicker_mode_2()
{
    // Gauges 4 & 6 flicker
    for (int i = 0; i < 7; i++)
    {
        gauge_flicker[i].enabled = false;
        gauge_leds[i][0] = CRGB(color_gauge_base);
    }

    gauge_flicker[3].enabled = true;
    gauge_flicker[3].color1 = flicker_blue;
    gauge_flicker[3].color2 = color_gauge_base;
    gauge_flicker[3].use_two_colors = true;

    gauge_flicker[5].enabled = true;
    gauge_flicker[5].color1 = flicker_green;
    gauge_flicker[5].color2 = color_gauge_base;
    gauge_flicker[5].use_two_colors = true;

    FastLED.show();
}

void set_flicker_mode_5()
{
    // Gauges 1, 2, 3, 7 flicker
    for (int i = 0; i < 7; i++)
    {
        gauge_flicker[i].enabled = false;
        gauge_leds[i][0] = CRGB(color_gauge_base);
    }

    gauge_flicker[0].enabled = true;
    gauge_flicker[0].color1 = flicker_red;
    gauge_flicker[0].color2 = color_gauge_base;
    gauge_flicker[0].use_two_colors = true;

    gauge_flicker[1].enabled = true;
    gauge_flicker[1].color1 = flicker_orange;
    gauge_flicker[1].color2 = color_gauge_base;
    gauge_flicker[1].use_two_colors = true;

    gauge_flicker[2].enabled = true;
    gauge_flicker[2].color1 = flicker_white;
    gauge_flicker[2].color2 = color_gauge_base;
    gauge_flicker[2].use_two_colors = true;

    gauge_flicker[6].enabled = true;
    gauge_flicker[6].color1 = flicker_orange;
    gauge_flicker[6].color2 = color_gauge_base;
    gauge_flicker[6].use_two_colors = true;

    FastLED.show();
}

void set_flicker_mode_8()
{
    // All 7 gauges flicker
    for (int i = 0; i < 7; i++)
    {
        gauge_flicker[i].enabled = false;
        gauge_leds[i][0] = CRGB(color_gauge_base);
    }

    gauge_flicker[0].enabled = true;
    gauge_flicker[0].color1 = flicker_red;
    gauge_flicker[0].color2 = color_gauge_base;
    gauge_flicker[0].use_two_colors = true;

    gauge_flicker[1].enabled = true;
    gauge_flicker[1].color1 = flicker_orange;
    gauge_flicker[1].color2 = flicker_purple;
    gauge_flicker[1].use_two_colors = true;

    gauge_flicker[2].enabled = true;
    gauge_flicker[2].color1 = flicker_white;
    gauge_flicker[2].color2 = color_gauge_base;
    gauge_flicker[2].use_two_colors = true;

    gauge_flicker[3].enabled = true;
    gauge_flicker[3].color1 = flicker_blue;
    gauge_flicker[3].color2 = color_gauge_base;
    gauge_flicker[3].use_two_colors = true;

    gauge_flicker[4].enabled = true;
    gauge_flicker[4].color1 = flicker_purple;
    gauge_flicker[4].color2 = color_gauge_base;
    gauge_flicker[4].use_two_colors = true;

    gauge_flicker[5].enabled = true;
    gauge_flicker[5].color1 = flicker_orange;
    gauge_flicker[5].color2 = color_gauge_base;
    gauge_flicker[5].use_two_colors = true;

    gauge_flicker[6].enabled = true;
    gauge_flicker[6].color1 = flicker_orange;
    gauge_flicker[6].color2 = color_gauge_base;
    gauge_flicker[6].use_two_colors = true;

    FastLED.show();
}

// ============================================================================
// FLICKER ANIMATION UPDATE
// ============================================================================

void update_gauge_flicker()
{
    unsigned long now = millis();
    bool any_active = false;

    for (int i = 0; i < 7; i++)
    {
        if (!gauge_flicker[i].enabled)
            continue;

        FlickerState &fs = gauge_flicker[i];

        if (!fs.active)
        {
            if (now >= fs.next_burst_at)
            {
                fs.active = true;
                fs.flicker_end = now + random(50, 250);
                fs.use_second_color = (fs.use_two_colors && random(2) == 0);
                fs.next_burst_at = now + random(100, 500);
            }
        }

        if (fs.active && now <= fs.flicker_end)
        {
            CRGB color = (fs.use_two_colors && fs.use_second_color) ? CRGB(fs.color2) : CRGB(fs.color1);
            color.nscale8_video(random(256));
            gauge_leds[i][0] = color;
            any_active = true;
        }
        else if (fs.active && now > fs.flicker_end)
        {
            fs.active = false;
            gauge_leds[i][0] = CRGB::Black;
        }
    }

    if (any_active)
    {
        FastLED.show();
    }
}

// ============================================================================
// EEPROM PERSISTENCE
// ============================================================================

void save_gauge_position(int gauge_number)
{
    if (gauge_number != 6)
    {
        Serial.println(F("[ERROR] Invalid gauge number (only gauge 6)"));
        return;
    }

    int position = stepper_6.currentPosition();
    EEPROM.put(eeprom_addr_gauge6, position);

    Serial.print(F("[EEPROM] Saved Gauge 6 position: "));
    Serial.println(position);
}

void load_gauge_positions()
{
    int position6;
    EEPROM.get(eeprom_addr_gauge6, position6);

    if (position6 < -5000 || position6 > 5000)
    {
        position6 = 0;
    }

    stepper_6.setCurrentPosition(position6);

    Serial.print(F("[EEPROM] Loaded Gauge 6 position: "));
    Serial.println(position6);
}

// ============================================================================
// STATUS PUBLISHING
// ============================================================================

void publish_hardware_status()
{
    if (!mqtt.isConnected())
        return;

    JsonDocument doc;
    doc["gauges_active"] = gauges_active;
    doc["gauge_6_position"] = stepper_6.currentPosition();
    doc["valve_6_psi"] = valve_6_psi;
    doc["ts"] = millis();

    mqtt.publishJson(CAT_SENSORS, (String(DEV_GAUGE_6) + "/" + SENSOR_VALVE_6_PSI).c_str(), doc);
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

String extract_command_value(const JsonDocument &payload)
{
    if (payload.containsKey("value"))
    {
        return payload["value"].as<String>();
    }
    return "";
}

// ============================================================================
// COMMAND ACKNOWLEDGEMENT
// ============================================================================

void publish_command_acknowledgement(const char *device_id, const char *command)
{
    if (!mqtt.isConnected())
        return;

    StaticJsonDocument<160> ack;
    ack["controller_id"] = CONTROLLER_ID;
    ack["device_id"] = device_id;
    ack["command"] = command;
    ack["success"] = true;
    ack["timestamp_ms"] = millis();

    char buf[196];
    serializeJson(ack, buf, sizeof(buf));

    // Topic: <tenant>/<room>/acknowledgement/<controller>/<device>/<command>
    String ackTopic = String(CLIENT_ID) + "/" + String(ROOM_ID) + "/" +
                      String(CAT_ACKNOWLEDGEMENT) + "/" + String(CONTROLLER_ID) + "/" +
                      String(device_id) + "/" + String(command);

    mqtt.get_client().publish(ackTopic.c_str(), buf, false);

    Serial.print(F("[ACK] -> "));
    Serial.println(ackTopic);
}
