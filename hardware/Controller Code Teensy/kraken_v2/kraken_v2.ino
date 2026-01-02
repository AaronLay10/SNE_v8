// Kraken v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════
// STATELESS ARCHITECTURE:
// - Rotary encoder wheel (captain wheel) with dual interrupt tracking
// - Throttle handle with 7 positions (FWD3/FWD2/FWD1/NEUTRAL/REV1/REV2/REV3)
// - Publishes combined telemetry (wheel counter + throttle position)
// - No game logic - Sentient makes all decisions
// - Change detection to avoid spamming MQTT
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include "controller_naming.h"
#include "FirmwareMetadata.h"

using namespace naming;

SentientMQTTConfig build_mqtt_config()
{
    SentientMQTTConfig cfg;
    cfg.brokerHost = "mqtt.sentientengine.ai";
    cfg.brokerIp = IPAddress(192, 168, 2, 3);
    cfg.brokerPort = 1883;
    cfg.username = "paragon_devices";
    cfg.password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
    cfg.namespaceId = CLIENT_ID;
    cfg.roomId = ROOM_ID;
    cfg.controllerId = CONTROLLER_ID;
    cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
    cfg.displayName = CONTROLLER_FRIENDLY_NAME;
    cfg.useDhcp = true;
    cfg.autoHeartbeat = true;
    cfg.heartbeatIntervalMs = 5000;
    return cfg;
}

SentientMQTT sentient(build_mqtt_config());
SentientCapabilityManifest manifest;
SentientDeviceRegistry deviceRegistry;

void build_capability_manifest()
{
    manifest.set_controller_info(
        CONTROLLER_ID,
        CONTROLLER_FRIENDLY_NAME,
        firmware::VERSION,
        ROOM_ID,
        CONTROLLER_ID);
}

// ------------------- CONSTANTS & VARIABLES -------------------

#define POWERLED 13

// Throttle Handle
#define FWD1 6
#define FWD2 7
#define FWD3 5
#define NEUTRAL 2
#define REV1 3
#define REV2 4
#define REV3 1

// Capitan Wheel
#define WHEELA 8 // White Wire
#define WHEELB 9 // Green Wire

// This variable will increase or decrease depending on the rotation of encoder
volatile long x, counterA = 0;
volatile long y, counterB = 0;

String sensorDetail;
String direction;

int readingA = 0;
int readingB = 0;
int moveValue = 0;
int throttle = 0;

// Telemetry change detection
char lastTelemetry[32] = "";
bool telemetryInitialized = false;

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000)
    {
        delay(10);
    }

    Serial.println("\n════════════════════════════════════════════════");
    Serial.println("  Kraken Controller v2.3.0");
    Serial.println("  STATELESS EXECUTOR Architecture");
    Serial.println("════════════════════════════════════════════════");
    Serial.print("  Version: ");
    Serial.println(firmware::VERSION);
    Serial.print("  Build Date: ");
    Serial.println(firmware::BUILD_DATE);
    Serial.print("  Controller ID: ");
    Serial.println(CONTROLLER_ID);
    Serial.println("════════════════════════════════════════════════\n");

    pinMode(WHEELB, INPUT_PULLUP);
    pinMode(WHEELA, INPUT_PULLUP);
    pinMode(FWD1, INPUT_PULLUP);
    pinMode(FWD2, INPUT_PULLUP);
    pinMode(FWD3, INPUT_PULLUP);
    pinMode(NEUTRAL, INPUT_PULLUP);
    pinMode(REV1, INPUT_PULLUP);
    pinMode(REV2, INPUT_PULLUP);
    pinMode(REV3, INPUT_PULLUP);

    pinMode(POWERLED, OUTPUT);

    attachInterrupt(WHEELB, CounterA, RISING); // Green Wire
    attachInterrupt(WHEELA, CounterB, RISING); // White Wire

    digitalWrite(POWERLED, HIGH);

    // Build capability manifest
    Serial.println("[Kraken] Building capability manifest...");
    build_capability_manifest();
    Serial.println("[Kraken] Manifest built successfully");

    // Initialize Sentient MQTT
    Serial.println("[Kraken] Initializing MQTT...");
    if (!sentient.begin())
    {
        Serial.println("[Kraken] MQTT initialization failed - continuing without network");
    }
    else
    {
        Serial.println("[Kraken] MQTT initialization successful");
        sentient.setCommandCallback(handleCommand);
    }

    Serial.println("✓ Kraken Controller Ready!");
    Serial.println("  Waiting for commands from Sentient...\n");
}

void loop()
{
    // Handle MQTT loop and registration
    static bool registered = false;
    sentient.loop();

    // Register after MQTT connection is established
    if (!registered && sentient.isConnected())
    {
        Serial.println("[Kraken] Registering with Sentient system...");
        if (manifest.publish_registration(sentient.get_client(), ROOM_ID, CONTROLLER_ID))
        {
            Serial.println("[Kraken] Registration successful!");
            registered = true;
        }
        else
        {
            Serial.println("[Kraken] Registration failed - will retry on next loop");
        }
    }

    Serial.print(counterA);
    Serial.print(":");
    Serial.print(digitalRead(FWD1));
    Serial.print(":");
    Serial.print(digitalRead(FWD2));
    Serial.print(":");
    Serial.print(digitalRead(FWD3));
    Serial.print(":");
    Serial.print(digitalRead(NEUTRAL));
    Serial.print(":");
    Serial.print(digitalRead(REV1));
    Serial.print(":");
    Serial.print(digitalRead(REV2));
    Serial.print(":");
    Serial.println(digitalRead(REV3));

    if (digitalRead(NEUTRAL) == LOW)
    {
        throttle = 0;
    }
    else if (digitalRead(FWD1) == LOW)
    {
        throttle = 1;
    }
    else if (digitalRead(FWD2) == LOW)
    {
        throttle = 2;
    }
    else if (digitalRead(FWD3) == LOW)
    {
        throttle = 3;
    }
    else if (digitalRead(REV1) == LOW)
    {
        throttle = -1;
    }
    else if (digitalRead(REV2) == LOW)
    {
        throttle = -2;
    }
    else if (digitalRead(REV3) == LOW)
    {
        throttle = -3;
    }

    // Send telemetry with current sensor readings
    char telemetryData[32];
    sprintf(telemetryData, "%ld:%d", counterA, throttle);

    // CHANGE DETECTION: Only publish if data has changed and MQTT is connected
    bool dataChanged = (!telemetryInitialized) || (strcmp(telemetryData, lastTelemetry) != 0);

    if (dataChanged && sentient.isConnected())
    {
        Serial.print("[TELEMETRY] ");
        Serial.println(telemetryData);
        sentient.publishText("sensors", "kraken_telemetry", telemetryData);
        strcpy(lastTelemetry, telemetryData);
        telemetryInitialized = true;
    }

    // sentient.loop() maintains MQTT connection
    sentient.loop();
}

void CounterA()
{
    if (digitalRead(WHEELA) == LOW)
    {
        counterA++;
    }
    else
    {
        counterA--;
    }
}

void CounterB()
{
    if (digitalRead(WHEELB) == LOW)
    {
        counterA--;
    }
    else
    {
        counterA++;
    }
}

// ------------------- COMMAND HANDLERS -------------------
void handleCommand(const char *command, const JsonDocument &payload, void *context)
{
    if (strcmp(command, "reset_counter") == 0)
    {
        Serial.println("[COMMAND] Reset counter command received");
        counterA = 0;
        counterB = 0;
        telemetryInitialized = false; // Force telemetry update
        Serial.println("  Wheel counter reset to 0");
        publish_command_acknowledgement(CONTROLLER_ID, command);
    }
}

// ------------------- COMMAND ACKNOWLEDGEMENT -------------------
void publish_command_acknowledgement(const char *device_id, const char *command)
{
    if (!sentient.isConnected())
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

    sentient.get_client().publish(ackTopic.c_str(), buf, false);

    Serial.print(F("[ACK] -> "));
    Serial.println(ackTopic);
}
