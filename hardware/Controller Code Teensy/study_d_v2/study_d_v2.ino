// ══════════════════════════════════════════════════════════════════════════════
// Study D Controller v2.1.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <TeensyDMX.h>
#include "controller_naming.h"
#include "FirmwareMetadata.h"

// ══════════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const int PIN_POWER_LED = 13;

// Motor left (dual step/dir pins)
const int PIN_MOTOR_LEFT_STEP_1 = 15;
const int PIN_MOTOR_LEFT_STEP_2 = 16;
const int PIN_MOTOR_LEFT_DIR_1 = 17;
const int PIN_MOTOR_LEFT_DIR_2 = 18;

// Motor right (dual step/dir pins)
const int PIN_MOTOR_RIGHT_STEP_1 = 20;
const int PIN_MOTOR_RIGHT_STEP_2 = 21;
const int PIN_MOTOR_RIGHT_DIR_1 = 22;
const int PIN_MOTOR_RIGHT_DIR_2 = 23;

const int PIN_MOTORS_ENABLE = 14;
const int PIN_MOTORS_POWER = 1;

// Proximity sensors (8 total)
const int PIN_LEFT_TOP_1 = 39;
const int PIN_LEFT_TOP_2 = 40;
const int PIN_LEFT_BOTTOM_1 = 38;
const int PIN_LEFT_BOTTOM_2 = 41;
const int PIN_RIGHT_TOP_1 = 35;
const int PIN_RIGHT_TOP_2 = 36;
const int PIN_RIGHT_BOTTOM_1 = 34;
const int PIN_RIGHT_BOTTOM_2 = 37;

// DMX fog machine (Serial7)
const int PIN_DMX_TX = 29;
const int PIN_DMX_RX = 28;
const int PIN_DMX_ENABLE = 30;
const int FOG_DMX_CHANNEL_VOLUME = 1;
const int FOG_DMX_CHANNEL_TIMER = 2;
const int FOG_DMX_CHANNEL_FAN_SPEED = 3;

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

const char *motor_commands[] = {naming::CMD_UP, naming::CMD_DOWN, naming::CMD_STOP};
const char *fog_commands[] = {naming::CMD_SET_VOLUME, naming::CMD_SET_TIMER, naming::CMD_SET_FAN_SPEED};
const char *proximity_sensors[] = {
    naming::SENSOR_LEFT_TOP_1, naming::SENSOR_LEFT_TOP_2,
    naming::SENSOR_LEFT_BOTTOM_1, naming::SENSOR_LEFT_BOTTOM_2,
    naming::SENSOR_RIGHT_TOP_1, naming::SENSOR_RIGHT_TOP_2,
    naming::SENSOR_RIGHT_BOTTOM_1, naming::SENSOR_RIGHT_BOTTOM_2};

SentientDeviceDef dev_motor_left(naming::DEV_MOTOR_LEFT, "Motor Left", "stepper_motor", motor_commands, 3);
SentientDeviceDef dev_motor_right(naming::DEV_MOTOR_RIGHT, "Motor Right", "stepper_motor", motor_commands, 3);
SentientDeviceDef dev_proximity_sensors(naming::DEV_PROXIMITY_SENSORS, "Proximity Sensors", "proximity_array", nullptr, 0, proximity_sensors, 8);
SentientDeviceDef dev_fog_dmx(naming::DEV_FOG_DMX, "DMX Fog Machine", "dmx_device", fog_commands, 3);

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
// DMX SETUP
// ══════════════════════════════════════════════════════════════════════════════

namespace teensydmx = ::qindesign::teensydmx;
teensydmx::Sender dmxTx{Serial7};

// ══════════════════════════════════════════════════════════════════════════════
// MOTOR CONTROL
// ══════════════════════════════════════════════════════════════════════════════

enum MotorDirection
{
    MOTOR_STOPPED,
    MOTOR_UP,
    MOTOR_DOWN
};

struct Motor
{
    int step_pin_1;
    int step_pin_2;
    int dir_pin_1;
    int dir_pin_2;
    MotorDirection direction;
    unsigned long last_step_time;
    unsigned long step_interval; // microseconds
};

Motor motor_left = {PIN_MOTOR_LEFT_STEP_1, PIN_MOTOR_LEFT_STEP_2, PIN_MOTOR_LEFT_DIR_1, PIN_MOTOR_LEFT_DIR_2, MOTOR_STOPPED, 0, 1000};
Motor motor_right = {PIN_MOTOR_RIGHT_STEP_1, PIN_MOTOR_RIGHT_STEP_2, PIN_MOTOR_RIGHT_DIR_1, PIN_MOTOR_RIGHT_DIR_2, MOTOR_STOPPED, 0, 1000};

// Forward declarations
void step_motor(Motor &motor);
void update_motors();

void step_motor(Motor &motor)
{
    if (motor.direction == MOTOR_STOPPED)
        return;

    unsigned long now = micros();
    if (now - motor.last_step_time >= motor.step_interval)
    {
        digitalWrite(motor.step_pin_1, HIGH);
        digitalWrite(motor.step_pin_2, LOW);
        delayMicroseconds(10);
        digitalWrite(motor.step_pin_1, LOW);
        digitalWrite(motor.step_pin_2, HIGH);
        motor.last_step_time = now;
    }
}

void update_motors()
{
    step_motor(motor_left);
    step_motor(motor_right);
}

// ══════════════════════════════════════════════════════════════════════════════
// SENSOR MONITORING
// ══════════════════════════════════════════════════════════════════════════════

struct ProximitySensors
{
    int left_top_1;
    int left_top_2;
    int left_bottom_1;
    int left_bottom_2;
    int right_top_1;
    int right_top_2;
    int right_bottom_1;
    int right_bottom_2;
};

ProximitySensors sensor_last = {-1, -1, -1, -1, -1, -1, -1, -1};

void monitor_sensors()
{
    ProximitySensors current;
    current.left_top_1 = digitalRead(PIN_LEFT_TOP_1);
    current.left_top_2 = digitalRead(PIN_LEFT_TOP_2);
    current.left_bottom_1 = digitalRead(PIN_LEFT_BOTTOM_1);
    current.left_bottom_2 = digitalRead(PIN_LEFT_BOTTOM_2);
    current.right_top_1 = digitalRead(PIN_RIGHT_TOP_1);
    current.right_top_2 = digitalRead(PIN_RIGHT_TOP_2);
    current.right_bottom_1 = digitalRead(PIN_RIGHT_BOTTOM_1);
    current.right_bottom_2 = digitalRead(PIN_RIGHT_BOTTOM_2);

    if (current.left_top_1 != sensor_last.left_top_1 ||
        current.left_top_2 != sensor_last.left_top_2 ||
        current.left_bottom_1 != sensor_last.left_bottom_1 ||
        current.left_bottom_2 != sensor_last.left_bottom_2 ||
        current.right_top_1 != sensor_last.right_top_1 ||
        current.right_top_2 != sensor_last.right_top_2 ||
        current.right_bottom_1 != sensor_last.right_bottom_1 ||
        current.right_bottom_2 != sensor_last.right_bottom_2)
    {
        JsonDocument doc;
        doc[naming::SENSOR_LEFT_TOP_1] = current.left_top_1;
        doc[naming::SENSOR_LEFT_TOP_2] = current.left_top_2;
        doc[naming::SENSOR_LEFT_BOTTOM_1] = current.left_bottom_1;
        doc[naming::SENSOR_LEFT_BOTTOM_2] = current.left_bottom_2;
        doc[naming::SENSOR_RIGHT_TOP_1] = current.right_top_1;
        doc[naming::SENSOR_RIGHT_TOP_2] = current.right_top_2;
        doc[naming::SENSOR_RIGHT_BOTTOM_1] = current.right_bottom_1;
        doc[naming::SENSOR_RIGHT_BOTTOM_2] = current.right_bottom_2;

        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_PROXIMITY_SENSORS, doc);
        sensor_last = current;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    // Initialize motor pins
    pinMode(PIN_MOTOR_LEFT_STEP_1, OUTPUT);
    pinMode(PIN_MOTOR_LEFT_STEP_2, OUTPUT);
    pinMode(PIN_MOTOR_LEFT_DIR_1, OUTPUT);
    pinMode(PIN_MOTOR_LEFT_DIR_2, OUTPUT);

    pinMode(PIN_MOTOR_RIGHT_STEP_1, OUTPUT);
    pinMode(PIN_MOTOR_RIGHT_STEP_2, OUTPUT);
    pinMode(PIN_MOTOR_RIGHT_DIR_1, OUTPUT);
    pinMode(PIN_MOTOR_RIGHT_DIR_2, OUTPUT);

    pinMode(PIN_MOTORS_ENABLE, OUTPUT);
    pinMode(PIN_MOTORS_POWER, OUTPUT);

    digitalWrite(PIN_MOTORS_ENABLE, HIGH); // Disabled
    digitalWrite(PIN_MOTORS_POWER, LOW);

    // Initialize proximity sensors
    pinMode(PIN_LEFT_TOP_1, INPUT);
    pinMode(PIN_LEFT_TOP_2, INPUT);
    pinMode(PIN_LEFT_BOTTOM_1, INPUT);
    pinMode(PIN_LEFT_BOTTOM_2, INPUT);
    pinMode(PIN_RIGHT_TOP_1, INPUT);
    pinMode(PIN_RIGHT_TOP_2, INPUT);
    pinMode(PIN_RIGHT_BOTTOM_1, INPUT);
    pinMode(PIN_RIGHT_BOTTOM_2, INPUT);

    // Initialize DMX
    pinMode(PIN_DMX_TX, OUTPUT);
    pinMode(PIN_DMX_RX, INPUT);
    pinMode(PIN_DMX_ENABLE, OUTPUT);
    digitalWrite(PIN_DMX_ENABLE, HIGH); // Enable RS485 transmitter

    Serial7.begin(250000); // DMX baud rate
    dmxTx.begin();
    dmxTx.setPacketSize(3); // 3 channels for fog machine

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Study D] Starting..."));

    deviceRegistry.addDevice(&dev_motor_left);
    deviceRegistry.addDevice(&dev_motor_right);
    deviceRegistry.addDevice(&dev_proximity_sensors);
    deviceRegistry.addDevice(&dev_fog_dmx);
    // deviceRegistry.printSummary(); // Commented out to prevent stack overflow

    Serial.println(F("[Study D] Building manifest..."));
    manifest.set_controller_info(naming::CONTROLLER_ID, naming::CONTROLLER_FRIENDLY_NAME,
                                 firmware::VERSION, naming::ROOM_ID, naming::CONTROLLER_ID);
    deviceRegistry.buildManifest(manifest);
    Serial.println(F("[Study D] Manifest built"));

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

    Serial.println(F("[Study D] Ready"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    sentient.loop();
    update_motors();
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

    // Motor Left
    if (strcmp(device_id, naming::DEV_MOTOR_LEFT) == 0)
    {
        if (strcmp(command, naming::CMD_UP) == 0)
        {
            digitalWrite(PIN_MOTORS_POWER, HIGH);
            digitalWrite(PIN_MOTORS_ENABLE, LOW);
            digitalWrite(motor_left.dir_pin_1, HIGH);
            digitalWrite(motor_left.dir_pin_2, LOW);
            motor_left.direction = MOTOR_UP;
            Serial.println(F("[CMD] Motor Left: Up"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_DOWN) == 0)
        {
            digitalWrite(PIN_MOTORS_POWER, HIGH);
            digitalWrite(PIN_MOTORS_ENABLE, LOW);
            digitalWrite(motor_left.dir_pin_1, LOW);
            digitalWrite(motor_left.dir_pin_2, HIGH);
            motor_left.direction = MOTOR_DOWN;
            Serial.println(F("[CMD] Motor Left: Down"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_STOP) == 0)
        {
            motor_left.direction = MOTOR_STOPPED;
            if (motor_right.direction == MOTOR_STOPPED)
            {
                digitalWrite(PIN_MOTORS_ENABLE, HIGH);
            }
            Serial.println(F("[CMD] Motor Left: Stop"));
            publish_command_acknowledgement(device_id, command);
        }
        return;
    }

    // Motor Right
    if (strcmp(device_id, naming::DEV_MOTOR_RIGHT) == 0)
    {
        if (strcmp(command, naming::CMD_UP) == 0)
        {
            digitalWrite(PIN_MOTORS_POWER, HIGH);
            digitalWrite(PIN_MOTORS_ENABLE, LOW);
            digitalWrite(motor_right.dir_pin_1, HIGH);
            digitalWrite(motor_right.dir_pin_2, LOW);
            motor_right.direction = MOTOR_UP;
            Serial.println(F("[CMD] Motor Right: Up"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_DOWN) == 0)
        {
            digitalWrite(PIN_MOTORS_POWER, HIGH);
            digitalWrite(PIN_MOTORS_ENABLE, LOW);
            digitalWrite(motor_right.dir_pin_1, LOW);
            digitalWrite(motor_right.dir_pin_2, HIGH);
            motor_right.direction = MOTOR_DOWN;
            Serial.println(F("[CMD] Motor Right: Down"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_STOP) == 0)
        {
            motor_right.direction = MOTOR_STOPPED;
            if (motor_left.direction == MOTOR_STOPPED)
            {
                digitalWrite(PIN_MOTORS_ENABLE, HIGH);
            }
            Serial.println(F("[CMD] Motor Right: Stop"));
            publish_command_acknowledgement(device_id, command);
        }
        return;
    }

    // DMX Fog Machine
    if (strcmp(device_id, naming::DEV_FOG_DMX) == 0)
    {
        if (strcmp(command, naming::CMD_SET_VOLUME) == 0 && payload["value"].is<int>())
        {
            int value = payload["value"];
            value = constrain(value, 0, 255);
            dmxTx.set(FOG_DMX_CHANNEL_VOLUME, value);
            Serial.printf("[CMD] Fog DMX: Volume = %d\n", value);
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_SET_TIMER) == 0 && payload["value"].is<int>())
        {
            int value = payload["value"];
            value = constrain(value, 0, 255);
            dmxTx.set(FOG_DMX_CHANNEL_TIMER, value);
            Serial.printf("[CMD] Fog DMX: Timer = %d\n", value);
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_SET_FAN_SPEED) == 0 && payload["value"].is<int>())
        {
            int value = payload["value"];
            value = constrain(value, 0, 255);
            dmxTx.set(FOG_DMX_CHANNEL_FAN_SPEED, value);
            Serial.printf("[CMD] Fog DMX: Fan Speed = %d\n", value);
            publish_command_acknowledgement(device_id, command);
        }
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
