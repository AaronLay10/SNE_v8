// ══════════════════════════════════════════════════════════════════════════════
// Fuse Box Controller v2.1.0
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

// Pin Definitions
const int PIN_POWER_LED = 13;

// RFID Readers
#define RFID_A Serial1 // Pins 0,1
#define RFID_B Serial2 // Pins 7,8
#define RFID_C Serial3 // Pins 14,15
#define RFID_D Serial4 // Pins 16,17
#define RFID_E Serial5 // Pins 20,21

// TIR (Tag In Range) Sensors
const int PIN_TIR_A = 4;
const int PIN_TIR_B = 5;
const int PIN_TIR_C = 2;
const int PIN_TIR_D = 6;
const int PIN_TIR_E = 3;

// Actuator
const int PIN_ACTUATOR_FWD = 33;
const int PIN_ACTUATOR_RWD = 34;

// Maglocks
const int PIN_MAGLOCK_B = 10;
const int PIN_MAGLOCK_C = 11;
const int PIN_MAGLOCK_D = 12;

// Metal Gate
const int PIN_METAL_GATE = 31;

// Fuse Resistor Sensors
const int PIN_FUSE_A = A5;
const int PIN_FUSE_B = A8;
const int PIN_FUSE_C = A9;

// Knife Switch
const int PIN_KNIFE_SWITCH = 18;

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
// STATE MANAGEMENT
// ══════════════════════════════════════════════════════════════════════════════

// RFID tag storage
const int TAG_LENGTH = 20;
char tag_a[TAG_LENGTH] = "";
char tag_b[TAG_LENGTH] = "";
char tag_c[TAG_LENGTH] = "";
char tag_d[TAG_LENGTH] = "";
char tag_e[TAG_LENGTH] = "";

char last_tag_a[TAG_LENGTH] = "";
char last_tag_b[TAG_LENGTH] = "";
char last_tag_c[TAG_LENGTH] = "";
char last_tag_d[TAG_LENGTH] = "";
char last_tag_e[TAG_LENGTH] = "";

// Resistor values (bucketed to 0, 100, 200, 300)
int resistor_a = 0;
int resistor_b = 0;
int resistor_c = 0;

int last_resistor_a = -1;
int last_resistor_b = -1;
int last_resistor_c = -1;

// Knife switch state
bool knife_switch_state = false;
bool last_knife_switch_state = false;

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

// RFID sensors (input only)
const char *rfid_a_sensors[] = {naming::SENSOR_RFID_TAG};
const char *rfid_b_sensors[] = {naming::SENSOR_RFID_TAG};
const char *rfid_c_sensors[] = {naming::SENSOR_RFID_TAG};
const char *rfid_d_sensors[] = {naming::SENSOR_RFID_TAG};
const char *rfid_e_sensors[] = {naming::SENSOR_RFID_TAG};

SentientDeviceDef dev_rfid_a(naming::DEV_RFID_A, naming::FRIENDLY_RFID_A, "rfid_reader", rfid_a_sensors, 1, true);
SentientDeviceDef dev_rfid_b(naming::DEV_RFID_B, naming::FRIENDLY_RFID_B, "rfid_reader", rfid_b_sensors, 1, true);
SentientDeviceDef dev_rfid_c(naming::DEV_RFID_C, naming::FRIENDLY_RFID_C, "rfid_reader", rfid_c_sensors, 1, true);
SentientDeviceDef dev_rfid_d(naming::DEV_RFID_D, naming::FRIENDLY_RFID_D, "rfid_reader", rfid_d_sensors, 1, true);
SentientDeviceDef dev_rfid_e(naming::DEV_RFID_E, naming::FRIENDLY_RFID_E, "rfid_reader", rfid_e_sensors, 1, true);

// Fuse sensors (input only)
const char *fuse_a_sensors[] = {naming::SENSOR_RESISTOR_VALUE};
const char *fuse_b_sensors[] = {naming::SENSOR_RESISTOR_VALUE};
const char *fuse_c_sensors[] = {naming::SENSOR_RESISTOR_VALUE};

SentientDeviceDef dev_fuse_a(naming::DEV_FUSE_A, naming::FRIENDLY_FUSE_A, "resistor_sensor", fuse_a_sensors, 1, true);
SentientDeviceDef dev_fuse_b(naming::DEV_FUSE_B, naming::FRIENDLY_FUSE_B, "resistor_sensor", fuse_b_sensors, 1, true);
SentientDeviceDef dev_fuse_c(naming::DEV_FUSE_C, naming::FRIENDLY_FUSE_C, "resistor_sensor", fuse_c_sensors, 1, true);

// Knife switch sensor (input only)
const char *knife_switch_sensors[] = {naming::SENSOR_SWITCH_STATE};
SentientDeviceDef dev_knife_switch(naming::DEV_KNIFE_SWITCH, naming::FRIENDLY_KNIFE_SWITCH, "switch", knife_switch_sensors, 1, true);

// Actuator (output device)
const char *actuator_commands[] = {
    naming::CMD_ACTUATOR_FORWARD,
    naming::CMD_ACTUATOR_REVERSE,
    naming::CMD_ACTUATOR_STOP};
SentientDeviceDef dev_actuator(naming::DEV_ACTUATOR, naming::FRIENDLY_ACTUATOR, "actuator", actuator_commands, 3);

// Maglocks (output devices)
const char *maglock_commands[] = {naming::CMD_DROP_PANEL};
SentientDeviceDef dev_maglock_b(naming::DEV_MAGLOCK_B, naming::FRIENDLY_MAGLOCK_B, "maglock", maglock_commands, 1);
SentientDeviceDef dev_maglock_c(naming::DEV_MAGLOCK_C, naming::FRIENDLY_MAGLOCK_C, "maglock", maglock_commands, 1);
SentientDeviceDef dev_maglock_d(naming::DEV_MAGLOCK_D, naming::FRIENDLY_MAGLOCK_D, "maglock", maglock_commands, 1);

// Metal gate (output device)
const char *metal_gate_commands[] = {naming::CMD_UNLOCK_GATE};
SentientDeviceDef dev_metal_gate(naming::DEV_METAL_GATE, naming::FRIENDLY_METAL_GATE, "maglock", metal_gate_commands, 1);

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
void monitor_rfid_readers();
void monitor_resistor_sensors();
void monitor_knife_switch();
void handle_rfid_reader(HardwareSerial &serial, char *tag_buffer, char *last_tag_buffer, int tir_pin, const char *device_id);
void clean_tag(char *tag);
int calculate_resistor_value(int pin);
int round_resistor_value(int raw_value);

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Fuse] Starting..."));

    // Initialize RFID readers
    RFID_A.begin(9600);
    RFID_B.begin(9600);
    RFID_C.begin(9600);
    RFID_D.begin(9600);
    RFID_E.begin(9600);

    // Initialize TIR sensor pins
    pinMode(PIN_TIR_A, INPUT_PULLDOWN);
    pinMode(PIN_TIR_B, INPUT_PULLDOWN);
    pinMode(PIN_TIR_C, INPUT_PULLDOWN);
    pinMode(PIN_TIR_D, INPUT_PULLDOWN);
    pinMode(PIN_TIR_E, INPUT_PULLDOWN);

    // Initialize knife switch
    pinMode(PIN_KNIFE_SWITCH, INPUT_PULLDOWN);

    // Initialize actuator pins
    pinMode(PIN_ACTUATOR_FWD, OUTPUT);
    pinMode(PIN_ACTUATOR_RWD, OUTPUT);
    digitalWrite(PIN_ACTUATOR_FWD, HIGH); // Default stop
    digitalWrite(PIN_ACTUATOR_RWD, LOW);

    // Initialize maglock pins (HIGH = locked)
    pinMode(PIN_MAGLOCK_B, OUTPUT);
    pinMode(PIN_MAGLOCK_C, OUTPUT);
    pinMode(PIN_MAGLOCK_D, OUTPUT);
    digitalWrite(PIN_MAGLOCK_B, HIGH);
    digitalWrite(PIN_MAGLOCK_C, HIGH);
    digitalWrite(PIN_MAGLOCK_D, HIGH);

    // Initialize metal gate (HIGH = locked)
    pinMode(PIN_METAL_GATE, OUTPUT);
    digitalWrite(PIN_METAL_GATE, HIGH);

    // Register all devices
    Serial.println(F("[INIT] Registering devices..."));
    deviceRegistry.addDevice(&dev_rfid_a);
    deviceRegistry.addDevice(&dev_rfid_b);
    deviceRegistry.addDevice(&dev_rfid_c);
    deviceRegistry.addDevice(&dev_rfid_d);
    deviceRegistry.addDevice(&dev_rfid_e);
    deviceRegistry.addDevice(&dev_fuse_a);
    deviceRegistry.addDevice(&dev_fuse_b);
    deviceRegistry.addDevice(&dev_fuse_c);
    deviceRegistry.addDevice(&dev_knife_switch);
    deviceRegistry.addDevice(&dev_actuator);
    deviceRegistry.addDevice(&dev_maglock_b);
    deviceRegistry.addDevice(&dev_maglock_c);
    deviceRegistry.addDevice(&dev_maglock_d);
    deviceRegistry.addDevice(&dev_metal_gate);
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

    Serial.println(F("[Fuse] Ready - awaiting Sentient commands"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    // 1. LISTEN for commands from Sentient
    sentient.loop();

    // 2. MONITOR sensors and publish changes
    monitor_rfid_readers();
    monitor_resistor_sensors();
    monitor_knife_switch();
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
        Serial.println(F("[CMD] ERROR: No device_id in payload"));
        return;
    }

    // Actuator commands
    if (strcmp(device_id, naming::DEV_ACTUATOR) == 0)
    {
        if (strcmp(command, naming::CMD_ACTUATOR_FORWARD) == 0)
        {
            digitalWrite(PIN_ACTUATOR_FWD, HIGH);
            digitalWrite(PIN_ACTUATOR_RWD, LOW);
            Serial.println(F("[ACTUATOR] Moving forward"));
        }
        else if (strcmp(command, naming::CMD_ACTUATOR_REVERSE) == 0)
        {
            digitalWrite(PIN_ACTUATOR_FWD, LOW);
            digitalWrite(PIN_ACTUATOR_RWD, HIGH);
            Serial.println(F("[ACTUATOR] Moving reverse"));
        }
        else if (strcmp(command, naming::CMD_ACTUATOR_STOP) == 0)
        {
            digitalWrite(PIN_ACTUATOR_FWD, LOW);
            digitalWrite(PIN_ACTUATOR_RWD, LOW);
            Serial.println(F("[ACTUATOR] Stopped"));
        }
    }
    // Maglock B drop command
    else if (strcmp(device_id, naming::DEV_MAGLOCK_B) == 0 && strcmp(command, naming::CMD_DROP_PANEL) == 0)
    {
        digitalWrite(PIN_MAGLOCK_B, LOW);
        Serial.println(F("[MAGLOCK_B] Panel dropped (unlocked)"));
    }
    // Maglock C drop command
    else if (strcmp(device_id, naming::DEV_MAGLOCK_C) == 0 && strcmp(command, naming::CMD_DROP_PANEL) == 0)
    {
        digitalWrite(PIN_MAGLOCK_C, LOW);
        Serial.println(F("[MAGLOCK_C] Panel dropped (unlocked)"));
    }
    // Maglock D drop command
    else if (strcmp(device_id, naming::DEV_MAGLOCK_D) == 0 && strcmp(command, naming::CMD_DROP_PANEL) == 0)
    {
        digitalWrite(PIN_MAGLOCK_D, LOW);
        Serial.println(F("[MAGLOCK_D] Panel dropped (unlocked)"));
    }
    // Metal gate unlock command
    else if (strcmp(device_id, naming::DEV_METAL_GATE) == 0 && strcmp(command, naming::CMD_UNLOCK_GATE) == 0)
    {
        digitalWrite(PIN_METAL_GATE, LOW);
        Serial.println(F("[METAL_GATE] Unlocked"));
    }
    else
    {
        return; // Unknown command, no ACK
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
// RFID MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void monitor_rfid_readers()
{
    handle_rfid_reader(RFID_A, tag_a, last_tag_a, PIN_TIR_A, naming::DEV_RFID_A);
    handle_rfid_reader(RFID_B, tag_b, last_tag_b, PIN_TIR_B, naming::DEV_RFID_B);
    handle_rfid_reader(RFID_C, tag_c, last_tag_c, PIN_TIR_C, naming::DEV_RFID_C);
    handle_rfid_reader(RFID_D, tag_d, last_tag_d, PIN_TIR_D, naming::DEV_RFID_D);
    handle_rfid_reader(RFID_E, tag_e, last_tag_e, PIN_TIR_E, naming::DEV_RFID_E);
}

void handle_rfid_reader(HardwareSerial &serial, char *tag_buffer, char *last_tag_buffer, int tir_pin, const char *device_id)
{
    // Read RFID data
    if (serial.available())
    {
        static char buffer[16];
        static int count = 0;
        static bool packet_started = false;

        char incoming_char = serial.read();

        if (incoming_char == 0x02)
        { // STX
            count = 0;
            packet_started = true;
        }
        else if (incoming_char == 0x03 && packet_started)
        { // ETX
            buffer[count] = '\0';
            strncpy(tag_buffer, buffer, TAG_LENGTH - 1);
            tag_buffer[TAG_LENGTH - 1] = '\0';
            clean_tag(tag_buffer);
            packet_started = false;
        }
        else if (packet_started && count < sizeof(buffer) - 1)
        {
            buffer[count++] = incoming_char;
        }
    }

    // Clear tag if TIR sensor indicates tag removed
    if (digitalRead(tir_pin) == LOW && strlen(tag_buffer) > 0)
    {
        tag_buffer[0] = '\0';
    }

    // Publish on change
    if (strcmp(tag_buffer, last_tag_buffer) != 0 && sentient.isConnected())
    {
        JsonDocument doc;
        doc["tag"] = (strlen(tag_buffer) > 0) ? tag_buffer : "EMPTY";
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_RFID_TAG, doc);

        Serial.print(F("[RFID] "));
        Serial.print(device_id);
        Serial.print(F(": "));
        Serial.println(strlen(tag_buffer) > 0 ? tag_buffer : "EMPTY");

        strncpy(last_tag_buffer, tag_buffer, TAG_LENGTH);
    }
}

void clean_tag(char *tag)
{
    int j = 0;
    for (int i = 0; tag[i] != '\0'; i++)
    {
        if (tag[i] != '\n' && tag[i] != '\r')
        {
            tag[j++] = tag[i];
        }
    }
    tag[j] = '\0';
}

// ══════════════════════════════════════════════════════════════════════════════
// RESISTOR SENSOR MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void monitor_resistor_sensors()
{
    static unsigned long last_publish = 0;
    unsigned long now = millis();

    // Calculate current resistor values
    resistor_a = calculate_resistor_value(PIN_FUSE_A);
    resistor_b = calculate_resistor_value(PIN_FUSE_B);
    resistor_c = calculate_resistor_value(PIN_FUSE_C);

    // Check for changes
    bool changed = (resistor_a != last_resistor_a ||
                    resistor_b != last_resistor_b ||
                    resistor_c != last_resistor_c);

    // Publish on change or every 5 seconds
    if ((changed || (now - last_publish >= 5000)) && sentient.isConnected())
    {
        last_publish = now;

        JsonDocument doc;

        // Publish fuse A
        doc["value"] = resistor_a;
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_RESISTOR_VALUE, doc);
        doc.clear();

        // Publish fuse B
        doc["value"] = resistor_b;
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_RESISTOR_VALUE, doc);
        doc.clear();

        // Publish fuse C
        doc["value"] = resistor_c;
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_RESISTOR_VALUE, doc);
        doc.clear();

        if (changed)
        {
            Serial.print(F("[FUSES] A: "));
            Serial.print(resistor_a);
            Serial.print(F(", B: "));
            Serial.print(resistor_b);
            Serial.print(F(", C: "));
            Serial.println(resistor_c);
        }

        last_resistor_a = resistor_a;
        last_resistor_b = resistor_b;
        last_resistor_c = resistor_c;
    }
}

int calculate_resistor_value(int pin)
{
    int raw_value = analogRead(pin);

    const int Vin_scaled = 3300;    // 3.3V * 1000 for integer math
    const int cont_resistor = 1000; // 1k ohm
    const int threshold = 10;
    const int max_resistor = 1000;

    int Vout_scaled = (raw_value * Vin_scaled) / 1024;

    int resistor = (Vout_scaled >= threshold) ? (cont_resistor * (Vin_scaled - Vout_scaled)) / Vout_scaled : 0;

    // Apply bucketing/rounding
    return round_resistor_value((resistor > max_resistor) ? 0 : resistor);
}

int round_resistor_value(int raw_value)
{
    if (raw_value < 50)
        return 0;

    // Round to nearest 100 (0, 100, 200, 300)
    int rounded = ((raw_value + 50) / 100) * 100;

    return (rounded > 300) ? 300 : rounded;
}

// ══════════════════════════════════════════════════════════════════════════════
// KNIFE SWITCH MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void monitor_knife_switch()
{
    static unsigned long last_publish = 0;
    unsigned long now = millis();

    knife_switch_state = (digitalRead(PIN_KNIFE_SWITCH) == HIGH);

    bool changed = (knife_switch_state != last_knife_switch_state);

    // Publish on change or every 5 seconds
    if ((changed || (now - last_publish >= 5000)) && sentient.isConnected())
    {
        last_publish = now;

        JsonDocument doc;
        doc["state"] = knife_switch_state ? "ON" : "OFF";
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_SWITCH_STATE, doc);

        if (changed)
        {
            Serial.print(F("[KNIFE_SWITCH] "));
            Serial.println(knife_switch_state ? "ON" : "OFF");
        }

        last_knife_switch_state = knife_switch_state;
    }
}
