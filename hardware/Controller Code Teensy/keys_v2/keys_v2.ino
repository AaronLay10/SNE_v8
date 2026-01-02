// ══════════════════════════════════════════════════════════════════════════════
// Keys Puzzle Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/**
 * Keys Puzzle Controller
 *
 * HARDWARE:
 * - 8x Key switches (4 color pairs: Green, Yellow, Blue, Red)
 * - 4x WS2812B LEDs (one per color box)
 * - Each color has two switches that must be pressed simultaneously
 *
 * STATELESS ARCHITECTURE:
 * - Publishes individual switch states AND pair states on change
 * - LED control via MQTT commands per box or all boxes
 * - Change-based publishing reduces MQTT traffic
 *
 * AUTHOR: Sentient Engine Team
 * TARGET: Teensy 4.1
 * ============================================================================
 */

#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <SentientCapabilityManifest.h>
#include <FastLED.h>

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

// Key switches (INPUT_PULLUP, active LOW)
const int pin_green_bottom = 3;
const int pin_green_right = 4;
const int pin_yellow_right = 5;
const int pin_yellow_top = 6;
const int pin_blue_left = 7;
const int pin_blue_bottom = 8;
const int pin_red_left = 9;
const int pin_red_bottom = 10;

// LEDs
const int led_pin = 2;
const int num_leds = 4;
const int power_led_pin = 13;

// LED indices
const int led_green = 0;
const int led_yellow = 1;
const int led_blue = 2;
const int led_red = 3;

CRGB leds[num_leds];

// Default LED colors (warm amber glow)
const CRGB default_green_color = CRGB(136, 99, 8);
const CRGB default_yellow_color = CRGB(136, 99, 8);
const CRGB default_blue_color = CRGB(136, 99, 8);
const CRGB default_red_color = CRGB(136, 99, 8);

// Current LED states and colors
bool green_led_on = true;
bool yellow_led_on = true;
bool blue_led_on = true;
bool red_led_on = true;

CRGB green_led_color = default_green_color;
CRGB yellow_led_color = default_yellow_color;
CRGB blue_led_color = default_blue_color;
CRGB red_led_color = default_red_color;

// ============================================================================
// SWITCH STATE TRACKING
// ============================================================================

// Individual switch states
bool green_bottom_state = false;
bool green_right_state = false;
bool yellow_right_state = false;
bool yellow_top_state = false;
bool blue_left_state = false;
bool blue_bottom_state = false;
bool red_left_state = false;
bool red_bottom_state = false;

// Pair states
bool green_pair_state = false;
bool yellow_pair_state = false;
bool blue_pair_state = false;
bool red_pair_state = false;

// Last published states (for change detection)
bool last_green_bottom = false;
bool last_green_right = false;
bool last_yellow_right = false;
bool last_yellow_top = false;
bool last_blue_left = false;
bool last_blue_bottom = false;
bool last_red_left = false;
bool last_red_bottom = false;

bool last_green_pair = false;
bool last_yellow_pair = false;
bool last_blue_pair = false;
bool last_red_pair = false;

bool sensors_initialized = false;

// Periodic publishing
unsigned long last_sensor_publish_time = 0;
const unsigned long sensor_publish_interval = 60000; // 60 seconds

// ============================================================================
// DEVICE REGISTRY
// ============================================================================

// Green key box
const char *green_box_commands[] = {
    CMD_GREEN_BOX_LED_ON,
    CMD_GREEN_BOX_LED_OFF,
    CMD_GREEN_BOX_COLOR};
const char *green_box_sensors[] = {
    SENSOR_GREEN_PAIR,
    SENSOR_GREEN_BOTTOM,
    SENSOR_GREEN_RIGHT};

// Yellow key box
const char *yellow_box_commands[] = {
    CMD_YELLOW_BOX_LED_ON,
    CMD_YELLOW_BOX_LED_OFF,
    CMD_YELLOW_BOX_COLOR};
const char *yellow_box_sensors[] = {
    SENSOR_YELLOW_PAIR,
    SENSOR_YELLOW_RIGHT,
    SENSOR_YELLOW_TOP};

// Blue key box
const char *blue_box_commands[] = {
    CMD_BLUE_BOX_LED_ON,
    CMD_BLUE_BOX_LED_OFF,
    CMD_BLUE_BOX_COLOR};
const char *blue_box_sensors[] = {
    SENSOR_BLUE_PAIR,
    SENSOR_BLUE_LEFT,
    SENSOR_BLUE_BOTTOM};

// Red key box
const char *red_box_commands[] = {
    CMD_RED_BOX_LED_ON,
    CMD_RED_BOX_LED_OFF,
    CMD_RED_BOX_COLOR};
const char *red_box_sensors[] = {
    SENSOR_RED_PAIR,
    SENSOR_RED_LEFT,
    SENSOR_RED_BOTTOM};

// Device definitions
SentientDeviceDef dev_green_box(
    DEV_GREEN_KEY_BOX,
    FRIENDLY_GREEN_KEY_BOX,
    "key_box",
    green_box_commands, 3,
    green_box_sensors, 3);

SentientDeviceDef dev_yellow_box(
    DEV_YELLOW_KEY_BOX,
    FRIENDLY_YELLOW_KEY_BOX,
    "key_box",
    yellow_box_commands, 3,
    yellow_box_sensors, 3);

SentientDeviceDef dev_blue_box(
    DEV_BLUE_KEY_BOX,
    FRIENDLY_BLUE_KEY_BOX,
    "key_box",
    blue_box_commands, 3,
    blue_box_sensors, 3);

SentientDeviceDef dev_red_box(
    DEV_RED_KEY_BOX,
    FRIENDLY_RED_KEY_BOX,
    "key_box",
    red_box_commands, 3,
    red_box_sensors, 3);

SentientDeviceRegistry deviceRegistry;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);
void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void publish_command_acknowledgement(const char *device_id, const char *command);
void read_switches();
void publish_sensor_changes(bool force_publish);
void update_leds();
String extract_command_value(const JsonDocument &payload);

// ============================================================================
// MQTT OBJECTS
// ============================================================================

SentientCapabilityManifest manifest;
SentientMQTT mqtt(build_mqtt_config());

// Registration retry state
bool registration_sent = false;
unsigned long last_registration_attempt_ms = 0;
const unsigned long registration_retry_interval_ms = 10000; // 10 seconds

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
    Serial.println("║        Sentient Engine - Keys Puzzle Controller v2        ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.print("[Keys] Firmware Version: ");
    Serial.println(firmware::VERSION);
    Serial.print("[Keys] Unique ID: ");
    Serial.println(firmware::UNIQUE_ID);
    Serial.print("[Keys] Controller ID: ");
    Serial.println(CONTROLLER_ID);
    Serial.println();

    // Configure power LED
    pinMode(power_led_pin, OUTPUT);
    digitalWrite(power_led_pin, HIGH);

    // Configure switch pins
    pinMode(pin_green_bottom, INPUT_PULLUP);
    pinMode(pin_green_right, INPUT_PULLUP);
    pinMode(pin_yellow_right, INPUT_PULLUP);
    pinMode(pin_yellow_top, INPUT_PULLUP);
    pinMode(pin_blue_left, INPUT_PULLUP);
    pinMode(pin_blue_bottom, INPUT_PULLUP);
    pinMode(pin_red_left, INPUT_PULLUP);
    pinMode(pin_red_bottom, INPUT_PULLUP);

    // Initialize FastLED
    FastLED.addLeds<WS2812B, led_pin, GRB>(leds, num_leds);
    FastLED.setBrightness(255);
    update_leds();
    Serial.println("[Keys] FastLED initialized (4 LEDs)");

    // Register devices
    Serial.println("[Keys] Registering devices...");
    deviceRegistry.addDevice(&dev_green_box);
    deviceRegistry.addDevice(&dev_yellow_box);
    deviceRegistry.addDevice(&dev_blue_box);
    deviceRegistry.addDevice(&dev_red_box);
    deviceRegistry.printSummary();

    // Build capability manifest
    Serial.println("[Keys] Building capability manifest...");
    build_capability_manifest();
    Serial.println("[Keys] Manifest built successfully");

    // Initialize MQTT
    Serial.println("[Keys] Initializing MQTT...");
    if (!mqtt.begin())
    {
        Serial.println("[Keys] MQTT initialization failed - continuing without network");
    }
    else
    {
        Serial.println("[Keys] MQTT initialization successful");
        mqtt.setCommandCallback(handle_mqtt_command);
        mqtt.setHeartbeatBuilder(build_heartbeat_payload);

        // Wait for broker connection
        Serial.println("[Keys] Waiting for broker connection...");
        unsigned long connection_start = millis();
        while (!mqtt.isConnected() && (millis() - connection_start < 5000))
        {
            mqtt.loop();
            delay(100);
        }

        if (mqtt.isConnected())
        {
            Serial.println("[Keys] Broker connected!");

            // Register with Sentient system
            Serial.println("[Keys] Registering with Sentient system...");
            if (manifest.publish_registration(mqtt.get_client(), ROOM_ID, CONTROLLER_ID))
            {
                Serial.println("[Keys] Registration successful!");
                registration_sent = true;
            }
            else
            {
                Serial.println("[Keys] Registration failed - will retry later");
                registration_sent = false;
                last_registration_attempt_ms = millis();
            }

            // Publish initial sensor states
            publish_sensor_changes(true);
        }
        else
        {
            Serial.println("[Keys] Broker connection timeout - continuing offline");
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
    read_switches();

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
            Serial.println(F("[Keys] Retrying registration..."));
            if (manifest.publish_registration(mqtt.get_client(), ROOM_ID, CONTROLLER_ID))
            {
                Serial.println(F("[Keys] Registration successful (retry)!"));
                registration_sent = true;
            }
            else
            {
                Serial.println(F("[Keys] Registration still failing"));
            }
            last_registration_attempt_ms = millis();
        }
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
    // Slightly larger buffer to ensure capability registration fits comfortably
    cfg.publishJsonCapacity = 3072;
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
    // GREEN BOX COMMANDS
    // =========================================

    if (cmd.equals(CMD_GREEN_BOX_LED_ON))
    {
        green_led_on = true;
        update_leds();
        Serial.println(F("[GREEN BOX] LED ON"));
    }
    else if (cmd.equals(CMD_GREEN_BOX_LED_OFF))
    {
        green_led_on = false;
        update_leds();
        Serial.println(F("[GREEN BOX] LED OFF"));
    }
    else if (cmd.equals(CMD_GREEN_BOX_COLOR))
    {
        if (payload["r"].is<int>() && payload["g"].is<int>() && payload["b"].is<int>())
        {
            green_led_color = CRGB(payload["r"], payload["g"], payload["b"]);
            update_leds();
            Serial.print(F("[GREEN BOX] Color set to RGB("));
            Serial.print(payload["r"].as<int>());
            Serial.print(F(", "));
            Serial.print(payload["g"].as<int>());
            Serial.print(F(", "));
            Serial.print(payload["b"].as<int>());
            Serial.println(F(")"));
        }
    }

    // =========================================
    // YELLOW BOX COMMANDS
    // =========================================

    else if (cmd.equals(CMD_YELLOW_BOX_LED_ON))
    {
        yellow_led_on = true;
        update_leds();
        Serial.println(F("[YELLOW BOX] LED ON"));
    }
    else if (cmd.equals(CMD_YELLOW_BOX_LED_OFF))
    {
        yellow_led_on = false;
        update_leds();
        Serial.println(F("[YELLOW BOX] LED OFF"));
    }
    else if (cmd.equals(CMD_YELLOW_BOX_COLOR))
    {
        if (payload["r"].is<int>() && payload["g"].is<int>() && payload["b"].is<int>())
        {
            yellow_led_color = CRGB(payload["r"], payload["g"], payload["b"]);
            update_leds();
            Serial.print(F("[YELLOW BOX] Color set to RGB("));
            Serial.print(payload["r"].as<int>());
            Serial.print(F(", "));
            Serial.print(payload["g"].as<int>());
            Serial.print(F(", "));
            Serial.print(payload["b"].as<int>());
            Serial.println(F(")"));
        }
    }

    // =========================================
    // BLUE BOX COMMANDS
    // =========================================

    else if (cmd.equals(CMD_BLUE_BOX_LED_ON))
    {
        blue_led_on = true;
        update_leds();
        Serial.println(F("[BLUE BOX] LED ON"));
    }
    else if (cmd.equals(CMD_BLUE_BOX_LED_OFF))
    {
        blue_led_on = false;
        update_leds();
        Serial.println(F("[BLUE BOX] LED OFF"));
    }
    else if (cmd.equals(CMD_BLUE_BOX_COLOR))
    {
        if (payload["r"].is<int>() && payload["g"].is<int>() && payload["b"].is<int>())
        {
            blue_led_color = CRGB(payload["r"], payload["g"], payload["b"]);
            update_leds();
            Serial.print(F("[BLUE BOX] Color set to RGB("));
            Serial.print(payload["r"].as<int>());
            Serial.print(F(", "));
            Serial.print(payload["g"].as<int>());
            Serial.print(F(", "));
            Serial.print(payload["b"].as<int>());
            Serial.println(F(")"));
        }
    }

    // =========================================
    // RED BOX COMMANDS
    // =========================================

    else if (cmd.equals(CMD_RED_BOX_LED_ON))
    {
        red_led_on = true;
        update_leds();
        Serial.println(F("[RED BOX] LED ON"));
    }
    else if (cmd.equals(CMD_RED_BOX_LED_OFF))
    {
        red_led_on = false;
        update_leds();
        Serial.println(F("[RED BOX] LED OFF"));
    }
    else if (cmd.equals(CMD_RED_BOX_COLOR))
    {
        if (payload["r"].is<int>() && payload["g"].is<int>() && payload["b"].is<int>())
        {
            red_led_color = CRGB(payload["r"], payload["g"], payload["b"]);
            update_leds();
            Serial.print(F("[RED BOX] Color set to RGB("));
            Serial.print(payload["r"].as<int>());
            Serial.print(F(", "));
            Serial.print(payload["g"].as<int>());
            Serial.print(F(", "));
            Serial.print(payload["b"].as<int>());
            Serial.println(F(")"));
        }
    }

    // =========================================
    // ALL BOXES COMMANDS
    // =========================================

    else if (cmd.equals(CMD_PANEL_LEDS_ON))
    {
        green_led_on = true;
        yellow_led_on = true;
        blue_led_on = true;
        red_led_on = true;
        update_leds();
        Serial.println(F("[ALL BOXES] LEDs ON"));
    }
    else if (cmd.equals(CMD_PANEL_LEDS_OFF))
    {
        green_led_on = false;
        yellow_led_on = false;
        blue_led_on = false;
        red_led_on = false;
        update_leds();
        Serial.println(F("[ALL BOXES] LEDs OFF"));
    }
    else
    {
        Serial.print(F("[WARNING] Unknown command: "));
        Serial.println(cmd);
        return;
    }

    // Get device_id from payload for acknowledgement
    const char *device_id = payload["device_id"].is<const char *>() ? payload["device_id"].as<const char *>() : CONTROLLER_ID;
    publish_command_acknowledgement(device_id, command);
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

// ============================================================================
// SWITCH READING
// ============================================================================

void read_switches()
{
    // Read individual switches (inverted: LOW = active = pressed)
    green_bottom_state = (digitalRead(pin_green_bottom) == LOW);
    green_right_state = (digitalRead(pin_green_right) == LOW);
    yellow_right_state = (digitalRead(pin_yellow_right) == LOW);
    yellow_top_state = (digitalRead(pin_yellow_top) == LOW);
    blue_left_state = (digitalRead(pin_blue_left) == LOW);
    blue_bottom_state = (digitalRead(pin_blue_bottom) == LOW);
    red_left_state = (digitalRead(pin_red_left) == LOW);
    red_bottom_state = (digitalRead(pin_red_bottom) == LOW);

    // Calculate pair states (both keys must be pressed simultaneously)
    green_pair_state = (green_bottom_state && green_right_state);
    yellow_pair_state = (yellow_right_state && yellow_top_state);
    blue_pair_state = (blue_left_state && blue_bottom_state);
    red_pair_state = (red_left_state && red_bottom_state);
}

// ============================================================================
// SENSOR PUBLISHING
// ============================================================================

void publish_sensor_changes(bool force_publish)
{
    if (!mqtt.isConnected())
        return;

    JsonDocument doc;

    // Publish individual switch states on change
    if (green_bottom_state != last_green_bottom || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = green_bottom_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_GREEN_KEY_BOX) + "/" + SENSOR_GREEN_BOTTOM).c_str(), doc);
        last_green_bottom = green_bottom_state;
    }

    if (green_right_state != last_green_right || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = green_right_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_GREEN_KEY_BOX) + "/" + SENSOR_GREEN_RIGHT).c_str(), doc);
        last_green_right = green_right_state;
    }

    if (yellow_right_state != last_yellow_right || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = yellow_right_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_YELLOW_KEY_BOX) + "/" + SENSOR_YELLOW_RIGHT).c_str(), doc);
        last_yellow_right = yellow_right_state;
    }

    if (yellow_top_state != last_yellow_top || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = yellow_top_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_YELLOW_KEY_BOX) + "/" + SENSOR_YELLOW_TOP).c_str(), doc);
        last_yellow_top = yellow_top_state;
    }

    if (blue_left_state != last_blue_left || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = blue_left_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_BLUE_KEY_BOX) + "/" + SENSOR_BLUE_LEFT).c_str(), doc);
        last_blue_left = blue_left_state;
    }

    if (blue_bottom_state != last_blue_bottom || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = blue_bottom_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_BLUE_KEY_BOX) + "/" + SENSOR_BLUE_BOTTOM).c_str(), doc);
        last_blue_bottom = blue_bottom_state;
    }

    if (red_left_state != last_red_left || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = red_left_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_RED_KEY_BOX) + "/" + SENSOR_RED_LEFT).c_str(), doc);
        last_red_left = red_left_state;
    }

    if (red_bottom_state != last_red_bottom || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = red_bottom_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_RED_KEY_BOX) + "/" + SENSOR_RED_BOTTOM).c_str(), doc);
        last_red_bottom = red_bottom_state;
    }

    // Publish pair states on change
    if (green_pair_state != last_green_pair || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = green_pair_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_GREEN_KEY_BOX) + "/" + SENSOR_GREEN_PAIR).c_str(), doc);
        last_green_pair = green_pair_state;
    }

    if (yellow_pair_state != last_yellow_pair || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = yellow_pair_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_YELLOW_KEY_BOX) + "/" + SENSOR_YELLOW_PAIR).c_str(), doc);
        last_yellow_pair = yellow_pair_state;
    }

    if (blue_pair_state != last_blue_pair || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = blue_pair_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_BLUE_KEY_BOX) + "/" + SENSOR_BLUE_PAIR).c_str(), doc);
        last_blue_pair = blue_pair_state;
    }

    if (red_pair_state != last_red_pair || !sensors_initialized || force_publish)
    {
        doc.clear();
        doc["state"] = red_pair_state ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_RED_KEY_BOX) + "/" + SENSOR_RED_PAIR).c_str(), doc);
        last_red_pair = red_pair_state;
    }

    if (!sensors_initialized)
    {
        sensors_initialized = true;
    }
}

// ============================================================================
// LED CONTROL
// ============================================================================

void update_leds()
{
    leds[led_green] = green_led_on ? green_led_color : CRGB::Black;
    leds[led_yellow] = yellow_led_on ? yellow_led_color : CRGB::Black;
    leds[led_blue] = blue_led_on ? blue_led_color : CRGB::Black;
    leds[led_red] = red_led_on ? red_led_color : CRGB::Black;
    FastLED.show();
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

String extract_command_value(const JsonDocument &payload)
{
    if (payload["value"].is<const char *>())
    {
        return payload["value"].as<String>();
    }
    return "";
}
