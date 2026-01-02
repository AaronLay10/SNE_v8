// ══════════════════════════════════════════════════════════════════════════════
// Crank Puzzle Controller v2.1.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include "controller_naming.h"

// ══════════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const int PIN_POWER_LED = 13;

// Encoder A pins
const int PIN_ENCODER_A_WHITE = 33;
const int PIN_ENCODER_A_GREEN = 34;

// Encoder B pins
const int PIN_ENCODER_B_WHITE = 35;
const int PIN_ENCODER_B_GREEN = 36;

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

// Encoder counters (volatile for interrupt safety)
volatile long counter_a = 0;
volatile long counter_b = 0;

// Previous counter values for change detection
long last_counter_a = 0;
long last_counter_b = 0;

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!)
// ══════════════════════════════════════════════════════════════════════════════

// Encoder A sensors
const char *encoder_a_sensors[] = {
    naming::SENSOR_ENCODER_COUNT};

// Encoder B sensors
const char *encoder_b_sensors[] = {
    naming::SENSOR_ENCODER_COUNT};

// Device definitions
SentientDeviceDef dev_encoder_a(
    naming::DEV_ENCODER_A,
    naming::FRIENDLY_ENCODER_A,
    "sensor",
    encoder_a_sensors, 1, true); // input only

SentientDeviceDef dev_encoder_b(
    naming::DEV_ENCODER_B,
    naming::FRIENDLY_ENCODER_B,
    "sensor",
    encoder_b_sensors, 1, true); // input only

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
void read_encoders();

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Crank] Starting..."));

    // Setup encoder pins
    pinMode(PIN_ENCODER_A_WHITE, INPUT_PULLUP);
    pinMode(PIN_ENCODER_A_GREEN, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B_WHITE, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B_GREEN, INPUT_PULLUP);

    // Attach interrupts for encoders
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A_WHITE), counter_a_white_isr, RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A_GREEN), counter_a_green_isr, RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B_WHITE), counter_b_white_isr, RISING);
    attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B_GREEN), counter_b_green_isr, RISING);

    // Register all devices
    Serial.println(F("[INIT] Registering devices..."));
    deviceRegistry.addDevice(&dev_encoder_a);
    deviceRegistry.addDevice(&dev_encoder_b);
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

    Serial.println(F("[Crank] Ready - awaiting Sentient commands (sensor-only controller)"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    // 1. LISTEN for commands from Sentient
    sentient.loop();

    // 2. DETECT encoder changes and publish if needed
    read_encoders();
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    // This controller has no commands - sensors only
    Serial.print(F("[CMD] Received (ignored): "));
    Serial.println(command);
}

// ══════════════════════════════════════════════════════════════════════════════
// ENCODER MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void read_encoders()
{
    static unsigned long last_publish = 0;
    unsigned long now = millis();

    // Check if encoder values have changed
    bool changed = false;

    noInterrupts(); // Atomic read of volatile variables
    long current_a = counter_a;
    long current_b = counter_b;
    interrupts();

    if (current_a != last_counter_a || current_b != last_counter_b)
    {
        changed = true;
        last_counter_a = current_a;
        last_counter_b = current_b;
    }

    // Publish on change or every 5 seconds
    if ((changed || (now - last_publish >= 5000)) && sentient.isConnected())
    {
        last_publish = now;

        JsonDocument doc;

        // Publish individual encoder counts
        doc["count"] = current_a;
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_ENCODER_COUNT, doc);
        doc.clear();

        doc["count"] = current_b;
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_ENCODER_COUNT, doc);
        doc.clear();

        if (changed)
        {
            Serial.print(F("[ENCODERS] A: "));
            Serial.print(current_a);
            Serial.print(F(", B: "));
            Serial.println(current_b);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ENCODER INTERRUPT HANDLERS
// ══════════════════════════════════════════════════════════════════════════════

// Encoder A - White wire interrupt
void counter_a_white_isr()
{
    if (digitalRead(PIN_ENCODER_A_GREEN) == LOW)
    {
        counter_a++;
    }
    else
    {
        counter_a--;
    }
}

// Encoder A - Green wire interrupt
void counter_a_green_isr()
{
    if (digitalRead(PIN_ENCODER_A_WHITE) == LOW)
    {
        counter_a--;
    }
    else
    {
        counter_a++;
    }
}

// Encoder B - White wire interrupt
void counter_b_white_isr()
{
    if (digitalRead(PIN_ENCODER_B_GREEN) == LOW)
    {
        counter_b++;
    }
    else
    {
        counter_b--;
    }
}

// Encoder B - Green wire interrupt
void counter_b_green_isr()
{
    if (digitalRead(PIN_ENCODER_B_WHITE) == LOW)
    {
        counter_b--;
    }
    else
    {
        counter_b++;
    }
}
