// ══════════════════════════════════════════════════════════════════════════════
// Lab Cage A Controller v2.1.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <AccelStepper.h>
#include "controller_naming.h"

// ══════════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const int PIN_POWER_LED = 13;

// Door 1 sensors
const int PIN_D1_OPEN_A = 9;
const int PIN_D1_OPEN_B = 10;
const int PIN_D1_CLOSED_A = 11;
const int PIN_D1_CLOSED_B = 12;
const int PIN_D1_ENABLE = 35;

// Door 2 sensors
const int PIN_D2_OPEN_A = 0;
const int PIN_D2_OPEN_B = 1;
const int PIN_D2_CLOSED_A = 2;
const int PIN_D2_CLOSED_B = 3;
const int PIN_D2_ENABLE = 36;

// Canister charging
const int PIN_CANISTER_CHARGING = 41;

// Stepper motor configuration
const float STEPPER_SPEED = 400.0;
const float STEPPER_ACCEL = 200.0;

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
// STATE MANAGEMENT
// ══════════════════════════════════════════════════════════════════════════════

// Door 1 steppers (pins: Pul+, Pul-, Dir+, Dir-)
AccelStepper d1_stepper_one(AccelStepper::FULL4WIRE, 24, 25, 26, 27);
AccelStepper d1_stepper_two(AccelStepper::FULL4WIRE, 28, 29, 30, 31);

// Door 2 stepper
AccelStepper d2_stepper_one(AccelStepper::FULL4WIRE, 4, 5, 6, 7);

enum DoorDirection
{
    STOPPED = 0,
    OPENING = 1,
    CLOSING = 2
};

DoorDirection d1_direction = STOPPED;
DoorDirection d2_direction = STOPPED;

// Sensor state tracking
int d1_open_a_last = -1;
int d1_open_b_last = -1;
int d1_closed_a_last = -1;
int d1_closed_b_last = -1;
int d2_open_a_last = -1;
int d2_open_b_last = -1;
int d2_closed_a_last = -1;
int d2_closed_b_last = -1;

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

const char *door_commands[] = {naming::CMD_DOOR_OPEN, naming::CMD_DOOR_CLOSE, naming::CMD_DOOR_STOP};
const char *door_sensors[] = {naming::SENSOR_OPEN_A, naming::SENSOR_OPEN_B, naming::SENSOR_CLOSED_A, naming::SENSOR_CLOSED_B};
const char *charging_commands[] = {naming::CMD_CHARGING_ON, naming::CMD_CHARGING_OFF};

SentientDeviceDef dev_door_one(naming::DEV_DOOR_ONE, "Door One", "stepper_door", door_commands, 3, door_sensors, 4);
SentientDeviceDef dev_door_two(naming::DEV_DOOR_TWO, "Door Two", "stepper_door", door_commands, 3, door_sensors, 4);
SentientDeviceDef dev_canister_charging(naming::DEV_CANISTER_CHARGING, "Canister Charging", "digital_output", charging_commands, 2);

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
// DOOR CONTROL FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

void set_door_direction(int door_num, DoorDirection dir)
{
    if (door_num == 1)
    {
        d1_direction = dir;
        if (dir == STOPPED)
        {
            d1_stepper_one.stop();
            d1_stepper_two.stop();
            digitalWrite(PIN_D1_ENABLE, LOW);
        }
        else
        {
            digitalWrite(PIN_D1_ENABLE, HIGH);
        }
    }
    else if (door_num == 2)
    {
        d2_direction = dir;
        if (dir == STOPPED)
        {
            d2_stepper_one.stop();
            digitalWrite(PIN_D2_ENABLE, LOW);
        }
        else
        {
            digitalWrite(PIN_D2_ENABLE, HIGH);
        }
    }
}

void update_door_motors()
{
    // Door 1
    if (d1_direction == OPENING)
    {
        d1_stepper_one.setSpeed(STEPPER_SPEED);
        d1_stepper_two.setSpeed(STEPPER_SPEED);
        d1_stepper_one.runSpeed();
        d1_stepper_two.runSpeed();
    }
    else if (d1_direction == CLOSING)
    {
        d1_stepper_one.setSpeed(-STEPPER_SPEED);
        d1_stepper_two.setSpeed(-STEPPER_SPEED);
        d1_stepper_one.runSpeed();
        d1_stepper_two.runSpeed();
    }

    // Door 2
    if (d2_direction == OPENING)
    {
        d2_stepper_one.setSpeed(STEPPER_SPEED);
        d2_stepper_one.runSpeed();
    }
    else if (d2_direction == CLOSING)
    {
        d2_stepper_one.setSpeed(-STEPPER_SPEED);
        d2_stepper_one.runSpeed();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SENSOR MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void monitor_sensors()
{
    // Door 1 sensors
    int d1_open_a = digitalRead(PIN_D1_OPEN_A);
    int d1_open_b = digitalRead(PIN_D1_OPEN_B);
    int d1_closed_a = digitalRead(PIN_D1_CLOSED_A);
    int d1_closed_b = digitalRead(PIN_D1_CLOSED_B);

    if (d1_open_a != d1_open_a_last)
    {
        StaticJsonDocument<128> doc;
        doc[naming::SENSOR_OPEN_A] = (d1_open_a == HIGH);
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_DOOR_ONE, doc);
        d1_open_a_last = d1_open_a;
    }
    if (d1_open_b != d1_open_b_last)
    {
        StaticJsonDocument<128> doc;
        doc[naming::SENSOR_OPEN_B] = (d1_open_b == HIGH);
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_DOOR_ONE, doc);
        d1_open_b_last = d1_open_b;
    }
    if (d1_closed_a != d1_closed_a_last)
    {
        StaticJsonDocument<128> doc;
        doc[naming::SENSOR_CLOSED_A] = (d1_closed_a == HIGH);
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_DOOR_ONE, doc);
        d1_closed_a_last = d1_closed_a;
    }
    if (d1_closed_b != d1_closed_b_last)
    {
        StaticJsonDocument<128> doc;
        doc[naming::SENSOR_CLOSED_B] = (d1_closed_b == HIGH);
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_DOOR_ONE, doc);
        d1_closed_b_last = d1_closed_b;
    }

    // Door 2 sensors
    int d2_open_a = digitalRead(PIN_D2_OPEN_A);
    int d2_open_b = digitalRead(PIN_D2_OPEN_B);
    int d2_closed_a = digitalRead(PIN_D2_CLOSED_A);
    int d2_closed_b = digitalRead(PIN_D2_CLOSED_B);

    if (d2_open_a != d2_open_a_last)
    {
        StaticJsonDocument<128> doc;
        doc[naming::SENSOR_OPEN_A] = (d2_open_a == HIGH);
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_DOOR_TWO, doc);
        d2_open_a_last = d2_open_a;
    }
    if (d2_open_b != d2_open_b_last)
    {
        StaticJsonDocument<128> doc;
        doc[naming::SENSOR_OPEN_B] = (d2_open_b == HIGH);
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_DOOR_TWO, doc);
        d2_open_b_last = d2_open_b;
    }
    if (d2_closed_a != d2_closed_a_last)
    {
        StaticJsonDocument<128> doc;
        doc[naming::SENSOR_CLOSED_A] = (d2_closed_a == HIGH);
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_DOOR_TWO, doc);
        d2_closed_a_last = d2_closed_a;
    }
    if (d2_closed_b != d2_closed_b_last)
    {
        StaticJsonDocument<128> doc;
        doc[naming::SENSOR_CLOSED_B] = (d2_closed_b == HIGH);
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_DOOR_TWO, doc);
        d2_closed_b_last = d2_closed_b;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    // Initialize door sensors
    pinMode(PIN_D1_OPEN_A, INPUT_PULLDOWN);
    pinMode(PIN_D1_OPEN_B, INPUT_PULLDOWN);
    pinMode(PIN_D1_CLOSED_A, INPUT_PULLDOWN);
    pinMode(PIN_D1_CLOSED_B, INPUT_PULLDOWN);
    pinMode(PIN_D2_OPEN_A, INPUT_PULLDOWN);
    pinMode(PIN_D2_OPEN_B, INPUT_PULLDOWN);
    pinMode(PIN_D2_CLOSED_A, INPUT_PULLDOWN);
    pinMode(PIN_D2_CLOSED_B, INPUT_PULLDOWN);

    // Initialize outputs
    pinMode(PIN_D1_ENABLE, OUTPUT);
    pinMode(PIN_D2_ENABLE, OUTPUT);
    pinMode(PIN_CANISTER_CHARGING, OUTPUT);
    digitalWrite(PIN_D1_ENABLE, LOW);
    digitalWrite(PIN_D2_ENABLE, LOW);
    digitalWrite(PIN_CANISTER_CHARGING, LOW);

    // Configure steppers
    d1_stepper_one.setMaxSpeed(STEPPER_SPEED);
    d1_stepper_one.setAcceleration(STEPPER_ACCEL);
    d1_stepper_two.setMaxSpeed(STEPPER_SPEED);
    d1_stepper_two.setAcceleration(STEPPER_ACCEL);
    d2_stepper_one.setMaxSpeed(STEPPER_SPEED);
    d2_stepper_one.setAcceleration(STEPPER_ACCEL);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Lab Cage A] Starting..."));

    deviceRegistry.addDevice(&dev_door_one);
    deviceRegistry.addDevice(&dev_door_two);
    deviceRegistry.addDevice(&dev_canister_charging);
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

    Serial.println(F("[Lab Cage A] Ready"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    sentient.loop();
    update_door_motors();
    monitor_sensors();
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    if (!payload["device_id"].is<const char *>())
        return;

    const char *device_id = payload["device_id"];

    // Door One
    if (strcmp(device_id, naming::DEV_DOOR_ONE) == 0)
    {
        if (strcmp(command, naming::CMD_DOOR_OPEN) == 0)
        {
            set_door_direction(1, OPENING);
            Serial.println(F("[CMD] Door 1: Open"));
        }
        else if (strcmp(command, naming::CMD_DOOR_CLOSE) == 0)
        {
            set_door_direction(1, CLOSING);
            Serial.println(F("[CMD] Door 1: Close"));
        }
        else if (strcmp(command, naming::CMD_DOOR_STOP) == 0)
        {
            set_door_direction(1, STOPPED);
            Serial.println(F("[CMD] Door 1: Stop"));
        }
        else
        {
            return;
        }
        publish_command_acknowledgement(device_id, command);
        return;
    }

    // Door Two
    if (strcmp(device_id, naming::DEV_DOOR_TWO) == 0)
    {
        if (strcmp(command, naming::CMD_DOOR_OPEN) == 0)
        {
            set_door_direction(2, OPENING);
            Serial.println(F("[CMD] Door 2: Open"));
        }
        else if (strcmp(command, naming::CMD_DOOR_CLOSE) == 0)
        {
            set_door_direction(2, CLOSING);
            Serial.println(F("[CMD] Door 2: Close"));
        }
        else if (strcmp(command, naming::CMD_DOOR_STOP) == 0)
        {
            set_door_direction(2, STOPPED);
            Serial.println(F("[CMD] Door 2: Stop"));
        }
        else
        {
            return;
        }
        publish_command_acknowledgement(device_id, command);
        return;
    }

    // Canister Charging
    if (strcmp(device_id, naming::DEV_CANISTER_CHARGING) == 0)
    {
        if (strcmp(command, naming::CMD_CHARGING_ON) == 0)
        {
            digitalWrite(PIN_CANISTER_CHARGING, HIGH);
            Serial.println(F("[CMD] Canister Charging: ON"));
        }
        else if (strcmp(command, naming::CMD_CHARGING_OFF) == 0)
        {
            digitalWrite(PIN_CANISTER_CHARGING, LOW);
            Serial.println(F("[CMD] Canister Charging: OFF"));
        }
        else
        {
            return;
        }
        publish_command_acknowledgement(device_id, command);
        return;
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
