// ══════════════════════════════════════════════════════════════════════════════
// Vault Puzzle Controller v2.1.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <SerialRFID.h>
#include "controller_naming.h"

// ══════════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const int PIN_POWER_LED = 13;
const int PIN_RX = 15;
const int PIN_TX = 14;
const int PIN_TIR = 19; // Tag In Range sensor

// Tag size for RFID
#define SIZE_TAG_ID 13

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
// VAULT TAG LOOKUP TABLE
// ══════════════════════════════════════════════════════════════════════════════

// 36 vault tags mapped to vault numbers 1-36
const char tagList[][SIZE_TAG_ID] = {
    "0C007DAE1DC2", // Vault 1
    "3C0088C9CFB2", // Vault 2
    "3C008923A630", // Vault 3
    "0C007E25A9FE", // Vault 4
    "0A005A9CF438", // Vault 5
    "3C00D64911B2", // Vault 6
    "3C00D58E96F1", // Vault 7
    "3C00D633459C", // Vault 8
    "3C00D5EDF6F2", // Vault 9
    "3C00D5C16C44", // Vault 10
    "3C00D63C61B7", // Vault 11
    "3C00D5B2F4AF", // Vault 12
    "3C00892935A9", // Vault 13
    "3C00891AB11E", // Vault 14
    "3C0088EA237D", // Vault 15
    "0C007DCE47F8", // Vault 16
    "3C0088A0DFCB", // Vault 17
    "3C00D5E96666", // Vault 18
    "3C008900EB5E", // Vault 19
    "3C00D6359C43", // Vault 20
    "0C007D1B7D17", // Vault 21
    "0C007E107E1C", // Vault 22
    "3C0088804470", // Vault 23
    "3C0088E695C7", // Vault 24
    "3C00D5E3FEF4", // Vault 25
    "0C007DF174F4", // Vault 26
    "0C007DF2FF7C", // Vault 27
    "0C007DEAE873", // Vault 28
    "3C0089199539", // Vault 29
    "0C007DC9BE06", // Vault 30
    "3C0088BFDAD1", // Vault 31
    "3C00D5CCEBCE", // Vault 32
    "0C007D5CDFF2", // Vault 33
    "3C0089198F23", // Vault 34
    "3C00892541D1", // Vault 35
    "0C007DD9832B"  // Vault 36
};

// ══════════════════════════════════════════════════════════════════════════════
// STATE MANAGEMENT
// ══════════════════════════════════════════════════════════════════════════════

SoftwareSerial sSerial(PIN_RX, PIN_TX);
SerialRFID rfid(sSerial);

char current_tag[SIZE_TAG_ID];
char last_tag[SIZE_TAG_ID] = "";
int current_vault_number = 0;
int last_vault_number = 0;

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

// RFID reader sensors
const char *rfid_sensors[] = {
    naming::SENSOR_VAULT_NUMBER,
    naming::SENSOR_TAG_ID};

SentientDeviceDef dev_rfid_reader(
    naming::DEV_RFID_READER,
    naming::FRIENDLY_RFID_READER,
    "rfid_reader",
    rfid_sensors, 2, true); // Input only

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
void monitor_rfid_reader();
int lookup_vault_number(const char *tag);

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    pinMode(PIN_TIR, INPUT);

    Serial.begin(115200);
    sSerial.begin(9600);

    delay(2000);
    Serial.println(F("[Vault] Starting..."));

    // Register all devices
    Serial.println(F("[INIT] Registering devices..."));
    deviceRegistry.addDevice(&dev_rfid_reader);
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

    Serial.println(F("[Vault] Ready - awaiting Sentient commands"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    // 1. LISTEN for commands from Sentient
    sentient.loop();

    // 2. MONITOR RFID reader and publish changes
    monitor_rfid_reader();
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    // This controller has no commands - sensor only
    Serial.print(F("[CMD] Received (ignored - sensor only): "));
    Serial.println(command);
}

// ══════════════════════════════════════════════════════════════════════════════
// RFID MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void monitor_rfid_reader()
{
    // Check if tag is in range
    bool tag_in_range = (digitalRead(PIN_TIR) == HIGH);

    // Try to read tag
    if (rfid.readTag(current_tag, sizeof(current_tag)))
    {
        // Look up vault number
        current_vault_number = lookup_vault_number(current_tag);

        // Check if vault number changed
        if (current_vault_number != last_vault_number && sentient.isConnected())
        {
            JsonDocument doc;

            // Publish vault number
            doc["vault_number"] = current_vault_number;
            doc["tag_id"] = current_tag;
            doc["tag_in_range"] = tag_in_range;
            sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_VAULT_NUMBER, doc);

            Serial.print(F("[RFID] Vault: "));
            Serial.print(current_vault_number);
            Serial.print(F(" Tag: "));
            Serial.print(current_tag);
            Serial.print(F(" TIR: "));
            Serial.println(tag_in_range ? "YES" : "NO");

            last_vault_number = current_vault_number;
            strncpy(last_tag, current_tag, SIZE_TAG_ID);
        }
    }

    // Clear vault number if tag removed
    if (!tag_in_range && current_vault_number != 0)
    {
        current_vault_number = 0;
        if (last_vault_number != 0 && sentient.isConnected())
        {
            JsonDocument doc;
            doc["vault_number"] = 0;
            doc["tag_id"] = "EMPTY";
            doc["tag_in_range"] = false;
            sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_VAULT_NUMBER, doc);

            Serial.println(F("[RFID] Tag removed"));
            last_vault_number = 0;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// VAULT TAG LOOKUP
// ══════════════════════════════════════════════════════════════════════════════

int lookup_vault_number(const char *tag)
{
    for (int i = 0; i < 36; i++)
    {
        if (strcmp(tag, tagList[i]) == 0)
        {
            return i + 1; // Vault numbers are 1-indexed
        }
    }
    return 0; // Unknown tag
}
