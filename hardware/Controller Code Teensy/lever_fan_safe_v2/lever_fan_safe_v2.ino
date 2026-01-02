// ══════════════════════════════════════════════════════════════════════════════
// Lever Fan Safe Controller v2.1.0
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
const int PIN_PHOTOCELL_SAFE = A1;
const int PIN_PHOTOCELL_FAN = A0;
const int PIN_IR_FAN = 16;
const int PIN_IR_SAFE = 17;
const int PIN_MAGLOCK_FAN = 41;
const int PIN_SOLENOID_SAFE = 40;
const int PIN_FAN_MOTOR_ENABLE = 37;

// Stepper motor pins
const int PIN_STEPPER_1 = 33;
const int PIN_STEPPER_2 = 34;
const int PIN_STEPPER_3 = 35;
const int PIN_STEPPER_4 = 36;

const int PHOTOCELL_THRESHOLD = 300;
const unsigned long IR_SWITCH_INTERVAL = 200; // ms between IR sensor switching

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

AccelStepper stepper(AccelStepper::FULL4WIRE, PIN_STEPPER_1, PIN_STEPPER_2, PIN_STEPPER_3, PIN_STEPPER_4);

int photocell_safe = 0;
int photocell_fan = 0;
String last_state_safe = "";
String last_state_fan = "";

bool ir_enabled = true;
int current_ir_pin = PIN_IR_FAN;
unsigned long last_ir_switch = 0;
bool ir_signal_in_progress = false;

bool fan_motor_running = false;

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

// Photocell sensors
const char *photocell_sensors[] = {naming::SENSOR_LEVER_POSITION};
SentientDeviceDef dev_photocell_safe(naming::DEV_PHOTOCELL_SAFE, "Safe Photocell", "photocell", photocell_sensors, 1, true);
SentientDeviceDef dev_photocell_fan(naming::DEV_PHOTOCELL_FAN, "Fan Photocell", "photocell", photocell_sensors, 1, true);

// IR receivers
const char *ir_sensors[] = {naming::SENSOR_IR_CODE};
const char *ir_commands[] = {naming::CMD_IR_ENABLE, naming::CMD_IR_DISABLE};
SentientDeviceDef dev_ir_safe(naming::DEV_IR_SAFE, "Safe IR Receiver", "ir_receiver", ir_commands, 2, ir_sensors, 1);
SentientDeviceDef dev_ir_fan(naming::DEV_IR_FAN, "Fan IR Receiver", "ir_receiver", ir_commands, 2, ir_sensors, 1);

// Maglock
const char *maglock_commands[] = {naming::CMD_LOCK, naming::CMD_UNLOCK};
SentientDeviceDef dev_maglock_fan(naming::DEV_MAGLOCK_FAN, "Fan Maglock", "maglock", maglock_commands, 2);

// Solenoid
const char *solenoid_commands[] = {naming::CMD_OPEN, naming::CMD_CLOSE};
SentientDeviceDef dev_solenoid_safe(naming::DEV_SOLENOID_SAFE, "Safe Solenoid", "solenoid", solenoid_commands, 2);

// Fan motor
const char *fan_commands[] = {naming::CMD_FAN_ON, naming::CMD_FAN_OFF};
SentientDeviceDef dev_fan_motor(naming::DEV_FAN_MOTOR, "Fan Motor", "motor", fan_commands, 2);

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
void monitor_sensors();
void handle_ir_signal(int pin);
void switch_ir_sensor();

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    pinMode(PIN_MAGLOCK_FAN, OUTPUT);
    digitalWrite(PIN_MAGLOCK_FAN, HIGH); // Locked

    pinMode(PIN_SOLENOID_SAFE, OUTPUT);
    digitalWrite(PIN_SOLENOID_SAFE, LOW); // Closed

    pinMode(PIN_FAN_MOTOR_ENABLE, OUTPUT);
    digitalWrite(PIN_FAN_MOTOR_ENABLE, LOW); // Off

    pinMode(PIN_PHOTOCELL_SAFE, INPUT);
    pinMode(PIN_PHOTOCELL_FAN, INPUT);

    // Initialize stepper
    stepper.setMaxSpeed(3000);
    stepper.setSpeed(0);
    stepper.stop();

    // Initialize IR receiver
    IrReceiver.begin(current_ir_pin, ENABLE_LED_FEEDBACK);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[LeverFanSafe] Starting..."));

    // Register devices
    deviceRegistry.addDevice(&dev_photocell_safe);
    deviceRegistry.addDevice(&dev_photocell_fan);
    deviceRegistry.addDevice(&dev_ir_safe);
    deviceRegistry.addDevice(&dev_ir_fan);
    deviceRegistry.addDevice(&dev_maglock_fan);
    deviceRegistry.addDevice(&dev_solenoid_safe);
    deviceRegistry.addDevice(&dev_fan_motor);
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

    Serial.println(F("[LeverFanSafe] Ready"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    sentient.loop();
    monitor_sensors();

    // Handle IR signal
    if (ir_enabled && IrReceiver.decode())
    {
        ir_signal_in_progress = true;
        handle_ir_signal(current_ir_pin);
        IrReceiver.resume();
        ir_signal_in_progress = false;
        last_ir_switch = millis();
    }

    // Switch IR sensor if not in progress
    if (ir_enabled && !ir_signal_in_progress && (millis() - last_ir_switch > IR_SWITCH_INTERVAL))
    {
        switch_ir_sensor();
    }

    // Run stepper if fan motor enabled
    if (fan_motor_running && digitalRead(PIN_FAN_MOTOR_ENABLE) == HIGH)
    {
        stepper.runSpeed();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    if (!payload["device_id"].is<const char *>())
        return;

    const char *device_id = payload["device_id"];

    // Fan maglock commands
    if (strcmp(device_id, naming::DEV_MAGLOCK_FAN) == 0)
    {
        if (strcmp(command, naming::CMD_LOCK) == 0)
        {
            digitalWrite(PIN_MAGLOCK_FAN, HIGH);
            Serial.println(F("[CMD] Fan Maglock LOCKED"));
        }
        else if (strcmp(command, naming::CMD_UNLOCK) == 0)
        {
            digitalWrite(PIN_MAGLOCK_FAN, LOW);
            Serial.println(F("[CMD] Fan Maglock UNLOCKED"));
        }
    }
    // Safe solenoid commands
    else if (strcmp(device_id, naming::DEV_SOLENOID_SAFE) == 0)
    {
        if (strcmp(command, naming::CMD_OPEN) == 0)
        {
            digitalWrite(PIN_SOLENOID_SAFE, HIGH);
            Serial.println(F("[CMD] Safe Solenoid OPEN"));
        }
        else if (strcmp(command, naming::CMD_CLOSE) == 0)
        {
            digitalWrite(PIN_SOLENOID_SAFE, LOW);
            Serial.println(F("[CMD] Safe Solenoid CLOSED"));
        }
    }
    // Fan motor commands
    else if (strcmp(device_id, naming::DEV_FAN_MOTOR) == 0)
    {
        if (strcmp(command, naming::CMD_FAN_ON) == 0)
        {
            digitalWrite(PIN_FAN_MOTOR_ENABLE, LOW);
            fan_motor_running = true;
            stepper.setSpeed(1500);
            Serial.println(F("[CMD] Fan Motor ON"));
        }
        else if (strcmp(command, naming::CMD_FAN_OFF) == 0)
        {
            digitalWrite(PIN_FAN_MOTOR_ENABLE, HIGH);
            fan_motor_running = false;
            stepper.setSpeed(0);
            Serial.println(F("[CMD] Fan Motor OFF"));
        }
    }
    // IR receiver commands (both share same enable/disable)
    else if ((strcmp(device_id, naming::DEV_IR_SAFE) == 0 || strcmp(device_id, naming::DEV_IR_FAN) == 0))
    {
        if (strcmp(command, naming::CMD_IR_ENABLE) == 0)
        {
            ir_enabled = true;
            Serial.println(F("[CMD] IR Receivers ENABLED"));
        }
        else if (strcmp(command, naming::CMD_IR_DISABLE) == 0)
        {
            ir_enabled = false;
            Serial.println(F("[CMD] IR Receivers DISABLED"));
        }
        else
        {
            return;
        }
    }
    else
    {
        return; // Unknown device/command
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

// ══════════════════════════════════════════════════════════════════════════════
// SENSOR MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void monitor_sensors()
{
    if (!sentient.isConnected())
        return;

    // Safe photocell - only publish on state change
    photocell_safe = analogRead(PIN_PHOTOCELL_SAFE);
    String current_state_safe = (photocell_safe > PHOTOCELL_THRESHOLD) ? "OPEN" : "CLOSED";
    if (current_state_safe != last_state_safe)
    {
        JsonDocument doc;
        doc["lever_position"] = current_state_safe;
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_PHOTOCELL_SAFE, doc);
        last_state_safe = current_state_safe;
    }

    // Fan photocell - only publish on state change
    photocell_fan = analogRead(PIN_PHOTOCELL_FAN);
    String current_state_fan = (photocell_fan > PHOTOCELL_THRESHOLD) ? "OPEN" : "CLOSED";
    if (current_state_fan != last_state_fan)
    {
        JsonDocument doc;
        doc["lever_position"] = current_state_fan;
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_PHOTOCELL_FAN, doc);
        last_state_fan = current_state_fan;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// IR SIGNAL HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_ir_signal(int pin)
{
    // Filter noise
    bool is_noise = (IrReceiver.decodedIRData.command == 0 &&
                     IrReceiver.decodedIRData.address == 0 &&
                     IrReceiver.decodedIRData.decodedRawData == 0 &&
                     IrReceiver.decodedIRData.numberOfBits == 0);

    if (is_noise || !sentient.isConnected())
        return;

    // Determine which IR receiver detected the signal
    const char *device_id = (pin == PIN_IR_SAFE) ? naming::DEV_IR_SAFE : naming::DEV_IR_FAN;

    JsonDocument doc;
    doc["ir_code"] = IrReceiver.decodedIRData.command;
    doc["address"] = IrReceiver.decodedIRData.address;
    doc["protocol"] = IrReceiver.decodedIRData.protocol;
    doc["raw"] = (unsigned long)IrReceiver.decodedIRData.decodedRawData;
    sentient.publishJson(naming::CAT_SENSORS, device_id, doc);

    Serial.print(F("[IR] "));
    Serial.print(device_id);
    Serial.print(F(" Code: 0x"));
    Serial.println(IrReceiver.decodedIRData.command, HEX);
}

// ══════════════════════════════════════════════════════════════════════════════
// IR SENSOR SWITCHING
// ══════════════════════════════════════════════════════════════════════════════

void switch_ir_sensor()
{
    if (current_ir_pin == PIN_IR_FAN)
    {
        current_ir_pin = PIN_IR_SAFE;
    }
    else
    {
        current_ir_pin = PIN_IR_FAN;
    }

    IrReceiver.begin(current_ir_pin, ENABLE_LED_FEEDBACK);
    last_ir_switch = millis();
}
