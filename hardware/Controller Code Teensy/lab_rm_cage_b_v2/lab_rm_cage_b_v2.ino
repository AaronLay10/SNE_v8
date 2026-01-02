// ══════════════════════════════════════════════════════════════════════════════
// Lab Cage B Controller v2.1.0
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

// Door 3 sensors
const int PIN_D3_OPEN_A = 8;
const int PIN_D3_OPEN_B = 9;
const int PIN_D3_CLOSED_A = 10;
const int PIN_D3_CLOSED_B = 11;
const int PIN_D3_ENABLE = 35;

// Door 4 sensors
const int PIN_D4_OPEN_A = 0;
const int PIN_D4_OPEN_B = 1;
const int PIN_D4_CLOSED_A = 2;
const int PIN_D4_CLOSED_B = 3;
const int PIN_D4_ENABLE = 36;

// Door 5 sensors
const int PIN_D5_OPEN_A = 20;
const int PIN_D5_OPEN_B = 21;
const int PIN_D5_CLOSED_A = 22;
const int PIN_D5_CLOSED_B = 23;
const int PIN_D5_ENABLE = 36; // Shared with D4

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

// Door 3 steppers (2 motors)
AccelStepper d3_stepper_one(AccelStepper::FULL4WIRE, 24, 25, 26, 27);
AccelStepper d3_stepper_two(AccelStepper::FULL4WIRE, 28, 29, 30, 31);

// Door 4 stepper (1 motor)
AccelStepper d4_stepper_one(AccelStepper::FULL4WIRE, 4, 5, 6, 7);

// Door 5 steppers (2 motors)
AccelStepper d5_stepper_one(AccelStepper::FULL4WIRE, 16, 17, 18, 19);
AccelStepper d5_stepper_two(AccelStepper::FULL4WIRE, 38, 39, 40, 41);

enum DoorDirection
{
    STOPPED = 0,
    OPENING = 1,
    CLOSING = 2
};

DoorDirection d3_direction = STOPPED;
DoorDirection d4_direction = STOPPED;
DoorDirection d5_direction = STOPPED;

// Sensor state tracking
int d3_open_a_last = -1, d3_open_b_last = -1, d3_closed_a_last = -1, d3_closed_b_last = -1;
int d4_open_a_last = -1, d4_open_b_last = -1, d4_closed_a_last = -1, d4_closed_b_last = -1;
int d5_open_a_last = -1, d5_open_b_last = -1, d5_closed_a_last = -1, d5_closed_b_last = -1;

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

const char *door_commands[] = {naming::CMD_DOOR_OPEN, naming::CMD_DOOR_CLOSE, naming::CMD_DOOR_STOP};
const char *door_sensors[] = {naming::SENSOR_OPEN_A, naming::SENSOR_OPEN_B, naming::SENSOR_CLOSED_A, naming::SENSOR_CLOSED_B};

SentientDeviceDef dev_door_three(naming::DEV_DOOR_THREE, "Door Three", "stepper_door", door_commands, 3, door_sensors, 4);
SentientDeviceDef dev_door_four(naming::DEV_DOOR_FOUR, "Door Four", "stepper_door", door_commands, 3, door_sensors, 4);
SentientDeviceDef dev_door_five(naming::DEV_DOOR_FIVE, "Door Five", "stepper_door", door_commands, 3, door_sensors, 4);

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
    if (door_num == 3)
    {
        d3_direction = dir;
        if (dir == STOPPED)
        {
            d3_stepper_one.stop();
            d3_stepper_two.stop();
            digitalWrite(PIN_D3_ENABLE, LOW);
        }
        else
        {
            digitalWrite(PIN_D3_ENABLE, HIGH);
        }
    }
    else if (door_num == 4)
    {
        d4_direction = dir;
        if (dir == STOPPED)
        {
            d4_stepper_one.stop();
            digitalWrite(PIN_D4_ENABLE, LOW);
        }
        else
        {
            digitalWrite(PIN_D4_ENABLE, HIGH);
        }
    }
    else if (door_num == 5)
    {
        d5_direction = dir;
        if (dir == STOPPED)
        {
            d5_stepper_one.stop();
            d5_stepper_two.stop();
            // D5 shares enable with D4 - only disable if both stopped
            if (d4_direction == STOPPED)
            {
                digitalWrite(PIN_D5_ENABLE, LOW);
            }
        }
        else
        {
            digitalWrite(PIN_D5_ENABLE, HIGH);
        }
    }
}

void update_door_motors()
{
    // Door 3 (2 motors)
    if (d3_direction == OPENING)
    {
        d3_stepper_one.setSpeed(STEPPER_SPEED);
        d3_stepper_two.setSpeed(STEPPER_SPEED);
        d3_stepper_one.runSpeed();
        d3_stepper_two.runSpeed();
    }
    else if (d3_direction == CLOSING)
    {
        d3_stepper_one.setSpeed(-STEPPER_SPEED);
        d3_stepper_two.setSpeed(-STEPPER_SPEED);
        d3_stepper_one.runSpeed();
        d3_stepper_two.runSpeed();
    }

    // Door 4 (1 motor)
    if (d4_direction == OPENING)
    {
        d4_stepper_one.setSpeed(STEPPER_SPEED);
        d4_stepper_one.runSpeed();
    }
    else if (d4_direction == CLOSING)
    {
        d4_stepper_one.setSpeed(-STEPPER_SPEED);
        d4_stepper_one.runSpeed();
    }

    // Door 5 (2 motors)
    if (d5_direction == OPENING)
    {
        d5_stepper_one.setSpeed(STEPPER_SPEED);
        d5_stepper_two.setSpeed(STEPPER_SPEED);
        d5_stepper_one.runSpeed();
        d5_stepper_two.runSpeed();
    }
    else if (d5_direction == CLOSING)
    {
        d5_stepper_one.setSpeed(-STEPPER_SPEED);
        d5_stepper_two.setSpeed(-STEPPER_SPEED);
        d5_stepper_one.runSpeed();
        d5_stepper_two.runSpeed();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SENSOR MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void publish_sensor(const char *device_id, const char *sensor_name, int value)
{
    JsonDocument doc;
    doc[sensor_name] = (value == HIGH);
    sentient.publishJson(naming::CAT_SENSORS, device_id, doc);
}

void monitor_sensors()
{
    // Door 3 sensors
    int d3_open_a = digitalRead(PIN_D3_OPEN_A);
    int d3_open_b = digitalRead(PIN_D3_OPEN_B);
    int d3_closed_a = digitalRead(PIN_D3_CLOSED_A);
    int d3_closed_b = digitalRead(PIN_D3_CLOSED_B);

    if (d3_open_a != d3_open_a_last)
    {
        publish_sensor(naming::DEV_DOOR_THREE, naming::SENSOR_OPEN_A, d3_open_a);
        d3_open_a_last = d3_open_a;
    }
    if (d3_open_b != d3_open_b_last)
    {
        publish_sensor(naming::DEV_DOOR_THREE, naming::SENSOR_OPEN_B, d3_open_b);
        d3_open_b_last = d3_open_b;
    }
    if (d3_closed_a != d3_closed_a_last)
    {
        publish_sensor(naming::DEV_DOOR_THREE, naming::SENSOR_CLOSED_A, d3_closed_a);
        d3_closed_a_last = d3_closed_a;
    }
    if (d3_closed_b != d3_closed_b_last)
    {
        publish_sensor(naming::DEV_DOOR_THREE, naming::SENSOR_CLOSED_B, d3_closed_b);
        d3_closed_b_last = d3_closed_b;
    }

    // Door 4 sensors
    int d4_open_a = digitalRead(PIN_D4_OPEN_A);
    int d4_open_b = digitalRead(PIN_D4_OPEN_B);
    int d4_closed_a = digitalRead(PIN_D4_CLOSED_A);
    int d4_closed_b = digitalRead(PIN_D4_CLOSED_B);

    if (d4_open_a != d4_open_a_last)
    {
        publish_sensor(naming::DEV_DOOR_FOUR, naming::SENSOR_OPEN_A, d4_open_a);
        d4_open_a_last = d4_open_a;
    }
    if (d4_open_b != d4_open_b_last)
    {
        publish_sensor(naming::DEV_DOOR_FOUR, naming::SENSOR_OPEN_B, d4_open_b);
        d4_open_b_last = d4_open_b;
    }
    if (d4_closed_a != d4_closed_a_last)
    {
        publish_sensor(naming::DEV_DOOR_FOUR, naming::SENSOR_CLOSED_A, d4_closed_a);
        d4_closed_a_last = d4_closed_a;
    }
    if (d4_closed_b != d4_closed_b_last)
    {
        publish_sensor(naming::DEV_DOOR_FOUR, naming::SENSOR_CLOSED_B, d4_closed_b);
        d4_closed_b_last = d4_closed_b;
    }

    // Door 5 sensors
    int d5_open_a = digitalRead(PIN_D5_OPEN_A);
    int d5_open_b = digitalRead(PIN_D5_OPEN_B);
    int d5_closed_a = digitalRead(PIN_D5_CLOSED_A);
    int d5_closed_b = digitalRead(PIN_D5_CLOSED_B);

    if (d5_open_a != d5_open_a_last)
    {
        publish_sensor(naming::DEV_DOOR_FIVE, naming::SENSOR_OPEN_A, d5_open_a);
        d5_open_a_last = d5_open_a;
    }
    if (d5_open_b != d5_open_b_last)
    {
        publish_sensor(naming::DEV_DOOR_FIVE, naming::SENSOR_OPEN_B, d5_open_b);
        d5_open_b_last = d5_open_b;
    }
    if (d5_closed_a != d5_closed_a_last)
    {
        publish_sensor(naming::DEV_DOOR_FIVE, naming::SENSOR_CLOSED_A, d5_closed_a);
        d5_closed_a_last = d5_closed_a;
    }
    if (d5_closed_b != d5_closed_b_last)
    {
        publish_sensor(naming::DEV_DOOR_FIVE, naming::SENSOR_CLOSED_B, d5_closed_b);
        d5_closed_b_last = d5_closed_b;
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
    pinMode(PIN_D3_OPEN_A, INPUT_PULLDOWN);
    pinMode(PIN_D3_OPEN_B, INPUT_PULLDOWN);
    pinMode(PIN_D3_CLOSED_A, INPUT_PULLDOWN);
    pinMode(PIN_D3_CLOSED_B, INPUT_PULLDOWN);
    pinMode(PIN_D4_OPEN_A, INPUT_PULLDOWN);
    pinMode(PIN_D4_OPEN_B, INPUT_PULLDOWN);
    pinMode(PIN_D4_CLOSED_A, INPUT_PULLDOWN);
    pinMode(PIN_D4_CLOSED_B, INPUT_PULLDOWN);
    pinMode(PIN_D5_OPEN_A, INPUT_PULLDOWN);
    pinMode(PIN_D5_OPEN_B, INPUT_PULLDOWN);
    pinMode(PIN_D5_CLOSED_A, INPUT_PULLDOWN);
    pinMode(PIN_D5_CLOSED_B, INPUT_PULLDOWN);

    // Initialize outputs
    pinMode(PIN_D3_ENABLE, OUTPUT);
    pinMode(PIN_D4_ENABLE, OUTPUT);
    digitalWrite(PIN_D3_ENABLE, LOW);
    digitalWrite(PIN_D4_ENABLE, LOW);
    // D5 shares enable with D4

    // Configure steppers
    d3_stepper_one.setMaxSpeed(STEPPER_SPEED);
    d3_stepper_one.setAcceleration(STEPPER_ACCEL);
    d3_stepper_two.setMaxSpeed(STEPPER_SPEED);
    d3_stepper_two.setAcceleration(STEPPER_ACCEL);
    d4_stepper_one.setMaxSpeed(STEPPER_SPEED);
    d4_stepper_one.setAcceleration(STEPPER_ACCEL);
    d5_stepper_one.setMaxSpeed(STEPPER_SPEED);
    d5_stepper_one.setAcceleration(STEPPER_ACCEL);
    d5_stepper_two.setMaxSpeed(STEPPER_SPEED);
    d5_stepper_two.setAcceleration(STEPPER_ACCEL);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Lab Cage B] Starting..."));

    deviceRegistry.addDevice(&dev_door_three);
    deviceRegistry.addDevice(&dev_door_four);
    deviceRegistry.addDevice(&dev_door_five);
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

    Serial.println(F("[Lab Cage B] Ready"));
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

    // Door Three
    if (strcmp(device_id, naming::DEV_DOOR_THREE) == 0)
    {
        if (strcmp(command, naming::CMD_DOOR_OPEN) == 0)
        {
            set_door_direction(3, OPENING);
            Serial.println(F("[CMD] Door 3: Open"));
        }
        else if (strcmp(command, naming::CMD_DOOR_CLOSE) == 0)
        {
            set_door_direction(3, CLOSING);
            Serial.println(F("[CMD] Door 3: Close"));
        }
        else if (strcmp(command, naming::CMD_DOOR_STOP) == 0)
        {
            set_door_direction(3, STOPPED);
            Serial.println(F("[CMD] Door 3: Stop"));
        }
        else
        {
            return;
        }
        publish_command_acknowledgement(device_id, command);
        return;
    }

    // Door Four
    if (strcmp(device_id, naming::DEV_DOOR_FOUR) == 0)
    {
        if (strcmp(command, naming::CMD_DOOR_OPEN) == 0)
        {
            set_door_direction(4, OPENING);
            Serial.println(F("[CMD] Door 4: Open"));
        }
        else if (strcmp(command, naming::CMD_DOOR_CLOSE) == 0)
        {
            set_door_direction(4, CLOSING);
            Serial.println(F("[CMD] Door 4: Close"));
        }
        else if (strcmp(command, naming::CMD_DOOR_STOP) == 0)
        {
            set_door_direction(4, STOPPED);
            Serial.println(F("[CMD] Door 4: Stop"));
        }
        else
        {
            return;
        }
        publish_command_acknowledgement(device_id, command);
        return;
    }

    // Door Five
    if (strcmp(device_id, naming::DEV_DOOR_FIVE) == 0)
    {
        if (strcmp(command, naming::CMD_DOOR_OPEN) == 0)
        {
            set_door_direction(5, OPENING);
            Serial.println(F("[CMD] Door 5: Open"));
        }
        else if (strcmp(command, naming::CMD_DOOR_CLOSE) == 0)
        {
            set_door_direction(5, CLOSING);
            Serial.println(F("[CMD] Door 5: Close"));
        }
        else if (strcmp(command, naming::CMD_DOOR_STOP) == 0)
        {
            set_door_direction(5, STOPPED);
            Serial.println(F("[CMD] Door 5: Stop"));
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
