// ══════════════════════════════════════════════════════════════════════════════
// Vern Controller v2.1.0
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
const int PIN_OUTPUT_ONE = 34;
const int PIN_OUTPUT_TWO = 35;
const int PIN_OUTPUT_THREE = 36;
const int PIN_OUTPUT_FOUR = 37;
const int PIN_OUTPUT_FIVE = 38;
const int PIN_OUTPUT_SIX = 39;
const int PIN_OUTPUT_SEVEN = 40;
const int PIN_OUTPUT_EIGHT = 41;
const int PIN_POWER_SWITCH = 24;

// ══════════════════════════════════════════════════════════════════════════════
// MQTT CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_username = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
const unsigned long heartbeat_interval_ms = 5000;

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

const char *output_commands[] = {naming::CMD_OUTPUT_ON, naming::CMD_OUTPUT_OFF};

SentientDeviceDef dev_output_one(naming::DEV_OUTPUT_ONE, "Output 1", "digital_output", output_commands, 2);
SentientDeviceDef dev_output_two(naming::DEV_OUTPUT_TWO, "Output 2", "digital_output", output_commands, 2);
SentientDeviceDef dev_output_three(naming::DEV_OUTPUT_THREE, "Output 3", "digital_output", output_commands, 2);
SentientDeviceDef dev_output_four(naming::DEV_OUTPUT_FOUR, "Output 4", "digital_output", output_commands, 2);
SentientDeviceDef dev_output_five(naming::DEV_OUTPUT_FIVE, "Output 5", "digital_output", output_commands, 2);
SentientDeviceDef dev_output_six(naming::DEV_OUTPUT_SIX, "Output 6", "digital_output", output_commands, 2);
SentientDeviceDef dev_output_seven(naming::DEV_OUTPUT_SEVEN, "Output 7", "digital_output", output_commands, 2);
SentientDeviceDef dev_output_eight(naming::DEV_OUTPUT_EIGHT, "Output 8", "digital_output", output_commands, 2);
SentientDeviceDef dev_power_switch(naming::DEV_POWER_SWITCH, "Power Switch", "digital_output", output_commands, 2);

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

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void publish_command_acknowledgement(const char *device_id, const char *command);

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    pinMode(PIN_OUTPUT_ONE, OUTPUT);
    pinMode(PIN_OUTPUT_TWO, OUTPUT);
    pinMode(PIN_OUTPUT_THREE, OUTPUT);
    pinMode(PIN_OUTPUT_FOUR, OUTPUT);
    pinMode(PIN_OUTPUT_FIVE, OUTPUT);
    pinMode(PIN_OUTPUT_SIX, OUTPUT);
    pinMode(PIN_OUTPUT_SEVEN, OUTPUT);
    pinMode(PIN_OUTPUT_EIGHT, OUTPUT);
    pinMode(PIN_POWER_SWITCH, OUTPUT);

    digitalWrite(PIN_OUTPUT_ONE, LOW);
    digitalWrite(PIN_OUTPUT_TWO, LOW);
    digitalWrite(PIN_OUTPUT_THREE, LOW);
    digitalWrite(PIN_OUTPUT_FOUR, LOW);
    digitalWrite(PIN_OUTPUT_FIVE, LOW);
    digitalWrite(PIN_OUTPUT_SIX, LOW);
    digitalWrite(PIN_OUTPUT_SEVEN, LOW);
    digitalWrite(PIN_OUTPUT_EIGHT, LOW);
    digitalWrite(PIN_POWER_SWITCH, LOW);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Vern] Starting..."));

    deviceRegistry.addDevice(&dev_output_one);
    deviceRegistry.addDevice(&dev_output_two);
    deviceRegistry.addDevice(&dev_output_three);
    deviceRegistry.addDevice(&dev_output_four);
    deviceRegistry.addDevice(&dev_output_five);
    deviceRegistry.addDevice(&dev_output_six);
    deviceRegistry.addDevice(&dev_output_seven);
    deviceRegistry.addDevice(&dev_output_eight);
    deviceRegistry.addDevice(&dev_power_switch);
    deviceRegistry.printSummary();

    manifest.set_controller_info(naming::CONTROLLER_ID, naming::CONTROLLER_FRIENDLY_NAME,
                                 firmware::VERSION, naming::ROOM_ID, naming::CONTROLLER_ID);
    deviceRegistry.buildManifest(manifest);

    sentient.begin();
    sentient.setCommandCallback(handle_mqtt_command);

    unsigned long connection_start = millis();
    while (!sentient.isConnected() && (millis() - connection_start < 5000))
    {
        sentient.loop();
        delay(100);
    }

    if (sentient.isConnected())
    {
        manifest.publish_registration(sentient.get_client(), naming::ROOM_ID, naming::CONTROLLER_ID);
    }

    Serial.println(F("[Vern] Ready"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    sentient.loop();
    delay(100);
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    if (!payload["device_id"].is<const char *>())
        return;

    const char *device_id = payload["device_id"];

    // Determine which pin to control
    int pin = -1;

    if (strcmp(device_id, naming::DEV_OUTPUT_ONE) == 0)
        pin = PIN_OUTPUT_ONE;
    else if (strcmp(device_id, naming::DEV_OUTPUT_TWO) == 0)
        pin = PIN_OUTPUT_TWO;
    else if (strcmp(device_id, naming::DEV_OUTPUT_THREE) == 0)
        pin = PIN_OUTPUT_THREE;
    else if (strcmp(device_id, naming::DEV_OUTPUT_FOUR) == 0)
        pin = PIN_OUTPUT_FOUR;
    else if (strcmp(device_id, naming::DEV_OUTPUT_FIVE) == 0)
        pin = PIN_OUTPUT_FIVE;
    else if (strcmp(device_id, naming::DEV_OUTPUT_SIX) == 0)
        pin = PIN_OUTPUT_SIX;
    else if (strcmp(device_id, naming::DEV_OUTPUT_SEVEN) == 0)
        pin = PIN_OUTPUT_SEVEN;
    else if (strcmp(device_id, naming::DEV_OUTPUT_EIGHT) == 0)
        pin = PIN_OUTPUT_EIGHT;
    else if (strcmp(device_id, naming::DEV_POWER_SWITCH) == 0)
        pin = PIN_POWER_SWITCH;

    if (pin == -1)
        return;

    // Execute command
    if (strcmp(command, naming::CMD_OUTPUT_ON) == 0)
    {
        digitalWrite(pin, HIGH);
        Serial.print(F("[CMD] "));
        Serial.print(device_id);
        Serial.println(F(" ON"));
        publish_command_acknowledgement(device_id, command);
    }
    else if (strcmp(command, naming::CMD_OUTPUT_OFF) == 0)
    {
        digitalWrite(pin, LOW);
        Serial.print(F("[CMD] "));
        Serial.print(device_id);
        Serial.println(F(" OFF"));
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
