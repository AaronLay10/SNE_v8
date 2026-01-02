// ══════════════════════════════════════════════════════════════════════════════
// Gear Puzzle Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include "controller_naming.h"

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 1: HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

// Pin Definitions
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
// SECTION 2: DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!)
// ══════════════════════════════════════════════════════════════════════════════

// Encoder A sensors
const char *encoder_a_sensors[] = {
    naming::SENSOR_ENCODER_A_COUNT};

// Encoder B sensors
const char *encoder_b_sensors[] = {
    naming::SENSOR_ENCODER_B_COUNT};

// Controller commands
const char *controller_commands[] = {
    naming::CMD_LAB,
    naming::CMD_STUDY,
    naming::CMD_BOILER,
    naming::CMD_RESET};

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

SentientDeviceDef dev_controller(
    naming::DEV_CONTROLLER,
    naming::FRIENDLY_CONTROLLER,
    "controller",
    controller_commands, 4);

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
void read_encoders();
void counter_a_isr();
void counter_b_isr();

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 4: SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Gear] Starting..."));

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
    deviceRegistry.addDevice(&dev_controller);
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

    Serial.println(F("[Gear] Ready - awaiting Sentient commands"));
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 5: MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    // 1. LISTEN for commands from Sentient
    sentient.loop();

    // 2. DETECT encoder changes and publish if needed
    read_encoders();
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 6: COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    Serial.print(F("[CMD] Received: "));
    Serial.println(command);

    // Parse device_id from payload
    const char *device_id = payload["device_id"];
    if (!device_id)
    {
        Serial.println(F("[CMD] ERROR: No device_id in payload"));
        return;
    }

    // Route to controller commands
    if (strcmp(device_id, naming::DEV_CONTROLLER) == 0)
    {
        if (strcmp(command, naming::CMD_LAB) == 0)
        {
            int value = payload["value"] | 0;
            Serial.print(F("[LAB] Command received, value: "));
            Serial.println(value);
            // TODO: Implement lab action
        }
        else if (strcmp(command, naming::CMD_STUDY) == 0)
        {
            int value = payload["value"] | 0;
            Serial.print(F("[STUDY] Command received, value: "));
            Serial.println(value);
            // TODO: Implement study lights action
        }
        else if (strcmp(command, naming::CMD_BOILER) == 0)
        {
            int value = payload["value"] | 0;
            Serial.print(F("[BOILER] Command received, value: "));
            Serial.println(value);
            // TODO: Implement boiler lights action
        }
        else if (strcmp(command, naming::CMD_RESET) == 0)
        {
            counter_a = 0;
            counter_b = 0;
            last_counter_a = 0;
            last_counter_b = 0;
            Serial.println(F("[RESET] Encoder counters reset"));
        }
        else
        {
            return; // Unknown command, no ACK
        }

        // Publish acknowledgement for successful command
        publish_command_acknowledgement(device_id, command);
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
// SECTION 7: ENCODER MONITORING
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
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_ENCODER_A_COUNT, doc);
        doc.clear();

        doc["count"] = current_b;
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_ENCODER_B_COUNT, doc);
        doc.clear();

        // Publish combined counters
        doc["encoder_a"] = current_a;
        doc["encoder_b"] = current_b;
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_COUNTERS, doc);
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
// SECTION 8: ENCODER INTERRUPT HANDLERS
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
