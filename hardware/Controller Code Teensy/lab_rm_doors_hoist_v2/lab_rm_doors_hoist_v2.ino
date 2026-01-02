// ══════════════════════════════════════════════════════════════════════════════
// Lab Doors & Hoist Controller v2.1.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN // IRremote library compatibility
#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <AccelStepper.h>
#include <IRremote.hpp>
#include "controller_naming.h"

// ══════════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const int PIN_POWER_LED = 13;

// Hoist sensors
const int PIN_HOIST_UP_A = 14;
const int PIN_HOIST_UP_B = 15;
const int PIN_HOIST_DOWN_A = 16;
const int PIN_HOIST_DOWN_B = 17;
const int PIN_HOIST_ENABLE = 20;

// Left lab door sensors
const int PIN_LEFT_OPEN_A = 37;
const int PIN_LEFT_OPEN_B = 38;
const int PIN_LEFT_CLOSED_A = 39;
const int PIN_LEFT_CLOSED_B = 40;

// Right lab door sensors
const int PIN_RIGHT_OPEN_A = 33;
const int PIN_RIGHT_OPEN_B = 34;
const int PIN_RIGHT_CLOSED_A = 35;
const int PIN_RIGHT_CLOSED_B = 36;

const int PIN_LAB_DOORS_ENABLE = 19;

// IR receiver and rope drop
const int PIN_IR_RECEIVER = 21;
const int PIN_ROPE_DROP = 23;

// Stepper motor configuration
const float STEPPER_SPEED = 400.0;
const float STEPPER_ACCEL = 200.0;

const uint8_t EXPECTED_GUN_IR_CODE = 0x51;

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

// Hoist steppers (2 motors)
AccelStepper hoist_stepper_one(AccelStepper::FULL4WIRE, 0, 1, 2, 3);
AccelStepper hoist_stepper_two(AccelStepper::FULL4WIRE, 5, 6, 7, 8);

// Left door steppers (2 motors)
AccelStepper left_stepper_one(AccelStepper::FULL4WIRE, 24, 25, 26, 27);
AccelStepper left_stepper_two(AccelStepper::FULL4WIRE, 28, 29, 30, 31);

// Right door steppers (2 motors)
AccelStepper right_stepper_one(AccelStepper::FULL4WIRE, 4, 5, 6, 7);
AccelStepper right_stepper_two(AccelStepper::FULL4WIRE, 9, 10, 11, 12);

enum Direction
{
    STOPPED = 0,
    MOVING = 1 // For hoist: up/down, for doors: opening/closing
};

Direction hoist_direction = STOPPED;
int hoist_target = 0; // 1=up, -1=down

Direction left_direction = STOPPED;
int left_target = 0; // 1=open, -1=close

Direction right_direction = STOPPED;
int right_target = 0; // 1=open, -1=close

// Sensor state tracking
int hoist_up_a_last = -1, hoist_up_b_last = -1, hoist_down_a_last = -1, hoist_down_b_last = -1;
int left_open_a_last = -1, left_open_b_last = -1, left_closed_a_last = -1, left_closed_b_last = -1;
int right_open_a_last = -1, right_open_b_last = -1, right_closed_a_last = -1, right_closed_b_last = -1;

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

const char *hoist_commands[] = {naming::CMD_UP, naming::CMD_DOWN, naming::CMD_STOP};
const char *hoist_sensors[] = {naming::SENSOR_UP_A, naming::SENSOR_UP_B, naming::SENSOR_DOWN_A, naming::SENSOR_DOWN_B};

const char *door_commands[] = {naming::CMD_DOOR_OPEN, naming::CMD_DOOR_CLOSE, naming::CMD_DOOR_STOP};
const char *door_sensors[] = {naming::SENSOR_OPEN_A, naming::SENSOR_OPEN_B, naming::SENSOR_CLOSED_A, naming::SENSOR_CLOSED_B};

const char *rope_commands[] = {naming::CMD_DROP, naming::CMD_RESET};
const char *ir_sensors[] = {naming::SENSOR_IR_CODE};

SentientDeviceDef dev_hoist(naming::DEV_HOIST, "Hoist", "stepper_hoist", hoist_commands, 3, hoist_sensors, 4);
SentientDeviceDef dev_left_door(naming::DEV_LAB_DOOR_LEFT, "Left Lab Door", "stepper_door", door_commands, 3, door_sensors, 4);
SentientDeviceDef dev_right_door(naming::DEV_LAB_DOOR_RIGHT, "Right Lab Door", "stepper_door", door_commands, 3, door_sensors, 4);
SentientDeviceDef dev_rope_drop(naming::DEV_ROPE_DROP, "Rope Drop", "solenoid", rope_commands, 2);
SentientDeviceDef dev_ir_receiver(naming::DEV_IR_RECEIVER, "Gun IR Receiver", "ir_receiver", nullptr, 0, ir_sensors, 1);

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
// CONTROL FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

void set_hoist_direction(int target)
{
    hoist_target = target;
    if (target == 0)
    {
        hoist_direction = STOPPED;
        hoist_stepper_one.stop();
        hoist_stepper_two.stop();
        digitalWrite(PIN_HOIST_ENABLE, LOW);
    }
    else
    {
        hoist_direction = MOVING;
        digitalWrite(PIN_HOIST_ENABLE, HIGH);
    }
}

void set_door_direction(bool is_left, int target)
{
    if (is_left)
    {
        left_target = target;
        if (target == 0)
        {
            left_direction = STOPPED;
            left_stepper_one.stop();
            left_stepper_two.stop();
        }
        else
        {
            left_direction = MOVING;
        }
    }
    else
    {
        right_target = target;
        if (target == 0)
        {
            right_direction = STOPPED;
            right_stepper_one.stop();
            right_stepper_two.stop();
        }
        else
        {
            right_direction = MOVING;
        }
    }

    // Enable pin shared by both doors
    if (left_direction != STOPPED || right_direction != STOPPED)
    {
        digitalWrite(PIN_LAB_DOORS_ENABLE, HIGH);
    }
    else
    {
        digitalWrite(PIN_LAB_DOORS_ENABLE, LOW);
    }
}

void update_motors()
{
    // Hoist
    if (hoist_direction == MOVING)
    {
        float speed = (hoist_target > 0) ? STEPPER_SPEED : -STEPPER_SPEED;
        hoist_stepper_one.setSpeed(speed);
        hoist_stepper_two.setSpeed(speed);
        hoist_stepper_one.runSpeed();
        hoist_stepper_two.runSpeed();
    }

    // Left door
    if (left_direction == MOVING)
    {
        float speed = (left_target > 0) ? STEPPER_SPEED : -STEPPER_SPEED;
        left_stepper_one.setSpeed(speed);
        left_stepper_two.setSpeed(speed);
        left_stepper_one.runSpeed();
        left_stepper_two.runSpeed();
    }

    // Right door
    if (right_direction == MOVING)
    {
        float speed = (right_target > 0) ? STEPPER_SPEED : -STEPPER_SPEED;
        right_stepper_one.setSpeed(speed);
        right_stepper_two.setSpeed(speed);
        right_stepper_one.runSpeed();
        right_stepper_two.runSpeed();
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
    // Hoist sensors
    int hoist_up_a = digitalRead(PIN_HOIST_UP_A);
    int hoist_up_b = digitalRead(PIN_HOIST_UP_B);
    int hoist_down_a = digitalRead(PIN_HOIST_DOWN_A);
    int hoist_down_b = digitalRead(PIN_HOIST_DOWN_B);

    if (hoist_up_a != hoist_up_a_last)
    {
        publish_sensor(naming::DEV_HOIST, naming::SENSOR_UP_A, hoist_up_a);
        hoist_up_a_last = hoist_up_a;
    }
    if (hoist_up_b != hoist_up_b_last)
    {
        publish_sensor(naming::DEV_HOIST, naming::SENSOR_UP_B, hoist_up_b);
        hoist_up_b_last = hoist_up_b;
    }
    if (hoist_down_a != hoist_down_a_last)
    {
        publish_sensor(naming::DEV_HOIST, naming::SENSOR_DOWN_A, hoist_down_a);
        hoist_down_a_last = hoist_down_a;
    }
    if (hoist_down_b != hoist_down_b_last)
    {
        publish_sensor(naming::DEV_HOIST, naming::SENSOR_DOWN_B, hoist_down_b);
        hoist_down_b_last = hoist_down_b;
    }

    // Left door sensors
    int left_open_a = digitalRead(PIN_LEFT_OPEN_A);
    int left_open_b = digitalRead(PIN_LEFT_OPEN_B);
    int left_closed_a = digitalRead(PIN_LEFT_CLOSED_A);
    int left_closed_b = digitalRead(PIN_LEFT_CLOSED_B);

    if (left_open_a != left_open_a_last)
    {
        publish_sensor(naming::DEV_LAB_DOOR_LEFT, naming::SENSOR_OPEN_A, left_open_a);
        left_open_a_last = left_open_a;
    }
    if (left_open_b != left_open_b_last)
    {
        publish_sensor(naming::DEV_LAB_DOOR_LEFT, naming::SENSOR_OPEN_B, left_open_b);
        left_open_b_last = left_open_b;
    }
    if (left_closed_a != left_closed_a_last)
    {
        publish_sensor(naming::DEV_LAB_DOOR_LEFT, naming::SENSOR_CLOSED_A, left_closed_a);
        left_closed_a_last = left_closed_a;
    }
    if (left_closed_b != left_closed_b_last)
    {
        publish_sensor(naming::DEV_LAB_DOOR_LEFT, naming::SENSOR_CLOSED_B, left_closed_b);
        left_closed_b_last = left_closed_b;
    }

    // Right door sensors
    int right_open_a = digitalRead(PIN_RIGHT_OPEN_A);
    int right_open_b = digitalRead(PIN_RIGHT_OPEN_B);
    int right_closed_a = digitalRead(PIN_RIGHT_CLOSED_A);
    int right_closed_b = digitalRead(PIN_RIGHT_CLOSED_B);

    if (right_open_a != right_open_a_last)
    {
        publish_sensor(naming::DEV_LAB_DOOR_RIGHT, naming::SENSOR_OPEN_A, right_open_a);
        right_open_a_last = right_open_a;
    }
    if (right_open_b != right_open_b_last)
    {
        publish_sensor(naming::DEV_LAB_DOOR_RIGHT, naming::SENSOR_OPEN_B, right_open_b);
        right_open_b_last = right_open_b;
    }
    if (right_closed_a != right_closed_a_last)
    {
        publish_sensor(naming::DEV_LAB_DOOR_RIGHT, naming::SENSOR_CLOSED_A, right_closed_a);
        right_closed_a_last = right_closed_a;
    }
    if (right_closed_b != right_closed_b_last)
    {
        publish_sensor(naming::DEV_LAB_DOOR_RIGHT, naming::SENSOR_CLOSED_B, right_closed_b);
        right_closed_b_last = right_closed_b;
    }
}

void check_ir_receiver()
{
    if (IrReceiver.decode())
    {
        uint8_t ir_code = IrReceiver.decodedIRData.command;

        JsonDocument doc;
        doc[naming::SENSOR_IR_CODE] = ir_code;
        doc["expected"] = EXPECTED_GUN_IR_CODE;
        doc["match"] = (ir_code == EXPECTED_GUN_IR_CODE);
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_IR_RECEIVER, doc);

        Serial.print(F("[IR] Received: 0x"));
        Serial.println(ir_code, HEX);

        IrReceiver.resume();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    // Initialize hoist sensors
    pinMode(PIN_HOIST_UP_A, INPUT_PULLDOWN);
    pinMode(PIN_HOIST_UP_B, INPUT_PULLDOWN);
    pinMode(PIN_HOIST_DOWN_A, INPUT_PULLDOWN);
    pinMode(PIN_HOIST_DOWN_B, INPUT_PULLDOWN);

    // Initialize left door sensors
    pinMode(PIN_LEFT_OPEN_A, INPUT_PULLDOWN);
    pinMode(PIN_LEFT_OPEN_B, INPUT_PULLDOWN);
    pinMode(PIN_LEFT_CLOSED_A, INPUT_PULLDOWN);
    pinMode(PIN_LEFT_CLOSED_B, INPUT_PULLDOWN);

    // Initialize right door sensors
    pinMode(PIN_RIGHT_OPEN_A, INPUT_PULLDOWN);
    pinMode(PIN_RIGHT_OPEN_B, INPUT_PULLDOWN);
    pinMode(PIN_RIGHT_CLOSED_A, INPUT_PULLDOWN);
    pinMode(PIN_RIGHT_CLOSED_B, INPUT_PULLDOWN);

    // Initialize outputs
    pinMode(PIN_HOIST_ENABLE, OUTPUT);
    pinMode(PIN_LAB_DOORS_ENABLE, OUTPUT);
    pinMode(PIN_ROPE_DROP, OUTPUT);
    digitalWrite(PIN_HOIST_ENABLE, LOW);
    digitalWrite(PIN_LAB_DOORS_ENABLE, LOW);
    digitalWrite(PIN_ROPE_DROP, LOW);

    // Initialize IR receiver
    IrReceiver.begin(PIN_IR_RECEIVER, DISABLE_LED_FEEDBACK);

    // Configure steppers
    hoist_stepper_one.setMaxSpeed(STEPPER_SPEED);
    hoist_stepper_one.setAcceleration(STEPPER_ACCEL);
    hoist_stepper_two.setMaxSpeed(STEPPER_SPEED);
    hoist_stepper_two.setAcceleration(STEPPER_ACCEL);

    left_stepper_one.setMaxSpeed(STEPPER_SPEED);
    left_stepper_one.setAcceleration(STEPPER_ACCEL);
    left_stepper_two.setMaxSpeed(STEPPER_SPEED);
    left_stepper_two.setAcceleration(STEPPER_ACCEL);

    right_stepper_one.setMaxSpeed(STEPPER_SPEED);
    right_stepper_one.setAcceleration(STEPPER_ACCEL);
    right_stepper_two.setMaxSpeed(STEPPER_SPEED);
    right_stepper_two.setAcceleration(STEPPER_ACCEL);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Lab Doors & Hoist] Starting..."));

    deviceRegistry.addDevice(&dev_hoist);
    deviceRegistry.addDevice(&dev_left_door);
    deviceRegistry.addDevice(&dev_right_door);
    deviceRegistry.addDevice(&dev_rope_drop);
    deviceRegistry.addDevice(&dev_ir_receiver);
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

    Serial.println(F("[Lab Doors & Hoist] Ready"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    sentient.loop();
    update_motors();
    monitor_sensors();
    check_ir_receiver();
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    if (!payload["device_id"].is<const char *>())
        return;

    const char *device_id = payload["device_id"];

    // Hoist
    if (strcmp(device_id, naming::DEV_HOIST) == 0)
    {
        if (strcmp(command, naming::CMD_UP) == 0)
        {
            set_hoist_direction(1);
            Serial.println(F("[CMD] Hoist: Up"));
        }
        else if (strcmp(command, naming::CMD_DOWN) == 0)
        {
            set_hoist_direction(-1);
            Serial.println(F("[CMD] Hoist: Down"));
        }
        else if (strcmp(command, naming::CMD_STOP) == 0)
        {
            set_hoist_direction(0);
            Serial.println(F("[CMD] Hoist: Stop"));
        }
        else
        {
            return;
        }
        publish_command_acknowledgement(device_id, command);
        return;
    }

    // Left door
    if (strcmp(device_id, naming::DEV_LAB_DOOR_LEFT) == 0)
    {
        if (strcmp(command, naming::CMD_DOOR_OPEN) == 0)
        {
            set_door_direction(true, 1);
            Serial.println(F("[CMD] Left Door: Open"));
        }
        else if (strcmp(command, naming::CMD_DOOR_CLOSE) == 0)
        {
            set_door_direction(true, -1);
            Serial.println(F("[CMD] Left Door: Close"));
        }
        else if (strcmp(command, naming::CMD_DOOR_STOP) == 0)
        {
            set_door_direction(true, 0);
            Serial.println(F("[CMD] Left Door: Stop"));
        }
        else
        {
            return;
        }
        publish_command_acknowledgement(device_id, command);
        return;
    }

    // Right door
    if (strcmp(device_id, naming::DEV_LAB_DOOR_RIGHT) == 0)
    {
        if (strcmp(command, naming::CMD_DOOR_OPEN) == 0)
        {
            set_door_direction(false, 1);
            Serial.println(F("[CMD] Right Door: Open"));
        }
        else if (strcmp(command, naming::CMD_DOOR_CLOSE) == 0)
        {
            set_door_direction(false, -1);
            Serial.println(F("[CMD] Right Door: Close"));
        }
        else if (strcmp(command, naming::CMD_DOOR_STOP) == 0)
        {
            set_door_direction(false, 0);
            Serial.println(F("[CMD] Right Door: Stop"));
        }
        else
        {
            return;
        }
        publish_command_acknowledgement(device_id, command);
        return;
    }

    // Rope drop
    if (strcmp(device_id, naming::DEV_ROPE_DROP) == 0)
    {
        if (strcmp(command, naming::CMD_DROP) == 0)
        {
            digitalWrite(PIN_ROPE_DROP, HIGH);
            Serial.println(F("[CMD] Rope Drop: Activated"));
        }
        else if (strcmp(command, naming::CMD_RESET) == 0)
        {
            digitalWrite(PIN_ROPE_DROP, LOW);
            Serial.println(F("[CMD] Rope Drop: Reset"));
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
