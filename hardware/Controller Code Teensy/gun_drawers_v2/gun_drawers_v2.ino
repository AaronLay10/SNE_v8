// ══════════════════════════════════════════════════════════════════════════════
// Gun Drawers Controller v2.1.0
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

// Electromagnet drawer pins (HIGH = locked, LOW = unlocked)
const int PIN_DRAWER_ELEGANT = 2;
const int PIN_DRAWER_ALCHEMIST = 3;
const int PIN_DRAWER_BOUNTY = 4;
const int PIN_DRAWER_MECHANIC = 5;

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
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

// Drawer commands
const char *drawer_commands[] = {
    naming::CMD_RELEASE_DRAWER,
    naming::CMD_LOCK_DRAWER};

// Individual drawer devices
SentientDeviceDef dev_drawer_elegant(naming::DEV_DRAWER_ELEGANT, naming::FRIENDLY_DRAWER_ELEGANT, "electromagnet", drawer_commands, 2);
SentientDeviceDef dev_drawer_alchemist(naming::DEV_DRAWER_ALCHEMIST, naming::FRIENDLY_DRAWER_ALCHEMIST, "electromagnet", drawer_commands, 2);
SentientDeviceDef dev_drawer_bounty(naming::DEV_DRAWER_BOUNTY, naming::FRIENDLY_DRAWER_BOUNTY, "electromagnet", drawer_commands, 2);
SentientDeviceDef dev_drawer_mechanic(naming::DEV_DRAWER_MECHANIC, naming::FRIENDLY_DRAWER_MECHANIC, "electromagnet", drawer_commands, 2);

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

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[GunDrawers] Starting..."));

    // Initialize drawer electromagnets (HIGH = locked)
    pinMode(PIN_DRAWER_ELEGANT, OUTPUT);
    pinMode(PIN_DRAWER_ALCHEMIST, OUTPUT);
    pinMode(PIN_DRAWER_BOUNTY, OUTPUT);
    pinMode(PIN_DRAWER_MECHANIC, OUTPUT);

    digitalWrite(PIN_DRAWER_ELEGANT, HIGH);
    digitalWrite(PIN_DRAWER_ALCHEMIST, HIGH);
    digitalWrite(PIN_DRAWER_BOUNTY, HIGH);
    digitalWrite(PIN_DRAWER_MECHANIC, HIGH);

    Serial.println(F("[INIT] All drawers locked"));

    // Register all devices
    Serial.println(F("[INIT] Registering devices..."));
    deviceRegistry.addDevice(&dev_drawer_elegant);
    deviceRegistry.addDevice(&dev_drawer_alchemist);
    deviceRegistry.addDevice(&dev_drawer_bounty);
    deviceRegistry.addDevice(&dev_drawer_mechanic);
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

    Serial.println(F("[GunDrawers] Ready - awaiting Sentient commands"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    // LISTEN for commands from Sentient
    sentient.loop();
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    Serial.print(F("[CMD] Received: "));
    Serial.println(command);

    const char *device_id = payload["device_id"];
    if (!device_id)
    {
        // Check for legacy "release all" command
        if (strcmp(command, naming::CMD_RELEASE_ALL) == 0)
        {
            digitalWrite(PIN_DRAWER_ELEGANT, LOW);
            digitalWrite(PIN_DRAWER_ALCHEMIST, LOW);
            digitalWrite(PIN_DRAWER_BOUNTY, LOW);
            digitalWrite(PIN_DRAWER_MECHANIC, LOW);
            Serial.println(F("[ALL_DRAWERS] Released (unlocked)"));
            return;
        }

        Serial.println(F("[CMD] ERROR: No device_id in payload"));
        return;
    }

    // Route commands to specific drawers
    if (strcmp(device_id, naming::DEV_DRAWER_ELEGANT) == 0)
    {
        if (strcmp(command, naming::CMD_RELEASE_DRAWER) == 0)
        {
            digitalWrite(PIN_DRAWER_ELEGANT, LOW);
            Serial.println(F("[ELEGANT] Released (unlocked)"));
        }
        else if (strcmp(command, naming::CMD_LOCK_DRAWER) == 0)
        {
            digitalWrite(PIN_DRAWER_ELEGANT, HIGH);
            Serial.println(F("[ELEGANT] Locked"));
        }
    }
    else if (strcmp(device_id, naming::DEV_DRAWER_ALCHEMIST) == 0)
    {
        if (strcmp(command, naming::CMD_RELEASE_DRAWER) == 0)
        {
            digitalWrite(PIN_DRAWER_ALCHEMIST, LOW);
            Serial.println(F("[ALCHEMIST] Released (unlocked)"));
        }
        else if (strcmp(command, naming::CMD_LOCK_DRAWER) == 0)
        {
            digitalWrite(PIN_DRAWER_ALCHEMIST, HIGH);
            Serial.println(F("[ALCHEMIST] Locked"));
        }
    }
    else if (strcmp(device_id, naming::DEV_DRAWER_BOUNTY) == 0)
    {
        if (strcmp(command, naming::CMD_RELEASE_DRAWER) == 0)
        {
            digitalWrite(PIN_DRAWER_BOUNTY, LOW);
            Serial.println(F("[BOUNTY] Released (unlocked)"));
        }
        else if (strcmp(command, naming::CMD_LOCK_DRAWER) == 0)
        {
            digitalWrite(PIN_DRAWER_BOUNTY, HIGH);
            Serial.println(F("[BOUNTY] Locked"));
        }
    }
    else if (strcmp(device_id, naming::DEV_DRAWER_MECHANIC) == 0)
    {
        if (strcmp(command, naming::CMD_RELEASE_DRAWER) == 0)
        {
            digitalWrite(PIN_DRAWER_MECHANIC, LOW);
            Serial.println(F("[MECHANIC] Released (unlocked)"));
        }
        else if (strcmp(command, naming::CMD_LOCK_DRAWER) == 0)
        {
            digitalWrite(PIN_DRAWER_MECHANIC, HIGH);
            Serial.println(F("[MECHANIC] Locked"));
        }
    }
    else
    {
        Serial.print(F("[CMD] Unknown device: "));
        Serial.println(device_id);
        return;
    }

    // Publish acknowledgement for successful command
    publish_command_acknowledgement(device_id, command);
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
