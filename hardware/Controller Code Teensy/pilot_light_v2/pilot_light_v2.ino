// ══════════════════════════════════════════════════════════════════════════════
// Pilot Light Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Pilot Light Controller
 *
 * Controlled Systems:
 * - Boiler Fire LED Animation (6 strips WS2812B in two banks)
 * - Boiler Monitor Power Relay
 * - Newell Post Power Relay
 * - Flange Status LED Strip (WS2812B)
 * - TCS34725 Color Temperature & Lux Sensor
 *
 * STATELESS ARCHITECTURE:
 * - Sensor REPORTS color temperature and lux changes (does NOT make decisions)
 * - Sentient decides all actions (power control, LED states, etc.)
 * - No autonomous behavior - pure input/output controller
 */

#define USE_NO_SEND_PWM
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 1024 // Raised for larger control/registration packets
#endif

#include <SentientCapabilityManifest.h>
#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <Adafruit_TCS34725.h>
#include <Wire.h>
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
// *** Boiler Fire LED Animation - 6 strips of WS2812B LEDs (two banks of three)
// *** Newell Post Power Control - Relay with 1 output pin
// *** Flange LED Strip - Single WS2812B LED strip for status indication
// *** Boiler Monitor Power Control - Relay to control power to the boiler monitor
// *** Color Sensor - TCS34725 for color temperature and lux measurement

// ──────────────────────────────────────────────────────────────────────────────
// Pin Assignments
// ──────────────────────────────────────────────────────────────────────────────
const int power_led_pin = 13;
const int led_pin_a = 2;           // Boiler Bank 1, Strip 1
const int led_pin_b = 3;           // Boiler Bank 1, Strip 2
const int led_pin_c = 4;           // Boiler Bank 1, Strip 3
const int led_pin_d = 5;           // Boiler Bank 2, Strip 1
const int led_pin_e = 6;           // Boiler Bank 2, Strip 2
const int led_pin_f = 7;           // Boiler Bank 2, Strip 3
const int boiler_monitor_pin = 10; // Boiler Monitor Power Relay
const int newell_power_pin = 9;    // Newell Post Power Relay
const int led_flange_pin = 24;     // Flange Status LED Strip

// ──────────────────────────────────────────────────────────────────────────────
// Configuration Constants
// ──────────────────────────────────────────────────────────────────────────────
const int num_leds = 34;
const unsigned long heartbeat_interval_ms = 5000;
const int color_temp_threshold = 50; // Change detection threshold for color sensor
const int lux_threshold = 10;        // Change detection threshold for lux

// ──────────────────────────────────────────────────────────────────────────────
// MQTT Configuration
// ──────────────────────────────────────────────────────────────────────────────
const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_username = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
static const size_t metadata_json_capacity = 1024;

// ──────────────────────────────────────────────────────────────────────────────
// Hardware State Variables
// ──────────────────────────────────────────────────────────────────────────────
bool fire_leds_active = false;
bool boiler_monitor_on = false;
bool newell_power_on = true;
bool flange_leds_on = true;
bool manual_heartbeat_requested = false;

// ──────────────────────────────────────────────────────────────────────────────
// Hardware Objects
// ──────────────────────────────────────────────────────────────────────────────
// LED arrays
CRGB leds_b1s1[num_leds];
CRGB leds_b1s2[num_leds];
CRGB leds_b1s3[num_leds];
CRGB leds_b2s1[num_leds];
CRGB leds_b2s2[num_leds];
CRGB leds_b2s3[num_leds];
CRGB leds_flange[num_leds];
CRGBPalette16 fire_palette;
int heat[num_leds];
int flame[num_leds];

// Sensor hardware
Adafruit_TCS34725 tcs(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_1X);
bool color_sensor_available = false;
uint16_t last_color_temp = 0;
uint16_t last_lux = 0;
unsigned long last_sensor_publish_time = 0;
const unsigned long sensor_publish_interval_ms = 60000; // Publish every 60 seconds regardless of change

// ============================================================================
// DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!) — Using controller_naming.h
// ============================================================================

// Define command arrays
const char *fire_leds_commands[] = {
    naming::CMD_FIRE_LEDS_ON,
    naming::CMD_FIRE_LEDS_OFF};

const char *monitor_commands[] = {
    naming::CMD_MONITOR_ON,
    naming::CMD_MONITOR_OFF};

const char *newell_commands[] = {
    naming::CMD_NEWELL_POWER_ON,
    naming::CMD_NEWELL_POWER_OFF};

const char *flange_commands[] = {
    naming::CMD_FLANGE_ON,
    naming::CMD_FLANGE_OFF};

const char *controller_commands[] = {
    naming::CMD_RESET,
    naming::CMD_REQUEST_STATUS};

// Define sensor arrays
const char *color_sensor_sensors[] = {
    naming::SENSOR_COLOR_TEMP,
    naming::SENSOR_LUX};

// Create device definitions with canonical IDs and friendly names
SentientDeviceDef dev_fire_leds(
    naming::DEV_FIRE_LEDS,
    naming::FRIENDLY_FIRE_LEDS,
    "led_strip",
    fire_leds_commands, 2);

SentientDeviceDef dev_monitor_relay(
    naming::DEV_MONITOR_POWER_RELAY,
    naming::FRIENDLY_MONITOR_RELAY,
    "relay",
    monitor_commands, 2);

SentientDeviceDef dev_newell_relay(
    naming::DEV_NEWELL_POWER_RELAY,
    naming::FRIENDLY_NEWELL_RELAY,
    "relay",
    newell_commands, 2);

SentientDeviceDef dev_flange_leds(
    naming::DEV_FLANGE_LEDS,
    naming::FRIENDLY_FLANGE_LEDS,
    "led_strip",
    flange_commands, 2);

SentientDeviceDef dev_color_sensor(
    naming::DEV_PILOTLIGHT_COLOR_SENSOR,
    naming::FRIENDLY_COLOR_SENSOR,
    "sensor",
    color_sensor_sensors, 2, true); // true = input device

SentientDeviceDef dev_controller(
    naming::DEV_CONTROLLER,
    naming::FRIENDLY_CONTROLLER,
    "controller",
    controller_commands, 2);

// Create the device registry
SentientDeviceRegistry deviceRegistry;

// ──────────────────────────────────────────────────────────────────────────────
// Forward Declarations
// ──────────────────────────────────────────────────────────────────────────────
void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);
void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void publish_command_acknowledgement(const char *device_id, const char *command);
void fill_fire_frame();
void check_and_publish_sensor_changes();
void publish_hardware_status();
void publish_full_status();
void publish_device_profile();
const char *get_hardware_label();
const char *build_device_identifier();
bool parse_truth(const String &value);
String extract_command_value(const JsonDocument &payload);

// MQTT objects
SentientCapabilityManifest manifest;
SentientMQTT mqtt(build_mqtt_config());

// ──────────────────────────────────────────────────────────────────────────────
// Network Diagnostics (added)
// ──────────────────────────────────────────────────────────────────────────────
void debug_network()
{
    Serial.println(F("[NetDiag] -------------------------"));
#if SENTIENT_HAS_NATIVE_ETHERNET
    Serial.print(F("[NetDiag] IP: "));
    Serial.println(Ethernet.localIP());
    Serial.print(F("[NetDiag] Subnet: "));
    Serial.println(Ethernet.subnetMask());
    Serial.print(F("[NetDiag] Gateway: "));
    Serial.println(Ethernet.gatewayIP());
    Serial.print(F("[NetDiag] DNS: "));
    Serial.println(Ethernet.dnsServerIP());

    EthernetClient c;
    Serial.print(F("[NetDiag] TCP test 192.168.20.3:1883 -> "));
    if (c.connect(IPAddress(192, 168, 20, 3), 1883))
    {
        Serial.println(F("SUCCESS"));
        c.stop();
    }
    else
    {
        Serial.println(F("FAIL"));
    }
#else
    Serial.println(F("[NetDiag] NativeEthernet not available; skipping TCP test"));
#endif
    Serial.println(F("[NetDiag] -------------------------"));

    // Simple /24 subnet mismatch detection vs broker IP (192.168.X.0/24)
#if SENTIENT_HAS_NATIVE_ETHERNET
    IPAddress ip = Ethernet.localIP();
    if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0)
    {
        Serial.println(F("[NetDiag] WARNING: No valid IP address assigned (DHCP failed?)"));
    }
    else if (ip[0] != mqtt_broker_ip[0] || ip[1] != mqtt_broker_ip[1] || ip[2] != mqtt_broker_ip[2])
    {
        Serial.print(F("[NetDiag] WARNING: Controller subnet ("));
        Serial.print(ip);
        Serial.print(F(") differs from broker subnet ("));
        Serial.print(mqtt_broker_ip);
        Serial.println(F(")"));
        Serial.println(F("[NetDiag] Action: Move Ethernet to broker VLAN or allow TCP/1883 routing between VLANs."));
    }
#endif
}

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

    Serial.println(F("=== Pilot Light Controller v2.0.1 - STATELESS MODE ==="));
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

    // Initialize GPIO pins
    pinMode(power_led_pin, OUTPUT);
    pinMode(boiler_monitor_pin, OUTPUT);
    pinMode(newell_power_pin, OUTPUT);

    digitalWrite(power_led_pin, HIGH);
    digitalWrite(boiler_monitor_pin, LOW);
    digitalWrite(newell_power_pin, HIGH); // Newell defaults to ON

    // Initialize LED strips
    FastLED.addLeds<WS2812B, led_pin_a, GRB>(leds_b1s1, num_leds);
    FastLED.addLeds<WS2812B, led_pin_b, GRB>(leds_b1s2, num_leds);
    FastLED.addLeds<WS2812B, led_pin_c, GRB>(leds_b1s3, num_leds);
    FastLED.addLeds<WS2812B, led_pin_d, GRB>(leds_b2s1, num_leds);
    FastLED.addLeds<WS2812B, led_pin_e, GRB>(leds_b2s2, num_leds);
    FastLED.addLeds<WS2812B, led_pin_f, GRB>(leds_b2s3, num_leds);
    FastLED.addLeds<WS2812B, led_flange_pin, GRB>(leds_flange, num_leds);
    FastLED.setBrightness(250);

    fire_palette = CRGBPalette16(0x3A79EB, 0x6495ED, 0xFC6000);
    randomSeed(analogRead(A0));

    // Set initial LED state
    FastLED.clear();
    fill_solid(leds_flange, num_leds, CRGB::Green);
    FastLED.show();

    // Try to initialize color sensor
    color_sensor_available = tcs.begin();
    Serial.print(F("Color sensor: "));
    Serial.println(color_sensor_available ? F("found") : F("missing"));

    // Register all devices (SINGLE SOURCE OF TRUTH!)
    Serial.println(F("[PilotLight] Registering devices..."));
    deviceRegistry.addDevice(&dev_fire_leds);
    deviceRegistry.addDevice(&dev_monitor_relay);
    deviceRegistry.addDevice(&dev_newell_relay);
    deviceRegistry.addDevice(&dev_flange_leds);
    deviceRegistry.addDevice(&dev_color_sensor);
    deviceRegistry.addDevice(&dev_controller);
    deviceRegistry.printSummary();

    // Build capability manifest
    Serial.println(F("[PilotLight] Building capability manifest..."));
    build_capability_manifest();
    Serial.println(F("[PilotLight] Manifest built successfully"));

    // Initialize MQTT
    Serial.println(F("[PilotLight] Initializing MQTT..."));
    if (!mqtt.begin())
    {
        Serial.println(F("[PilotLight] MQTT initialization failed - continuing without network"));
    }
    else
    {
        Serial.println(F("[PilotLight] MQTT initialization successful"));
        debug_network(); // Immediate network diagnostics
        mqtt.setCommandCallback(handle_mqtt_command);
        mqtt.setHeartbeatBuilder(build_heartbeat_payload);

        // Wait for broker connection (max 5 seconds)
        Serial.println(F("[PilotLight] Waiting for broker connection..."));
        unsigned long connection_start = millis();
        while (!mqtt.isConnected() && (millis() - connection_start < 5000))
        {
            mqtt.loop();
            delay(100);
        }

        if (mqtt.isConnected())
        {
            Serial.println(F("[PilotLight] Broker connected!"));

            // Register with Sentient system
            Serial.println(F("[PilotLight] Registering with Sentient system..."));
            if (manifest.publish_registration(mqtt.get_client(), naming::ROOM_ID, naming::CONTROLLER_ID))
            {
                Serial.println(F("[PilotLight] Registration successful!"));
            }
            else
            {
                Serial.println(F("[PilotLight] Registration failed - will retry later"));
            }

            // Publish initial status
            publish_device_profile();
            publish_full_status();
        }
        else
        {
            Serial.println(F("[PilotLight] Broker connection timeout - will retry in main loop"));
        }
    }

    Serial.println(F("[PilotLight] Ready - awaiting Sentient commands"));
    Serial.print(F("[PilotLight] Firmware: "));
    Serial.println(firmware::VERSION);
}

// ============================================================================
// SECTION 3: LOOP FUNCTION
// ============================================================================

void loop()
{
    // 1. LISTEN for commands from Sentient
    mqtt.loop();

    // 2. DETECT sensor changes and publish if needed
    check_and_publish_sensor_changes();

    // 3. EXECUTE active hardware operations
    if (fire_leds_active)
    {
        fill_fire_frame(); // Run fire animation
    }

    // Handle manual heartbeat requests
    if (manual_heartbeat_requested)
    {
        if (mqtt.publishHeartbeat())
        {
            manual_heartbeat_requested = false;
        }
    }
}

// ============================================================================
// SECTION 4: COMMANDS AND ASSOCIATED FUNCTIONS
// ============================================================================

void handle_mqtt_command(const char *command, const JsonDocument &payload, void * /*ctx*/)
{
    String value = extract_command_value(payload);
    String cmd = String(command);

    Serial.print(F("[PilotLight] Command: "));
    Serial.print(command);
    Serial.print(F(" Value: "));
    Serial.println(value);

    // ──────────────────────────────────────────────────────────────────────────
    // FIRE LEDS COMMANDS
    // ──────────────────────────────────────────────────────────────────────────
    if (cmd.equals(naming::CMD_FIRE_LEDS_ON))
    {
        fire_leds_active = true;
        Serial.println(F("[PilotLight] Fire LEDs: ON"));
        publish_hardware_status();
    }
    else if (cmd.equals(naming::CMD_FIRE_LEDS_OFF))
    {
        fire_leds_active = false;
        // Turn all fire LED strips to black
        fill_solid(leds_b1s1, num_leds, CRGB::Black);
        fill_solid(leds_b1s2, num_leds, CRGB::Black);
        fill_solid(leds_b1s3, num_leds, CRGB::Black);
        fill_solid(leds_b2s1, num_leds, CRGB::Black);
        fill_solid(leds_b2s2, num_leds, CRGB::Black);
        fill_solid(leds_b2s3, num_leds, CRGB::Black);
        FastLED.show();
        Serial.println(F("[PilotLight] Fire LEDs: OFF"));
        publish_hardware_status();
    }

    // ──────────────────────────────────────────────────────────────────────────
    // MONITOR RELAY COMMANDS
    // ──────────────────────────────────────────────────────────────────────────
    else if (cmd.equals(naming::CMD_MONITOR_ON))
    {
        boiler_monitor_on = true;
        digitalWrite(boiler_monitor_pin, HIGH);
        Serial.println(F("[PilotLight] Boiler Monitor: ON"));
        publish_hardware_status();
    }
    else if (cmd.equals(naming::CMD_MONITOR_OFF))
    {
        boiler_monitor_on = false;
        digitalWrite(boiler_monitor_pin, LOW);
        Serial.println(F("[PilotLight] Boiler Monitor: OFF"));
        publish_hardware_status();
    }

    // ──────────────────────────────────────────────────────────────────────────
    // NEWELL POWER RELAY COMMANDS
    // ──────────────────────────────────────────────────────────────────────────
    else if (cmd.equals(naming::CMD_NEWELL_POWER_ON))
    {
        newell_power_on = true;
        digitalWrite(newell_power_pin, HIGH);
        Serial.println(F("[PilotLight] Newell Power: ON"));
        publish_hardware_status();
    }
    else if (cmd.equals(naming::CMD_NEWELL_POWER_OFF))
    {
        newell_power_on = false;
        digitalWrite(newell_power_pin, LOW);
        Serial.println(F("[PilotLight] Newell Power: OFF"));
        publish_hardware_status();
    }

    // ──────────────────────────────────────────────────────────────────────────
    // FLANGE LED COMMANDS
    // ──────────────────────────────────────────────────────────────────────────
    else if (cmd.equals(naming::CMD_FLANGE_ON))
    {
        flange_leds_on = true;
        fill_solid(leds_flange, num_leds, CRGB::Green);
        FastLED.show();
        Serial.println(F("[PilotLight] Flange LEDs: ON (Green)"));
        publish_hardware_status();
    }
    else if (cmd.equals(naming::CMD_FLANGE_OFF))
    {
        flange_leds_on = false;
        fill_solid(leds_flange, num_leds, CRGB::Black);
        FastLED.show();
        Serial.println(F("[PilotLight] Flange LEDs: OFF"));
        publish_hardware_status();
    }

    // ──────────────────────────────────────────────────────────────────────────
    // CONTROLLER-LEVEL COMMANDS
    // ──────────────────────────────────────────────────────────────────────────
    else if (cmd.equals(naming::CMD_REQUEST_STATUS))
    {
        Serial.println(F("[PilotLight] Status requested"));
        publish_full_status();
    }
    else if (cmd.equals(naming::CMD_RESET))
    {
        Serial.println(F("[PilotLight] RESET command"));
        fire_leds_active = false;
        boiler_monitor_on = false;
        newell_power_on = false;
        flange_leds_on = false;

        // Turn off all LEDs
        fill_solid(leds_b1s1, num_leds, CRGB::Black);
        fill_solid(leds_b1s2, num_leds, CRGB::Black);
        fill_solid(leds_b1s3, num_leds, CRGB::Black);
        fill_solid(leds_b2s1, num_leds, CRGB::Black);
        fill_solid(leds_b2s2, num_leds, CRGB::Black);
        fill_solid(leds_b2s3, num_leds, CRGB::Black);
        fill_solid(leds_flange, num_leds, CRGB::Red); // Red = reset/error state
        FastLED.show();

        // Turn off all relays
        digitalWrite(boiler_monitor_pin, LOW);
        digitalWrite(newell_power_pin, LOW);

        publish_hardware_status();
        Serial.println(F("[PilotLight] Hardware reset complete"));
    }

    // ──────────────────────────────────────────────────────────────────────────
    // UNKNOWN COMMAND
    // ──────────────────────────────────────────────────────────────────────────
    else
    {
        Serial.print(F("[PilotLight] Unknown command: "));
        Serial.println(command);
        return;
    }

    // Get device_id from payload for acknowledgement
    const char *device_id = payload["device_id"].is<const char *>() ? payload["device_id"].as<const char *>() : naming::CONTROLLER_ID;
    publish_command_acknowledgement(device_id, command);
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

// Hardware execution function - Fire animation
void fill_fire_frame()
{
    static int boom_index = -1;
    static unsigned long last_debug = 0;

    // Debug output every 5 seconds
    if (millis() - last_debug > 5000)
    {
        Serial.println(F("[PilotLight] Fire animation running..."));
        last_debug = millis();
    }

    // Fire animation logic
    int i = random(0, num_leds);
    heat[i] = qsub8(heat[i], random8(1, 10));
    flame[i] = random(-50, 50);
    if (flame[i] == 0)
    {
        flame[i] = random(1, 50);
    }

    for (int idx = 0; idx < num_leds; idx++)
    {
        heat[idx] = min(255, max(100, heat[idx] + flame[idx]));

        CRGB color = ColorFromPalette(fire_palette, scale8(heat[idx], 240), scale8(heat[idx], 250), LINEARBLEND);
        leds_b1s1[idx] = color;
        leds_b1s2[idx] = color;
        leds_b1s3[idx] = color;
        leds_b2s1[idx] = color;
        leds_b2s2[idx] = color;
        leds_b2s3[idx] = color;

        if (heat[idx] > 250)
        {
            boom_index = idx;
        }

        flame[idx] = (heat[idx] + flame[idx] > 255) ? random(-50, -1)
                     : (heat[idx] < 0)              ? random(1, 50)
                                                    : flame[idx];

        if (heat[idx] < 250 && boom_index == idx)
        {
            boom_index = -1;
        }
    }

    FastLED.show();
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
    cfg.username = mqtt_username;
    cfg.password = mqtt_password;
    cfg.namespaceId = naming::CLIENT_ID;
    cfg.roomId = naming::ROOM_ID;
    cfg.controllerId = naming::CONTROLLER_ID;
    cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
    cfg.displayName = naming::CONTROLLER_FRIENDLY_NAME;
    cfg.publishJsonCapacity = 1536; // Increased for large registration messages
    cfg.heartbeatIntervalMs = heartbeat_interval_ms;
    cfg.autoHeartbeat = true;
#if !defined(ESP32)
    cfg.useDhcp = true;
#endif

    // Debug output for MQTT configuration
    Serial.println(F("\n[PilotLight] MQTT Configuration:"));
    Serial.print(F("  Broker IP: "));
    Serial.println(cfg.brokerIp);
    Serial.print(F("  Broker Host: "));
    Serial.println(cfg.brokerHost ? cfg.brokerHost : "null");
    Serial.print(F("  Broker Port: "));
    Serial.println(cfg.brokerPort);
    Serial.print(F("  Username: "));
    Serial.println(cfg.username ? cfg.username : "null");
    Serial.print(F("  Password: "));
    Serial.println(cfg.password ? "***SET***" : "null");
    Serial.print(F("  Client ID (deviceId): "));
    Serial.println(cfg.deviceId);
    Serial.print(F("  Namespace: "));
    Serial.println(cfg.namespaceId);
    Serial.print(F("  Room ID: "));
    Serial.println(cfg.roomId);
    Serial.println();

    return cfg;
}

// ──────────────────────────────────────────────────────────────────────────────
// Heartbeat Builder - Minimal identity only
// ──────────────────────────────────────────────────────────────────────────────
bool build_heartbeat_payload(JsonDocument &doc, void * /*ctx*/)
{
    doc["uid"] = naming::CONTROLLER_ID;
    doc["fw"] = firmware::VERSION;
    doc["up"] = millis();
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Sensor Monitoring - Change-based + Periodic publishing
// Publishes when values change OR every 60 seconds to ensure fresh data
// ──────────────────────────────────────────────────────────────────────────────
void check_and_publish_sensor_changes()
{
    // Try to initialize sensor if not available
    if (!color_sensor_available)
    {
        color_sensor_available = tcs.begin();
        if (!color_sensor_available)
        {
            return;
        }
    }

    // Read current sensor values
    uint16_t r, g, b, c;
    tcs.getRawData(&r, &g, &b, &c);
    uint16_t color_temp = tcs.calculateColorTemperature_dn40(r, g, b, c);
    uint16_t lux = tcs.calculateLux(r, g, b);

    // Check if values changed beyond threshold OR if it's been too long since last publish
    unsigned long current_time = millis();
    bool changed = false;
    bool force_publish = (current_time - last_sensor_publish_time >= sensor_publish_interval_ms);

    // Check if either value changed beyond threshold
    if (abs((int)color_temp - (int)last_color_temp) >= color_temp_threshold)
    {
        changed = true;
    }

    if (abs((int)lux - (int)last_lux) >= lux_threshold)
    {
        changed = true;
    }

    // Publish if something changed OR if periodic interval has elapsed
    if (changed || force_publish)
    {
        // Publish both sensor values using category/item format
        JsonDocument doc_temp;
        doc_temp["value"] = color_temp;
        doc_temp["ts"] = millis();

        JsonDocument doc_lux;
        doc_lux["value"] = lux;
        doc_lux["ts"] = millis();

        // Publish to MQTT using category and item
        // Category: sensors, Item: <device_id>/<sensor_name>
        String item_temp = String(naming::DEV_PILOTLIGHT_COLOR_SENSOR) + "/" + naming::SENSOR_COLOR_TEMP;
        String item_lux = String(naming::DEV_PILOTLIGHT_COLOR_SENSOR) + "/" + naming::SENSOR_LUX;

        mqtt.publishJson(naming::CAT_SENSORS, item_temp.c_str(), doc_temp);
        mqtt.publishJson(naming::CAT_SENSORS, item_lux.c_str(), doc_lux);

        // Update last published values
        last_color_temp = color_temp;
        last_lux = lux;
        last_sensor_publish_time = current_time;

        if (force_publish)
        {
            Serial.println(F("[PilotLight] Periodic sensor update - published"));
        }
        else
        {
            Serial.print(F("[PilotLight] Sensor change detected - Color: "));
            Serial.print(color_temp);
            Serial.print(F("K, Lux: "));
            Serial.println(lux);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Status Publishing
// ──────────────────────────────────────────────────────────────────────────────
void publish_hardware_status()
{
    JsonDocument doc;
    doc["fire_leds"] = fire_leds_active;
    doc["monitor_power"] = boiler_monitor_on;
    doc["newell_power"] = newell_power_on;
    doc["flange_leds"] = flange_leds_on;
    doc["ts"] = millis();
    doc["uid"] = naming::CONTROLLER_ID;

    mqtt.publishJson(naming::CAT_STATUS, naming::ITEM_HARDWARE, doc);
}

void publish_full_status()
{
    JsonDocument doc;

    // Current sensor readings
    if (color_sensor_available)
    {
        uint16_t r, g, b, c;
        tcs.getRawData(&r, &g, &b, &c);
        doc["color_temp"] = tcs.calculateColorTemperature_dn40(r, g, b, c);
        doc["lux"] = tcs.calculateLux(r, g, b);
    }
    else
    {
        doc["color_temp"] = 0;
        doc["lux"] = 0;
    }

    // Current hardware state
    doc["fire_leds"] = fire_leds_active;
    doc["monitor_power"] = boiler_monitor_on;
    doc["newell_power"] = newell_power_on;
    doc["flange_leds"] = flange_leds_on;
    doc["uptime"] = millis();
    doc["ts"] = millis();
    doc["uid"] = naming::CONTROLLER_ID;
    doc["fw"] = firmware::VERSION;

    mqtt.publishJson(naming::CAT_STATUS, "full", doc);
    Serial.println(F("[PilotLight] Full status published"));
}

void publish_device_profile()
{
    JsonDocument doc;
    doc["uid"] = naming::CONTROLLER_ID;
    doc["displayName"] = naming::CONTROLLER_FRIENDLY_NAME;
    doc["name"] = naming::CONTROLLER_FRIENDLY_NAME;
    doc["roomLabel"] = naming::ROOM_ID;
    doc["puzzleId"] = naming::CONTROLLER_ID;
    doc["hardware"] = get_hardware_label();
    doc["deviceId"] = build_device_identifier();
    doc["fw"] = firmware::VERSION;

    JsonObject outputs = doc["outputs"].to<JsonObject>();
    outputs["boiler_monitor"] = boiler_monitor_pin;
    outputs["newell_power"] = newell_power_pin;

    mqtt.publishJson(naming::CAT_EVENTS, "profile", doc);
}

// ──────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ──────────────────────────────────────────────────────────────────────────────
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
    board.replace("Teensy ", "teensy");
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

bool parse_truth(const String &value)
{
    if (value.length() == 0)
    {
        return false;
    }
    if (value.equalsIgnoreCase("on") || value.equalsIgnoreCase("true"))
    {
        return true;
    }
    if (value.equalsIgnoreCase("off") || value.equalsIgnoreCase("false"))
    {
        return false;
    }
    return value.toInt() != 0;
}

String extract_command_value(const JsonDocument &payload)
{
    if (payload.is<JsonObjectConst>())
    {
        auto obj = payload.as<JsonObjectConst>();
        if (obj.containsKey("value"))
        {
            return obj["value"].as<String>();
        }
        if (obj.containsKey("state"))
        {
            return obj["state"].as<String>();
        }
        if (obj.containsKey("command"))
        {
            return obj["command"].as<String>();
        }
    }
    else if (payload.is<const char *>())
    {
        return payload.as<const char *>();
    }
    else if (payload.is<long>())
    {
        return String(payload.as<long>());
    }
    else if (payload.is<int>())
    {
        return String(payload.as<int>());
    }
    else if (payload.is<float>())
    {
        return String(payload.as<float>(), 3);
    }
    return String();
}
