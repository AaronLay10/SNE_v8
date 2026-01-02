// ══════════════════════════════════════════════════════════════════════════════
// Study B Controller v2.1.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include "controller_naming.h"
#include "FirmwareMetadata.h"

// ══════════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const int PIN_POWER_LED = 13;

// Study fan stepper (DMX542)
const int PIN_FAN_STEP_POS = 0;
const int PIN_FAN_STEP_NEG = 1;
const int PIN_FAN_DIR_POS = 2;
const int PIN_FAN_DIR_NEG = 3;
const int PIN_FAN_ENABLE = 7;

// Wall gear steppers (3x DMX542)
const int PIN_GEAR1_STEP_POS = 38;
const int PIN_GEAR1_STEP_NEG = 39;
const int PIN_GEAR1_DIR_POS = 40;
const int PIN_GEAR1_DIR_NEG = 41;

const int PIN_GEAR2_STEP_POS = 20;
const int PIN_GEAR2_STEP_NEG = 21;
const int PIN_GEAR2_DIR_POS = 22;
const int PIN_GEAR2_DIR_NEG = 23;

const int PIN_GEAR3_STEP_POS = 16;
const int PIN_GEAR3_STEP_NEG = 17;
const int PIN_GEAR3_DIR_POS = 18;
const int PIN_GEAR3_DIR_NEG = 19;

const int PIN_GEARS_ENABLE = 15;
const int PIN_MOTORS_POWER = 24;

// Other outputs
const int PIN_TV_1 = 9;
const int PIN_TV_2 = 10;
const int PIN_MAKSERVO = 8;
const int PIN_FOG_POWER = 4;
const int PIN_FOG_TRIGGER = 5;
const int PIN_STUDY_FAN_LIGHT = 11;
const int PIN_BLACKLIGHTS = 36;
const int PIN_NIXIE_LEDS = 35;

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

const char *motor_commands[] = {naming::CMD_START, naming::CMD_STOP, naming::CMD_SLOW, naming::CMD_FAST};
const char *power_commands[] = {naming::CMD_ON, naming::CMD_OFF};
const char *fog_commands[] = {naming::CMD_ON, naming::CMD_OFF, naming::CMD_FOG_TRIGGER};

SentientDeviceDef dev_study_fan(naming::DEV_STUDY_FAN, "Study Fan", "stepper_motor", motor_commands, 4);
SentientDeviceDef dev_wall_gear_1(naming::DEV_WALL_GEAR_1, "Wall Gear 1", "stepper_motor", motor_commands, 4);
SentientDeviceDef dev_wall_gear_2(naming::DEV_WALL_GEAR_2, "Wall Gear 2", "stepper_motor", motor_commands, 4);
SentientDeviceDef dev_wall_gear_3(naming::DEV_WALL_GEAR_3, "Wall Gear 3", "stepper_motor", motor_commands, 4);
SentientDeviceDef dev_tv_1(naming::DEV_TV_1, "TV 1", "power_control", power_commands, 2);
SentientDeviceDef dev_tv_2(naming::DEV_TV_2, "TV 2", "power_control", power_commands, 2);
SentientDeviceDef dev_makservo(naming::DEV_MAKSERVO, "Makservo", "power_control", power_commands, 2);
SentientDeviceDef dev_fog_machine(naming::DEV_FOG_MACHINE, "Fog Machine", "fog_control", fog_commands, 3);
SentientDeviceDef dev_study_fan_light(naming::DEV_STUDY_FAN_LIGHT, "Study Fan Light", "light", power_commands, 2);
SentientDeviceDef dev_blacklights(naming::DEV_BLACKLIGHTS, "Blacklights", "light", power_commands, 2);
SentientDeviceDef dev_nixie_leds(naming::DEV_NIXIE_LEDS, "Nixie LEDs", "light", power_commands, 2);

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
// STEPPER CONTROL (Direct control, no library)
// ══════════════════════════════════════════════════════════════════════════════

enum MotorState
{
    STOPPED,
    RUNNING_SLOW,
    RUNNING_FAST
};

struct StepperMotor
{
    int step_pos_pin;
    int step_neg_pin;
    int dir_pos_pin;
    int dir_neg_pin;
    MotorState state;
    unsigned long last_step_time;
    unsigned long step_interval; // microseconds
};

StepperMotor fan_motor = {PIN_FAN_STEP_POS, PIN_FAN_STEP_NEG, PIN_FAN_DIR_POS, PIN_FAN_DIR_NEG, STOPPED, 0, 2000};
StepperMotor gear1_motor = {PIN_GEAR1_STEP_POS, PIN_GEAR1_STEP_NEG, PIN_GEAR1_DIR_POS, PIN_GEAR1_DIR_NEG, STOPPED, 0, 2000};
StepperMotor gear2_motor = {PIN_GEAR2_STEP_POS, PIN_GEAR2_STEP_NEG, PIN_GEAR2_DIR_POS, PIN_GEAR2_DIR_NEG, STOPPED, 0, 2000};
StepperMotor gear3_motor = {PIN_GEAR3_STEP_POS, PIN_GEAR3_STEP_NEG, PIN_GEAR3_DIR_POS, PIN_GEAR3_DIR_NEG, STOPPED, 0, 2000};

// Forward declarations
void step_motor(StepperMotor &motor);
void update_motors();

void step_motor(StepperMotor &motor)
{
    if (motor.state == STOPPED)
        return;

    unsigned long now = micros();
    if (now - motor.last_step_time >= motor.step_interval)
    {
        digitalWrite(motor.step_pos_pin, HIGH);
        digitalWrite(motor.step_neg_pin, LOW);
        delayMicroseconds(10);
        digitalWrite(motor.step_pos_pin, LOW);
        digitalWrite(motor.step_neg_pin, HIGH);
        motor.last_step_time = now;
    }
}

void update_motors()
{
    step_motor(fan_motor);
    step_motor(gear1_motor);
    step_motor(gear2_motor);
    step_motor(gear3_motor);
}

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    // Initialize stepper pins
    pinMode(PIN_FAN_STEP_POS, OUTPUT);
    pinMode(PIN_FAN_STEP_NEG, OUTPUT);
    pinMode(PIN_FAN_DIR_POS, OUTPUT);
    pinMode(PIN_FAN_DIR_NEG, OUTPUT);
    pinMode(PIN_FAN_ENABLE, OUTPUT);

    pinMode(PIN_GEAR1_STEP_POS, OUTPUT);
    pinMode(PIN_GEAR1_STEP_NEG, OUTPUT);
    pinMode(PIN_GEAR1_DIR_POS, OUTPUT);
    pinMode(PIN_GEAR1_DIR_NEG, OUTPUT);

    pinMode(PIN_GEAR2_STEP_POS, OUTPUT);
    pinMode(PIN_GEAR2_STEP_NEG, OUTPUT);
    pinMode(PIN_GEAR2_DIR_POS, OUTPUT);
    pinMode(PIN_GEAR2_DIR_NEG, OUTPUT);

    pinMode(PIN_GEAR3_STEP_POS, OUTPUT);
    pinMode(PIN_GEAR3_STEP_NEG, OUTPUT);
    pinMode(PIN_GEAR3_DIR_POS, OUTPUT);
    pinMode(PIN_GEAR3_DIR_NEG, OUTPUT);

    pinMode(PIN_GEARS_ENABLE, OUTPUT);
    pinMode(PIN_MOTORS_POWER, OUTPUT);

    digitalWrite(PIN_FAN_ENABLE, HIGH);   // Disabled
    digitalWrite(PIN_GEARS_ENABLE, HIGH); // Disabled
    digitalWrite(PIN_MOTORS_POWER, LOW);

    // Initialize other outputs
    pinMode(PIN_TV_1, OUTPUT);
    pinMode(PIN_TV_2, OUTPUT);
    pinMode(PIN_MAKSERVO, OUTPUT);
    pinMode(PIN_FOG_POWER, OUTPUT);
    pinMode(PIN_FOG_TRIGGER, OUTPUT);
    pinMode(PIN_STUDY_FAN_LIGHT, OUTPUT);
    pinMode(PIN_BLACKLIGHTS, OUTPUT);
    pinMode(PIN_NIXIE_LEDS, OUTPUT);

    digitalWrite(PIN_TV_1, LOW);
    digitalWrite(PIN_TV_2, LOW);
    digitalWrite(PIN_MAKSERVO, LOW);
    digitalWrite(PIN_FOG_POWER, LOW);
    digitalWrite(PIN_FOG_TRIGGER, LOW);
    digitalWrite(PIN_STUDY_FAN_LIGHT, LOW);
    digitalWrite(PIN_BLACKLIGHTS, LOW);
    digitalWrite(PIN_NIXIE_LEDS, LOW);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Study B] Starting..."));

    deviceRegistry.addDevice(&dev_study_fan);
    deviceRegistry.addDevice(&dev_wall_gear_1);
    deviceRegistry.addDevice(&dev_wall_gear_2);
    deviceRegistry.addDevice(&dev_wall_gear_3);
    deviceRegistry.addDevice(&dev_tv_1);
    deviceRegistry.addDevice(&dev_tv_2);
    deviceRegistry.addDevice(&dev_makservo);
    deviceRegistry.addDevice(&dev_fog_machine);
    deviceRegistry.addDevice(&dev_study_fan_light);
    deviceRegistry.addDevice(&dev_blacklights);
    deviceRegistry.addDevice(&dev_nixie_leds);
    // deviceRegistry.printSummary(); // Commented out to prevent stack overflow

    Serial.println(F("[Study B] Building manifest..."));
    manifest.set_controller_info(naming::CONTROLLER_ID, naming::CONTROLLER_FRIENDLY_NAME,
                                 firmware::VERSION, naming::ROOM_ID, naming::CONTROLLER_ID);
    deviceRegistry.buildManifest(manifest);
    Serial.println(F("[Study B] Manifest built"));

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

    Serial.println(F("[Study B] Ready"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    sentient.loop();
    update_motors();
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    if (!payload["device_id"].is<const char *>())
        return;

    const char *device_id = payload["device_id"];

    // Study Fan
    if (strcmp(device_id, naming::DEV_STUDY_FAN) == 0)
    {
        if (strcmp(command, naming::CMD_START) == 0 || strcmp(command, naming::CMD_SLOW) == 0)
        {
            digitalWrite(PIN_MOTORS_POWER, HIGH);
            digitalWrite(PIN_FAN_ENABLE, LOW);
            fan_motor.state = RUNNING_SLOW;
            fan_motor.step_interval = 2000;
            Serial.println(F("[CMD] Study Fan: Start/Slow"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_FAST) == 0)
        {
            digitalWrite(PIN_MOTORS_POWER, HIGH);
            digitalWrite(PIN_FAN_ENABLE, LOW);
            fan_motor.state = RUNNING_FAST;
            fan_motor.step_interval = 667;
            Serial.println(F("[CMD] Study Fan: Fast"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_STOP) == 0)
        {
            fan_motor.state = STOPPED;
            digitalWrite(PIN_FAN_ENABLE, HIGH);
            Serial.println(F("[CMD] Study Fan: Stop"));
            publish_command_acknowledgement(device_id, command);
        }
        return;
    }

    // Wall Gear 1
    if (strcmp(device_id, naming::DEV_WALL_GEAR_1) == 0)
    {
        if (strcmp(command, naming::CMD_START) == 0 || strcmp(command, naming::CMD_SLOW) == 0)
        {
            digitalWrite(PIN_MOTORS_POWER, HIGH);
            digitalWrite(PIN_GEARS_ENABLE, LOW);
            gear1_motor.state = RUNNING_SLOW;
            gear1_motor.step_interval = 2000;
            Serial.println(F("[CMD] Wall Gear 1: Start/Slow"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_FAST) == 0)
        {
            gear1_motor.state = RUNNING_FAST;
            gear1_motor.step_interval = 667;
            Serial.println(F("[CMD] Wall Gear 1: Fast"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_STOP) == 0)
        {
            gear1_motor.state = STOPPED;
            Serial.println(F("[CMD] Wall Gear 1: Stop"));
            publish_command_acknowledgement(device_id, command);
        }
        return;
    }

    // Wall Gear 2
    if (strcmp(device_id, naming::DEV_WALL_GEAR_2) == 0)
    {
        if (strcmp(command, naming::CMD_START) == 0 || strcmp(command, naming::CMD_SLOW) == 0)
        {
            digitalWrite(PIN_MOTORS_POWER, HIGH);
            digitalWrite(PIN_GEARS_ENABLE, LOW);
            gear2_motor.state = RUNNING_SLOW;
            gear2_motor.step_interval = 2000;
            Serial.println(F("[CMD] Wall Gear 2: Start/Slow"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_FAST) == 0)
        {
            gear2_motor.state = RUNNING_FAST;
            gear2_motor.step_interval = 667;
            Serial.println(F("[CMD] Wall Gear 2: Fast"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_STOP) == 0)
        {
            gear2_motor.state = STOPPED;
            Serial.println(F("[CMD] Wall Gear 2: Stop"));
            publish_command_acknowledgement(device_id, command);
        }
        return;
    }

    // Wall Gear 3
    if (strcmp(device_id, naming::DEV_WALL_GEAR_3) == 0)
    {
        if (strcmp(command, naming::CMD_START) == 0 || strcmp(command, naming::CMD_SLOW) == 0)
        {
            digitalWrite(PIN_MOTORS_POWER, HIGH);
            digitalWrite(PIN_GEARS_ENABLE, LOW);
            gear3_motor.state = RUNNING_SLOW;
            gear3_motor.step_interval = 2000;
            Serial.println(F("[CMD] Wall Gear 3: Start/Slow"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_FAST) == 0)
        {
            gear3_motor.state = RUNNING_FAST;
            gear3_motor.step_interval = 667;
            Serial.println(F("[CMD] Wall Gear 3: Fast"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_STOP) == 0)
        {
            gear3_motor.state = STOPPED;
            Serial.println(F("[CMD] Wall Gear 3: Stop"));
            publish_command_acknowledgement(device_id, command);
        }
        return;
    }

    // Simple on/off devices
    if (strcmp(device_id, naming::DEV_TV_1) == 0)
    {
        digitalWrite(PIN_TV_1, strcmp(command, naming::CMD_ON) == 0 ? HIGH : LOW);
        Serial.printf("[CMD] TV 1: %s\n", command);
        publish_command_acknowledgement(device_id, command);
        return;
    }
    if (strcmp(device_id, naming::DEV_TV_2) == 0)
    {
        digitalWrite(PIN_TV_2, strcmp(command, naming::CMD_ON) == 0 ? HIGH : LOW);
        Serial.printf("[CMD] TV 2: %s\n", command);
        publish_command_acknowledgement(device_id, command);
        return;
    }
    if (strcmp(device_id, naming::DEV_MAKSERVO) == 0)
    {
        digitalWrite(PIN_MAKSERVO, strcmp(command, naming::CMD_ON) == 0 ? HIGH : LOW);
        Serial.printf("[CMD] Makservo: %s\n", command);
        publish_command_acknowledgement(device_id, command);
        return;
    }
    if (strcmp(device_id, naming::DEV_STUDY_FAN_LIGHT) == 0)
    {
        digitalWrite(PIN_STUDY_FAN_LIGHT, strcmp(command, naming::CMD_ON) == 0 ? HIGH : LOW);
        Serial.printf("[CMD] Study Fan Light: %s\n", command);
        publish_command_acknowledgement(device_id, command);
        return;
    }
    if (strcmp(device_id, naming::DEV_BLACKLIGHTS) == 0)
    {
        digitalWrite(PIN_BLACKLIGHTS, strcmp(command, naming::CMD_ON) == 0 ? HIGH : LOW);
        Serial.printf("[CMD] Blacklights: %s\n", command);
        publish_command_acknowledgement(device_id, command);
        return;
    }
    if (strcmp(device_id, naming::DEV_NIXIE_LEDS) == 0)
    {
        digitalWrite(PIN_NIXIE_LEDS, strcmp(command, naming::CMD_ON) == 0 ? HIGH : LOW);
        Serial.printf("[CMD] Nixie LEDs: %s\n", command);
        publish_command_acknowledgement(device_id, command);
        return;
    }

    // Fog machine
    if (strcmp(device_id, naming::DEV_FOG_MACHINE) == 0)
    {
        if (strcmp(command, naming::CMD_ON) == 0)
        {
            digitalWrite(PIN_FOG_POWER, HIGH);
            Serial.println(F("[CMD] Fog Machine: Power ON"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_OFF) == 0)
        {
            digitalWrite(PIN_FOG_POWER, LOW);
            digitalWrite(PIN_FOG_TRIGGER, LOW);
            Serial.println(F("[CMD] Fog Machine: Power OFF"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_FOG_TRIGGER) == 0)
        {
            digitalWrite(PIN_FOG_TRIGGER, HIGH);
            Serial.println(F("[CMD] Fog Machine: Trigger"));
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
