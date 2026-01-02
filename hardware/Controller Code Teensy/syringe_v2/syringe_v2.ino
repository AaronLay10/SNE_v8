// ══════════════════════════════════════════════════════════════════════════════
// Syringe Puzzle Controller v2.1.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include "controller_naming.h"

// ══════════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const byte PIN_POWER_LED = 13;
const byte PIN_FILAMENT_LED = 30;

// LED Ring Pins
const byte PIN_LED_RING_A = 24;
const byte PIN_LED_RING_B = 25;
const byte PIN_LED_RING_C = 26;
const byte PIN_LED_RING_D = 27;
const byte PIN_LED_RING_E = 28;
const byte PIN_LED_RING_F = 29;
const int NUM_LEDS = 12;

// Actuator Pins
const byte PIN_FORGE_ACTUATOR_RETRACT = 31;
const byte PIN_FORGE_ACTUATOR_EXTEND = 32;
const byte PIN_ACTUATORS_UP = 33;
const byte PIN_ACTUATORS_DN = 34;

// Rotary Encoder Pins
const byte PIN_ENCODER_LT_A = 18;
const byte PIN_ENCODER_LT_B = 19;
const byte PIN_ENCODER_LM_A = 16;
const byte PIN_ENCODER_LM_B = 17;
const byte PIN_ENCODER_LB_A = 14;
const byte PIN_ENCODER_LB_B = 15;
const byte PIN_ENCODER_RT_A = 20;
const byte PIN_ENCODER_RT_B = 21;
const byte PIN_ENCODER_RM_A = 40;
const byte PIN_ENCODER_RM_B = 41;
const byte PIN_ENCODER_RB_A = 22;
const byte PIN_ENCODER_RB_B = 23;

// ══════════════════════════════════════════════════════════════════════════════
// MQTT CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_username = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
const unsigned long heartbeat_interval_ms = 5000; // 5 seconds

// ══════════════════════════════════════════════════════════════════════════════
// STATE MANAGEMENT
// ══════════════════════════════════════════════════════════════════════════════

// LED Arrays
CRGB ring_a[NUM_LEDS];
CRGB ring_b[NUM_LEDS];
CRGB ring_c[NUM_LEDS];
CRGB ring_d[NUM_LEDS];
CRGB ring_e[NUM_LEDS];
CRGB ring_f[NUM_LEDS];

// Encoder values
volatile long encoder_values[6] = {0};
int last_encoded[6] = {0};
long last_published_values[6] = {0};

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

// Encoder sensors (input only)
const char *encoder_sensors[] = {naming::SENSOR_ENCODER_COUNT};

SentientDeviceDef dev_encoder_lt(naming::DEV_ENCODER_LT, "Encoder Left Top", "rotary_encoder", encoder_sensors, 1, true);
SentientDeviceDef dev_encoder_lm(naming::DEV_ENCODER_LM, "Encoder Left Middle", "rotary_encoder", encoder_sensors, 1, true);
SentientDeviceDef dev_encoder_lb(naming::DEV_ENCODER_LB, "Encoder Left Bottom", "rotary_encoder", encoder_sensors, 1, true);
SentientDeviceDef dev_encoder_rt(naming::DEV_ENCODER_RT, "Encoder Right Top", "rotary_encoder", encoder_sensors, 1, true);
SentientDeviceDef dev_encoder_rm(naming::DEV_ENCODER_RM, "Encoder Right Middle", "rotary_encoder", encoder_sensors, 1, true);
SentientDeviceDef dev_encoder_rb(naming::DEV_ENCODER_RB, "Encoder Right Bottom", "rotary_encoder", encoder_sensors, 1, true);

// LED Ring devices (output)
const char *led_commands[] = {naming::CMD_SET_COLOR};
SentientDeviceDef dev_led_ring_a(naming::DEV_LED_RING_A, "LED Ring A", "led_ring", led_commands, 1);
SentientDeviceDef dev_led_ring_b(naming::DEV_LED_RING_B, "LED Ring B", "led_ring", led_commands, 1);
SentientDeviceDef dev_led_ring_c(naming::DEV_LED_RING_C, "LED Ring C", "led_ring", led_commands, 1);
SentientDeviceDef dev_led_ring_d(naming::DEV_LED_RING_D, "LED Ring D", "led_ring", led_commands, 1);
SentientDeviceDef dev_led_ring_e(naming::DEV_LED_RING_E, "LED Ring E", "led_ring", led_commands, 1);
SentientDeviceDef dev_led_ring_f(naming::DEV_LED_RING_F, "LED Ring F", "led_ring", led_commands, 1);

// Filament LED (output)
const char *filament_commands[] = {naming::CMD_LED_ON, naming::CMD_LED_OFF};
SentientDeviceDef dev_filament_led(naming::DEV_FILAMENT_LED, "Filament LED", "led", filament_commands, 2);

// Main Actuator (output)
const char *actuator_commands[] = {naming::CMD_ACTUATOR_UP, naming::CMD_ACTUATOR_DOWN, naming::CMD_ACTUATOR_STOP};
SentientDeviceDef dev_main_actuator(naming::DEV_MAIN_ACTUATOR, "Main Actuator", "actuator", actuator_commands, 3);

// Forge Actuator (output)
const char *forge_commands[] = {naming::CMD_FORGE_EXTEND, naming::CMD_FORGE_RETRACT};
SentientDeviceDef dev_forge_actuator(naming::DEV_FORGE_ACTUATOR, "Forge Actuator", "actuator", forge_commands, 2);

// Create the device registry
SentientDeviceRegistry deviceRegistry;

// ══════════════════════════════════════════════════════════════════════════════
// SENTIENT MQTT INITIALIZATION
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
    cfg.heartbeatIntervalMs = heartbeat_interval_ms;
    cfg.autoHeartbeat = true;
    cfg.useDhcp = true;
    return cfg;
}

SentientMQTT sentient(make_mqtt_config());
SentientCapabilityManifest manifest;

// Forward declarations
void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void publish_command_acknowledgement(const char *device_id, const char *command);
void monitor_encoders();
void update_encoder(int index, int pinA, int pinB);
void set_led_ring_color(CRGB *ring, const char *color);

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    pinMode(PIN_FILAMENT_LED, OUTPUT);
    digitalWrite(PIN_FILAMENT_LED, HIGH);

    pinMode(PIN_ACTUATORS_UP, OUTPUT);
    pinMode(PIN_ACTUATORS_DN, OUTPUT);
    digitalWrite(PIN_ACTUATORS_UP, HIGH);
    digitalWrite(PIN_ACTUATORS_DN, LOW);

    pinMode(PIN_FORGE_ACTUATOR_EXTEND, OUTPUT);
    pinMode(PIN_FORGE_ACTUATOR_RETRACT, OUTPUT);
    digitalWrite(PIN_FORGE_ACTUATOR_EXTEND, LOW);
    digitalWrite(PIN_FORGE_ACTUATOR_RETRACT, HIGH);

    // Initialize FastLED
    FastLED.addLeds<WS2812B, PIN_LED_RING_A, GRB>(ring_a, NUM_LEDS);
    FastLED.addLeds<WS2812B, PIN_LED_RING_B, GRB>(ring_b, NUM_LEDS);
    FastLED.addLeds<WS2812B, PIN_LED_RING_C, GRB>(ring_c, NUM_LEDS);
    FastLED.addLeds<WS2812B, PIN_LED_RING_D, GRB>(ring_d, NUM_LEDS);
    FastLED.addLeds<WS2812B, PIN_LED_RING_E, GRB>(ring_e, NUM_LEDS);
    FastLED.addLeds<WS2812B, PIN_LED_RING_F, GRB>(ring_f, NUM_LEDS);

    // Set initial colors
    fill_solid(ring_a, NUM_LEDS, CRGB::Blue);
    fill_solid(ring_b, NUM_LEDS, CRGB::Red);
    fill_solid(ring_c, NUM_LEDS, CRGB::Yellow);
    fill_solid(ring_d, NUM_LEDS, CRGB::Green);
    fill_solid(ring_e, NUM_LEDS, CRGB::Purple);
    fill_solid(ring_f, NUM_LEDS, CRGB::Orange);
    FastLED.show();

    // Setup encoder pins
    pinMode(PIN_ENCODER_LT_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_LT_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_LM_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_LM_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_LB_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_LB_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_RT_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_RT_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_RM_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_RM_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_RB_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_RB_B, INPUT_PULLUP);

    // Attach interrupts
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LT_A), []()
                    { update_encoder(0, PIN_ENCODER_LT_A, PIN_ENCODER_LT_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LT_B), []()
                    { update_encoder(0, PIN_ENCODER_LT_A, PIN_ENCODER_LT_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LM_A), []()
                    { update_encoder(1, PIN_ENCODER_LM_A, PIN_ENCODER_LM_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LM_B), []()
                    { update_encoder(1, PIN_ENCODER_LM_A, PIN_ENCODER_LM_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LB_A), []()
                    { update_encoder(2, PIN_ENCODER_LB_A, PIN_ENCODER_LB_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LB_B), []()
                    { update_encoder(2, PIN_ENCODER_LB_A, PIN_ENCODER_LB_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RT_A), []()
                    { update_encoder(3, PIN_ENCODER_RT_A, PIN_ENCODER_RT_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RT_B), []()
                    { update_encoder(3, PIN_ENCODER_RT_A, PIN_ENCODER_RT_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RM_A), []()
                    { update_encoder(4, PIN_ENCODER_RM_A, PIN_ENCODER_RM_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RM_B), []()
                    { update_encoder(4, PIN_ENCODER_RM_A, PIN_ENCODER_RM_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RB_A), []()
                    { update_encoder(5, PIN_ENCODER_RB_A, PIN_ENCODER_RB_B); }, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RB_B), []()
                    { update_encoder(5, PIN_ENCODER_RB_A, PIN_ENCODER_RB_B); }, CHANGE);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Syringe] Starting..."));

    // Register all devices
    Serial.println(F("[INIT] Registering devices..."));
    deviceRegistry.addDevice(&dev_encoder_lt);
    deviceRegistry.addDevice(&dev_encoder_lm);
    deviceRegistry.addDevice(&dev_encoder_lb);
    deviceRegistry.addDevice(&dev_encoder_rt);
    deviceRegistry.addDevice(&dev_encoder_rm);
    deviceRegistry.addDevice(&dev_encoder_rb);
    deviceRegistry.addDevice(&dev_led_ring_a);
    deviceRegistry.addDevice(&dev_led_ring_b);
    deviceRegistry.addDevice(&dev_led_ring_c);
    deviceRegistry.addDevice(&dev_led_ring_d);
    deviceRegistry.addDevice(&dev_led_ring_e);
    deviceRegistry.addDevice(&dev_led_ring_f);
    deviceRegistry.addDevice(&dev_filament_led);
    deviceRegistry.addDevice(&dev_main_actuator);
    deviceRegistry.addDevice(&dev_forge_actuator);
    deviceRegistry.printSummary();

    // Build capability manifest
    Serial.println(F("[INIT] Building capability manifest..."));
    manifest.set_controller_info(
        naming::CONTROLLER_ID,
        naming::CONTROLLER_FRIENDLY_NAME,
        firmware::VERSION,
        naming::ROOM_ID,
        naming::CONTROLLER_ID);

    deviceRegistry.buildManifest(manifest);
    Serial.println(F("[INIT] Manifest built from device registry"));

    // Initialize Sentient MQTT
    sentient.begin();
    sentient.setCommandCallback(handle_mqtt_command);

    Serial.println(F("[INIT] Sentient MQTT initialized"));
    Serial.println(F("[INIT] Waiting for network connection..."));

    // Wait for broker connection (max 5 seconds)
    unsigned long connection_start = millis();
    while (!sentient.isConnected() && (millis() - connection_start < 5000))
    {
        sentient.loop();
        delay(100);
    }

    if (sentient.isConnected())
    {
        Serial.println(F("[INIT] Broker connected!"));

        // Register with Sentient system
        Serial.println(F("[INIT] Registering with Sentient system..."));
        if (manifest.publish_registration(sentient.get_client(), naming::ROOM_ID, naming::CONTROLLER_ID))
        {
            Serial.println(F("[INIT] Registration successful!"));
        }
        else
        {
            Serial.println(F("[INIT] Registration failed - will retry later"));
        }
    }
    else
    {
        Serial.println(F("[INIT] Broker connection timeout - will retry in loop"));
    }

    Serial.println(F("[Syringe] Ready - awaiting Sentient commands"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    // 1. LISTEN for commands from Sentient
    sentient.loop();

    // 2. MONITOR encoders and publish changes
    monitor_encoders();
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    if (!payload.containsKey("device_id"))
    {
        Serial.println(F("[CMD] Error: No device_id in payload"));
        return;
    }

    const char *device_id = payload["device_id"];

    Serial.print(F("[CMD] Device: "));
    Serial.print(device_id);
    Serial.print(F(" Command: "));
    Serial.println(command);

    // LED Ring commands
    if (strcmp(device_id, naming::DEV_LED_RING_A) == 0 && strcmp(command, naming::CMD_SET_COLOR) == 0)
    {
        set_led_ring_color(ring_a, payload["color"]);
        publish_command_acknowledgement(device_id, command);
    }
    else if (strcmp(device_id, naming::DEV_LED_RING_B) == 0 && strcmp(command, naming::CMD_SET_COLOR) == 0)
    {
        set_led_ring_color(ring_b, payload["color"]);
        publish_command_acknowledgement(device_id, command);
    }
    else if (strcmp(device_id, naming::DEV_LED_RING_C) == 0 && strcmp(command, naming::CMD_SET_COLOR) == 0)
    {
        set_led_ring_color(ring_c, payload["color"]);
        publish_command_acknowledgement(device_id, command);
    }
    else if (strcmp(device_id, naming::DEV_LED_RING_D) == 0 && strcmp(command, naming::CMD_SET_COLOR) == 0)
    {
        set_led_ring_color(ring_d, payload["color"]);
        publish_command_acknowledgement(device_id, command);
    }
    else if (strcmp(device_id, naming::DEV_LED_RING_E) == 0 && strcmp(command, naming::CMD_SET_COLOR) == 0)
    {
        set_led_ring_color(ring_e, payload["color"]);
        publish_command_acknowledgement(device_id, command);
    }
    else if (strcmp(device_id, naming::DEV_LED_RING_F) == 0 && strcmp(command, naming::CMD_SET_COLOR) == 0)
    {
        set_led_ring_color(ring_f, payload["color"]);
        publish_command_acknowledgement(device_id, command);
    }
    // Filament LED commands
    else if (strcmp(device_id, naming::DEV_FILAMENT_LED) == 0)
    {
        if (strcmp(command, naming::CMD_LED_ON) == 0)
        {
            digitalWrite(PIN_FILAMENT_LED, HIGH);
            Serial.println(F("[CMD] Filament LED ON"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_LED_OFF) == 0)
        {
            digitalWrite(PIN_FILAMENT_LED, LOW);
            Serial.println(F("[CMD] Filament LED OFF"));
            publish_command_acknowledgement(device_id, command);
        }
    }
    // Main Actuator commands
    else if (strcmp(device_id, naming::DEV_MAIN_ACTUATOR) == 0)
    {
        if (strcmp(command, naming::CMD_ACTUATOR_UP) == 0)
        {
            digitalWrite(PIN_ACTUATORS_UP, HIGH);
            digitalWrite(PIN_ACTUATORS_DN, LOW);
            Serial.println(F("[CMD] Main Actuator UP"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_ACTUATOR_DOWN) == 0)
        {
            digitalWrite(PIN_ACTUATORS_UP, LOW);
            digitalWrite(PIN_ACTUATORS_DN, HIGH);
            Serial.println(F("[CMD] Main Actuator DOWN"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_ACTUATOR_STOP) == 0)
        {
            digitalWrite(PIN_ACTUATORS_UP, LOW);
            digitalWrite(PIN_ACTUATORS_DN, LOW);
            Serial.println(F("[CMD] Main Actuator STOP"));
            publish_command_acknowledgement(device_id, command);
        }
    }
    // Forge Actuator commands
    else if (strcmp(device_id, naming::DEV_FORGE_ACTUATOR) == 0)
    {
        if (strcmp(command, naming::CMD_FORGE_EXTEND) == 0)
        {
            digitalWrite(PIN_FORGE_ACTUATOR_EXTEND, HIGH);
            digitalWrite(PIN_FORGE_ACTUATOR_RETRACT, LOW);
            Serial.println(F("[CMD] Forge Actuator EXTEND"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_FORGE_RETRACT) == 0)
        {
            digitalWrite(PIN_FORGE_ACTUATOR_EXTEND, LOW);
            digitalWrite(PIN_FORGE_ACTUATOR_RETRACT, HIGH);
            Serial.println(F("[CMD] Forge Actuator RETRACT"));
            publish_command_acknowledgement(device_id, command);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND ACKNOWLEDGEMENT
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

    Serial.print(F("[ACK] -> "));
    Serial.println(ackTopic);
}

// ══════════════════════════════════════════════════════════════════════════════
// ENCODER MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void monitor_encoders()
{
    static unsigned long last_publish = 0;
    const unsigned long PUBLISH_INTERVAL = 100; // 100ms between checks

    if (millis() - last_publish < PUBLISH_INTERVAL)
    {
        return;
    }

    if (!sentient.isConnected())
    {
        return;
    }

    // Check each encoder for changes
    const char *encoder_names[] = {
        naming::DEV_ENCODER_LT,
        naming::DEV_ENCODER_LM,
        naming::DEV_ENCODER_LB,
        naming::DEV_ENCODER_RT,
        naming::DEV_ENCODER_RM,
        naming::DEV_ENCODER_RB};

    for (int i = 0; i < 6; i++)
    {
        if (encoder_values[i] != last_published_values[i])
        {
            JsonDocument doc;
            doc["encoder_count"] = encoder_values[i];
            sentient.publishJson(naming::CAT_SENSORS, encoder_names[i], doc);

            Serial.print(F("[ENCODER] "));
            Serial.print(encoder_names[i]);
            Serial.print(F(": "));
            Serial.println(encoder_values[i]);

            last_published_values[i] = encoder_values[i];
        }
    }

    last_publish = millis();
}

// ══════════════════════════════════════════════════════════════════════════════
// ENCODER UPDATE (ISR)
// ══════════════════════════════════════════════════════════════════════════════

void update_encoder(int index, int pinA, int pinB)
{
    int MSB = digitalRead(pinA);
    int LSB = digitalRead(pinB);

    int encoded = (MSB << 1) | LSB;
    int sum = (last_encoded[index] << 2) | encoded;

    if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
        encoder_values[index]++;
    if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
        encoder_values[index]--;

    last_encoded[index] = encoded;
}

// ══════════════════════════════════════════════════════════════════════════════
// LED RING COLOR HELPER
// ══════════════════════════════════════════════════════════════════════════════

void set_led_ring_color(CRGB *ring, const char *color)
{
    CRGB rgb;

    if (strcmp(color, "red") == 0)
        rgb = CRGB::Red;
    else if (strcmp(color, "green") == 0)
        rgb = CRGB::Green;
    else if (strcmp(color, "blue") == 0)
        rgb = CRGB::Blue;
    else if (strcmp(color, "yellow") == 0)
        rgb = CRGB::Yellow;
    else if (strcmp(color, "purple") == 0)
        rgb = CRGB::Purple;
    else if (strcmp(color, "orange") == 0)
        rgb = CRGB::Orange;
    else if (strcmp(color, "white") == 0)
        rgb = CRGB::White;
    else if (strcmp(color, "off") == 0)
        rgb = CRGB::Black;
    else
        rgb = CRGB::White; // Default

    fill_solid(ring, NUM_LEDS, rgb);
    FastLED.show();

    Serial.print(F("[LED] Set color: "));
    Serial.println(color);
}
