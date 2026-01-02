// ══════════════════════════════════════════════════════════════════════════════
// Study A Controller v2.1.0
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

// Tentacle move outputs (2x2 = 4 outputs)
const int PIN_TENTACLE_MOVE_A1 = 0;
const int PIN_TENTACLE_MOVE_A2 = 1;
const int PIN_TENTACLE_MOVE_B1 = 2;
const int PIN_TENTACLE_MOVE_B2 = 3;

// Porthole sensors (6 inputs)
const int PIN_PORTHOLE_A1 = 5;
const int PIN_PORTHOLE_A2 = 6;
const int PIN_PORTHOLE_B1 = 7;
const int PIN_PORTHOLE_B2 = 8;
const int PIN_PORTHOLE_C1 = 9;
const int PIN_PORTHOLE_C2 = 10;

// Riddle motor
const int PIN_RIDDLE_MOTOR = 12;

// Tentacle sensors (16 inputs)
const int PIN_TENTACLE_A1 = 14;
const int PIN_TENTACLE_A2 = 15;
const int PIN_TENTACLE_A3 = 16;
const int PIN_TENTACLE_A4 = 17;
const int PIN_TENTACLE_B1 = 18;
const int PIN_TENTACLE_B2 = 19;
const int PIN_TENTACLE_B3 = 20;
const int PIN_TENTACLE_B4 = 21;
const int PIN_TENTACLE_C1 = 22;
const int PIN_TENTACLE_C2 = 23;
const int PIN_TENTACLE_C3 = 36;
const int PIN_TENTACLE_C4 = 37;
const int PIN_TENTACLE_D1 = 38;
const int PIN_TENTACLE_D2 = 39;
const int PIN_TENTACLE_D3 = 40;
const int PIN_TENTACLE_D4 = 41;

// Porthole control (2 outputs)
const int PIN_PORTHOLE_OPEN = 33;
const int PIN_PORTHOLE_CLOSE = 34;

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

// Sensor state tracking
int porthole_a1_last = -1, porthole_a2_last = -1;
int porthole_b1_last = -1, porthole_b2_last = -1;
int porthole_c1_last = -1, porthole_c2_last = -1;

int tentacle_a1_last = -1, tentacle_a2_last = -1, tentacle_a3_last = -1, tentacle_a4_last = -1;
int tentacle_b1_last = -1, tentacle_b2_last = -1, tentacle_b3_last = -1, tentacle_b4_last = -1;
int tentacle_c1_last = -1, tentacle_c2_last = -1, tentacle_c3_last = -1, tentacle_c4_last = -1;
int tentacle_d1_last = -1, tentacle_d2_last = -1, tentacle_d3_last = -1, tentacle_d4_last = -1;

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

const char *mover_commands[] = {naming::CMD_UP, naming::CMD_DOWN, naming::CMD_STOP};
const char *motor_commands[] = {naming::CMD_MOTOR_ON, naming::CMD_MOTOR_OFF};
const char *porthole_control_commands[] = {naming::CMD_OPEN, naming::CMD_CLOSE};

const char *porthole_sensors[] = {
    naming::SENSOR_PORTHOLE_A1, naming::SENSOR_PORTHOLE_A2,
    naming::SENSOR_PORTHOLE_B1, naming::SENSOR_PORTHOLE_B2,
    naming::SENSOR_PORTHOLE_C1, naming::SENSOR_PORTHOLE_C2};

const char *tentacle_sensors[] = {
    naming::SENSOR_TENTACLE_A1, naming::SENSOR_TENTACLE_A2, naming::SENSOR_TENTACLE_A3, naming::SENSOR_TENTACLE_A4,
    naming::SENSOR_TENTACLE_B1, naming::SENSOR_TENTACLE_B2, naming::SENSOR_TENTACLE_B3, naming::SENSOR_TENTACLE_B4,
    naming::SENSOR_TENTACLE_C1, naming::SENSOR_TENTACLE_C2, naming::SENSOR_TENTACLE_C3, naming::SENSOR_TENTACLE_C4,
    naming::SENSOR_TENTACLE_D1, naming::SENSOR_TENTACLE_D2, naming::SENSOR_TENTACLE_D3, naming::SENSOR_TENTACLE_D4};

SentientDeviceDef dev_tentacle_mover_a(naming::DEV_TENTACLE_MOVER_A, "Tentacle Mover A", "motor", mover_commands, 3);
SentientDeviceDef dev_tentacle_mover_b(naming::DEV_TENTACLE_MOVER_B, "Tentacle Mover B", "motor", mover_commands, 3);
SentientDeviceDef dev_riddle_motor(naming::DEV_RIDDLE_MOTOR, "Riddle Motor", "motor", motor_commands, 2);
SentientDeviceDef dev_porthole_controller(naming::DEV_PORTHOLE_CONTROLLER, "Porthole Controller", "actuator", porthole_control_commands, 2, porthole_sensors, 6);
SentientDeviceDef dev_tentacle_sensors(naming::DEV_TENTACLE_SENSORS, "Tentacle Sensors", "sensor_array", nullptr, 0, tentacle_sensors, 16);

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
// SENSOR MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void publish_sensor(const char *device_id, const char *sensor_name, int value)
{
    JsonDocument doc;
    doc[sensor_name] = (value == LOW); // INPUT_PULLUP inverted
    sentient.publishJson(naming::CAT_SENSORS, device_id, doc);
}

void monitor_sensors()
{
    // Porthole sensors
    int pa1 = digitalRead(PIN_PORTHOLE_A1);
    int pa2 = digitalRead(PIN_PORTHOLE_A2);
    int pb1 = digitalRead(PIN_PORTHOLE_B1);
    int pb2 = digitalRead(PIN_PORTHOLE_B2);
    int pc1 = digitalRead(PIN_PORTHOLE_C1);
    int pc2 = digitalRead(PIN_PORTHOLE_C2);

    if (pa1 != porthole_a1_last)
    {
        publish_sensor(naming::DEV_PORTHOLE_CONTROLLER, naming::SENSOR_PORTHOLE_A1, pa1);
        porthole_a1_last = pa1;
    }
    if (pa2 != porthole_a2_last)
    {
        publish_sensor(naming::DEV_PORTHOLE_CONTROLLER, naming::SENSOR_PORTHOLE_A2, pa2);
        porthole_a2_last = pa2;
    }
    if (pb1 != porthole_b1_last)
    {
        publish_sensor(naming::DEV_PORTHOLE_CONTROLLER, naming::SENSOR_PORTHOLE_B1, pb1);
        porthole_b1_last = pb1;
    }
    if (pb2 != porthole_b2_last)
    {
        publish_sensor(naming::DEV_PORTHOLE_CONTROLLER, naming::SENSOR_PORTHOLE_B2, pb2);
        porthole_b2_last = pb2;
    }
    if (pc1 != porthole_c1_last)
    {
        publish_sensor(naming::DEV_PORTHOLE_CONTROLLER, naming::SENSOR_PORTHOLE_C1, pc1);
        porthole_c1_last = pc1;
    }
    if (pc2 != porthole_c2_last)
    {
        publish_sensor(naming::DEV_PORTHOLE_CONTROLLER, naming::SENSOR_PORTHOLE_C2, pc2);
        porthole_c2_last = pc2;
    }

    // Tentacle sensors
    int ta1 = digitalRead(PIN_TENTACLE_A1);
    int ta2 = digitalRead(PIN_TENTACLE_A2);
    int ta3 = digitalRead(PIN_TENTACLE_A3);
    int ta4 = digitalRead(PIN_TENTACLE_A4);
    int tb1 = digitalRead(PIN_TENTACLE_B1);
    int tb2 = digitalRead(PIN_TENTACLE_B2);
    int tb3 = digitalRead(PIN_TENTACLE_B3);
    int tb4 = digitalRead(PIN_TENTACLE_B4);
    int tc1 = digitalRead(PIN_TENTACLE_C1);
    int tc2 = digitalRead(PIN_TENTACLE_C2);
    int tc3 = digitalRead(PIN_TENTACLE_C3);
    int tc4 = digitalRead(PIN_TENTACLE_C4);
    int td1 = digitalRead(PIN_TENTACLE_D1);
    int td2 = digitalRead(PIN_TENTACLE_D2);
    int td3 = digitalRead(PIN_TENTACLE_D3);
    int td4 = digitalRead(PIN_TENTACLE_D4);

    if (ta1 != tentacle_a1_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_A1, ta1);
        tentacle_a1_last = ta1;
    }
    if (ta2 != tentacle_a2_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_A2, ta2);
        tentacle_a2_last = ta2;
    }
    if (ta3 != tentacle_a3_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_A3, ta3);
        tentacle_a3_last = ta3;
    }
    if (ta4 != tentacle_a4_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_A4, ta4);
        tentacle_a4_last = ta4;
    }
    if (tb1 != tentacle_b1_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_B1, tb1);
        tentacle_b1_last = tb1;
    }
    if (tb2 != tentacle_b2_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_B2, tb2);
        tentacle_b2_last = tb2;
    }
    if (tb3 != tentacle_b3_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_B3, tb3);
        tentacle_b3_last = tb3;
    }
    if (tb4 != tentacle_b4_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_B4, tb4);
        tentacle_b4_last = tb4;
    }
    if (tc1 != tentacle_c1_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_C1, tc1);
        tentacle_c1_last = tc1;
    }
    if (tc2 != tentacle_c2_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_C2, tc2);
        tentacle_c2_last = tc2;
    }
    if (tc3 != tentacle_c3_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_C3, tc3);
        tentacle_c3_last = tc3;
    }
    if (tc4 != tentacle_c4_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_C4, tc4);
        tentacle_c4_last = tc4;
    }
    if (td1 != tentacle_d1_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_D1, td1);
        tentacle_d1_last = td1;
    }
    if (td2 != tentacle_d2_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_D2, td2);
        tentacle_d2_last = td2;
    }
    if (td3 != tentacle_d3_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_D3, td3);
        tentacle_d3_last = td3;
    }
    if (td4 != tentacle_d4_last)
    {
        publish_sensor(naming::DEV_TENTACLE_SENSORS, naming::SENSOR_TENTACLE_D4, td4);
        tentacle_d4_last = td4;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    // Initialize tentacle move outputs
    pinMode(PIN_TENTACLE_MOVE_A1, OUTPUT);
    pinMode(PIN_TENTACLE_MOVE_A2, OUTPUT);
    pinMode(PIN_TENTACLE_MOVE_B1, OUTPUT);
    pinMode(PIN_TENTACLE_MOVE_B2, OUTPUT);
    digitalWrite(PIN_TENTACLE_MOVE_A1, LOW);
    digitalWrite(PIN_TENTACLE_MOVE_A2, LOW);
    digitalWrite(PIN_TENTACLE_MOVE_B1, LOW);
    digitalWrite(PIN_TENTACLE_MOVE_B2, LOW);

    // Initialize porthole sensors
    pinMode(PIN_PORTHOLE_A1, INPUT_PULLUP);
    pinMode(PIN_PORTHOLE_A2, INPUT_PULLUP);
    pinMode(PIN_PORTHOLE_B1, INPUT_PULLUP);
    pinMode(PIN_PORTHOLE_B2, INPUT_PULLUP);
    pinMode(PIN_PORTHOLE_C1, INPUT_PULLUP);
    pinMode(PIN_PORTHOLE_C2, INPUT_PULLUP);

    // Initialize riddle motor
    pinMode(PIN_RIDDLE_MOTOR, OUTPUT);
    digitalWrite(PIN_RIDDLE_MOTOR, LOW);

    // Initialize tentacle sensors
    pinMode(PIN_TENTACLE_A1, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_A2, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_A3, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_A4, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_B1, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_B2, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_B3, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_B4, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_C1, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_C2, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_C3, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_C4, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_D1, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_D2, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_D3, INPUT_PULLUP);
    pinMode(PIN_TENTACLE_D4, INPUT_PULLUP);

    // Initialize porthole control
    pinMode(PIN_PORTHOLE_OPEN, OUTPUT);
    pinMode(PIN_PORTHOLE_CLOSE, OUTPUT);
    digitalWrite(PIN_PORTHOLE_OPEN, LOW);
    digitalWrite(PIN_PORTHOLE_CLOSE, HIGH);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Study A] Starting..."));

    deviceRegistry.addDevice(&dev_tentacle_mover_a);
    deviceRegistry.addDevice(&dev_tentacle_mover_b);
    deviceRegistry.addDevice(&dev_riddle_motor);
    deviceRegistry.addDevice(&dev_porthole_controller);
    deviceRegistry.addDevice(&dev_tentacle_sensors);
    // deviceRegistry.printSummary(); // Commented out - causes crash with large sensor array

    Serial.println(F("[Study A] Building manifest..."));
    manifest.set_controller_info(naming::CONTROLLER_ID, naming::CONTROLLER_FRIENDLY_NAME,
                                 firmware::VERSION, naming::ROOM_ID, naming::CONTROLLER_ID);
    deviceRegistry.buildManifest(manifest);
    Serial.println(F("[Study A] Manifest built"));

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

    Serial.println(F("[Study A] Ready"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    sentient.loop();
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

    // Tentacle Mover A
    if (strcmp(device_id, naming::DEV_TENTACLE_MOVER_A) == 0)
    {
        if (strcmp(command, naming::CMD_UP) == 0)
        {
            digitalWrite(PIN_TENTACLE_MOVE_A1, HIGH);
            digitalWrite(PIN_TENTACLE_MOVE_A2, LOW);
            Serial.println(F("[CMD] Tentacle Mover A: Up"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_DOWN) == 0)
        {
            digitalWrite(PIN_TENTACLE_MOVE_A1, LOW);
            digitalWrite(PIN_TENTACLE_MOVE_A2, HIGH);
            Serial.println(F("[CMD] Tentacle Mover A: Down"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_STOP) == 0)
        {
            digitalWrite(PIN_TENTACLE_MOVE_A1, LOW);
            digitalWrite(PIN_TENTACLE_MOVE_A2, LOW);
            Serial.println(F("[CMD] Tentacle Mover A: Stop"));
            publish_command_acknowledgement(device_id, command);
        }
        return;
    }

    // Tentacle Mover B
    if (strcmp(device_id, naming::DEV_TENTACLE_MOVER_B) == 0)
    {
        if (strcmp(command, naming::CMD_UP) == 0)
        {
            digitalWrite(PIN_TENTACLE_MOVE_B1, HIGH);
            digitalWrite(PIN_TENTACLE_MOVE_B2, LOW);
            Serial.println(F("[CMD] Tentacle Mover B: Up"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_DOWN) == 0)
        {
            digitalWrite(PIN_TENTACLE_MOVE_B1, LOW);
            digitalWrite(PIN_TENTACLE_MOVE_B2, HIGH);
            Serial.println(F("[CMD] Tentacle Mover B: Down"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_STOP) == 0)
        {
            digitalWrite(PIN_TENTACLE_MOVE_B1, LOW);
            digitalWrite(PIN_TENTACLE_MOVE_B2, LOW);
            Serial.println(F("[CMD] Tentacle Mover B: Stop"));
            publish_command_acknowledgement(device_id, command);
        }
        return;
    }

    // Riddle Motor
    if (strcmp(device_id, naming::DEV_RIDDLE_MOTOR) == 0)
    {
        if (strcmp(command, naming::CMD_MOTOR_ON) == 0)
        {
            digitalWrite(PIN_RIDDLE_MOTOR, HIGH);
            Serial.println(F("[CMD] Riddle Motor: ON"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_MOTOR_OFF) == 0)
        {
            digitalWrite(PIN_RIDDLE_MOTOR, LOW);
            Serial.println(F("[CMD] Riddle Motor: OFF"));
            publish_command_acknowledgement(device_id, command);
        }
        return;
    }

    // Porthole Controller
    if (strcmp(device_id, naming::DEV_PORTHOLE_CONTROLLER) == 0)
    {
        if (strcmp(command, naming::CMD_OPEN) == 0)
        {
            digitalWrite(PIN_PORTHOLE_OPEN, HIGH);
            digitalWrite(PIN_PORTHOLE_CLOSE, LOW);
            Serial.println(F("[CMD] Portholes: Open"));
            publish_command_acknowledgement(device_id, command);
        }
        else if (strcmp(command, naming::CMD_CLOSE) == 0)
        {
            digitalWrite(PIN_PORTHOLE_OPEN, LOW);
            digitalWrite(PIN_PORTHOLE_CLOSE, HIGH);
            Serial.println(F("[CMD] Portholes: Close"));
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
