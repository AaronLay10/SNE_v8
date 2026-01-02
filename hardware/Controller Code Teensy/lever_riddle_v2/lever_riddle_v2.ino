// ══════════════════════════════════════════════════════════════════════════════
// Lever Riddle Controller v2.1.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN // IRremote library compatibility
#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <IRremote.hpp>
#include <FastLED.h>
#include "controller_naming.h"

// ══════════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const int PIN_POWER_LED = 13;
const int PIN_IR_RECEIVE = 25;
const int PIN_MAGLOCK = 26;
const int PIN_HALL_A = 5;
const int PIN_HALL_B = 6;
const int PIN_HALL_C = 7;
const int PIN_HALL_D = 8;
const int PIN_LED_STRIP = 1;
const int PIN_LED_LEVER = 12;
const int PIN_PHOTOCELL = A10;
const int PIN_CUBE_BUTTON = 32;
const int PIN_COB_LIGHT = 30;

const int NUM_LEDS = 9;
const int NUM_LEVER_LEDS = 10;
const int BRIGHTNESS = 255;

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

CRGB leds[NUM_LEDS];
CRGB lever_leds[NUM_LEVER_LEDS];

bool hall_a = false, hall_b = false, hall_c = false, hall_d = false;
bool last_hall_a = false, last_hall_b = false, last_hall_c = false, last_hall_d = false;

int photocell_value = 0;
int last_photocell_value = -1;

bool cube_button = false;
bool last_cube_button = false;

bool ir_enabled = true;
char last_ir_code[64] = "";

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

// Hall effect sensors
const char *hall_sensors[] = {naming::SENSOR_MAGNET};
SentientDeviceDef dev_hall_a(naming::DEV_HALL_A, "Hall Sensor A", "hall_effect", hall_sensors, 1, true);
SentientDeviceDef dev_hall_b(naming::DEV_HALL_B, "Hall Sensor B", "hall_effect", hall_sensors, 1, true);
SentientDeviceDef dev_hall_c(naming::DEV_HALL_C, "Hall Sensor C", "hall_effect", hall_sensors, 1, true);
SentientDeviceDef dev_hall_d(naming::DEV_HALL_D, "Hall Sensor D", "hall_effect", hall_sensors, 1, true);

// Photocell
const char *photocell_sensors[] = {naming::SENSOR_LEVER_POSITION};
SentientDeviceDef dev_photocell(naming::DEV_PHOTOCELL, "Photocell Lever", "photocell", photocell_sensors, 1, true);

// Cube button
const char *button_sensors[] = {naming::SENSOR_BUTTON_STATE};
SentientDeviceDef dev_cube_button(naming::DEV_CUBE_BUTTON, "Cube Button", "button", button_sensors, 1, true);

// IR receiver
const char *ir_sensors[] = {naming::SENSOR_IR_CODE};
const char *ir_commands[] = {naming::CMD_IR_ENABLE, naming::CMD_IR_DISABLE};
SentientDeviceDef dev_ir_receiver(naming::DEV_IR_RECEIVER, "IR Receiver", "ir_receiver", ir_commands, 2, ir_sensors, 1);

// Maglock
const char *maglock_commands[] = {naming::CMD_LOCK, naming::CMD_UNLOCK};
SentientDeviceDef dev_maglock(naming::DEV_MAGLOCK, "Maglock", "maglock", maglock_commands, 2);

// LED strips
const char *led_commands[] = {naming::CMD_SET_COLOR};
SentientDeviceDef dev_led_strip(naming::DEV_LED_STRIP, "Main LED Strip", "led_strip", led_commands, 1);
SentientDeviceDef dev_led_lever(naming::DEV_LED_LEVER, "Lever LED Strip", "led_strip", led_commands, 1);

// COB light
const char *cob_commands[] = {naming::CMD_LIGHT_ON, naming::CMD_LIGHT_OFF};
SentientDeviceDef dev_cob_light(naming::DEV_COB_LIGHT, "COB Light", "light", cob_commands, 2);

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
void handle_ir_signal();
void set_led_color(CRGB *leds, int count, const char *color);

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    pinMode(PIN_MAGLOCK, OUTPUT);
    digitalWrite(PIN_MAGLOCK, HIGH); // Locked

    pinMode(PIN_COB_LIGHT, OUTPUT);
    digitalWrite(PIN_COB_LIGHT, HIGH);

    pinMode(PIN_HALL_A, INPUT_PULLUP);
    pinMode(PIN_HALL_B, INPUT_PULLUP);
    pinMode(PIN_HALL_C, INPUT_PULLUP);
    pinMode(PIN_HALL_D, INPUT_PULLUP);
    pinMode(PIN_PHOTOCELL, INPUT);
    pinMode(PIN_CUBE_BUTTON, INPUT_PULLUP);

    // Initialize FastLED
    FastLED.addLeds<WS2812B, PIN_LED_STRIP, RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2812B, PIN_LED_LEVER, RGB>(lever_leds, NUM_LEVER_LEDS);
    FastLED.setBrightness(BRIGHTNESS);
    set_led_color(leds, NUM_LEDS, "red");
    set_led_color(lever_leds, NUM_LEVER_LEDS, "white");

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[LeverRiddle] Starting..."));

    // Register devices
    deviceRegistry.addDevice(&dev_hall_a);
    deviceRegistry.addDevice(&dev_hall_b);
    deviceRegistry.addDevice(&dev_hall_c);
    deviceRegistry.addDevice(&dev_hall_d);
    deviceRegistry.addDevice(&dev_photocell);
    deviceRegistry.addDevice(&dev_cube_button);
    deviceRegistry.addDevice(&dev_ir_receiver);
    deviceRegistry.addDevice(&dev_maglock);
    deviceRegistry.addDevice(&dev_led_strip);
    deviceRegistry.addDevice(&dev_led_lever);
    deviceRegistry.addDevice(&dev_cob_light);
    deviceRegistry.printSummary();

    manifest.set_controller_info(naming::CONTROLLER_ID, naming::CONTROLLER_FRIENDLY_NAME,
                                 firmware::VERSION, naming::ROOM_ID, naming::CONTROLLER_ID);
    deviceRegistry.buildManifest(manifest);

    // Initialize IR receiver after serial
    IrReceiver.begin(PIN_IR_RECEIVE, DISABLE_LED_FEEDBACK);

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

    Serial.println(F("[LeverRiddle] Ready"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    sentient.loop();
    monitor_sensors();

    if (ir_enabled && IrReceiver.decode())
    {
        handle_ir_signal();
        IrReceiver.resume();
    }

    delay(50);
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    if (!payload["device_id"].is<const char *>())
        return;

    const char *device_id = payload["device_id"];

    // Maglock commands
    if (strcmp(device_id, naming::DEV_MAGLOCK) == 0)
    {
        if (strcmp(command, naming::CMD_LOCK) == 0)
        {
            digitalWrite(PIN_MAGLOCK, HIGH);
            Serial.println(F("[CMD] Maglock LOCKED"));
        }
        else if (strcmp(command, naming::CMD_UNLOCK) == 0)
        {
            digitalWrite(PIN_MAGLOCK, LOW);
            Serial.println(F("[CMD] Maglock UNLOCKED"));
        }
    }
    // LED strip commands
    else if (strcmp(device_id, naming::DEV_LED_STRIP) == 0 && strcmp(command, naming::CMD_SET_COLOR) == 0)
    {
        set_led_color(leds, NUM_LEDS, payload["color"]);
    }
    else if (strcmp(device_id, naming::DEV_LED_LEVER) == 0 && strcmp(command, naming::CMD_SET_COLOR) == 0)
    {
        set_led_color(lever_leds, NUM_LEVER_LEDS, payload["color"]);
    }
    // COB light commands
    else if (strcmp(device_id, naming::DEV_COB_LIGHT) == 0)
    {
        if (strcmp(command, naming::CMD_LIGHT_ON) == 0)
        {
            digitalWrite(PIN_COB_LIGHT, HIGH);
            Serial.println(F("[CMD] COB Light ON"));
        }
        else if (strcmp(command, naming::CMD_LIGHT_OFF) == 0)
        {
            digitalWrite(PIN_COB_LIGHT, LOW);
            Serial.println(F("[CMD] COB Light OFF"));
        }
    }
    // IR receiver commands
    else if (strcmp(device_id, naming::DEV_IR_RECEIVER) == 0)
    {
        if (strcmp(command, naming::CMD_IR_ENABLE) == 0)
        {
            ir_enabled = true;
            Serial.println(F("[CMD] IR Receiver ENABLED"));
        }
        else if (strcmp(command, naming::CMD_IR_DISABLE) == 0)
        {
            ir_enabled = false;
            Serial.println(F("[CMD] IR Receiver DISABLED"));
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

    // Hall effect sensors
    hall_a = !digitalRead(PIN_HALL_A);
    hall_b = !digitalRead(PIN_HALL_B);
    hall_c = !digitalRead(PIN_HALL_C);
    hall_d = !digitalRead(PIN_HALL_D);

    if (hall_a != last_hall_a)
    {
        JsonDocument doc;
        doc["magnet_detected"] = hall_a;
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_HALL_A, doc);
        last_hall_a = hall_a;
    }
    if (hall_b != last_hall_b)
    {
        JsonDocument doc;
        doc["magnet_detected"] = hall_b;
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_HALL_B, doc);
        last_hall_b = hall_b;
    }
    if (hall_c != last_hall_c)
    {
        JsonDocument doc;
        doc["magnet_detected"] = hall_c;
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_HALL_C, doc);
        last_hall_c = hall_c;
    }
    if (hall_d != last_hall_d)
    {
        JsonDocument doc;
        doc["magnet_detected"] = hall_d;
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_HALL_D, doc);
        last_hall_d = hall_d;
    }

    // Photocell
    photocell_value = analogRead(PIN_PHOTOCELL);
    if (abs(photocell_value - last_photocell_value) > 50)
    {
        JsonDocument doc;
        doc["lever_position"] = (photocell_value > 500) ? "OPEN" : "CLOSED";
        doc["raw_value"] = photocell_value;
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_PHOTOCELL, doc);
        last_photocell_value = photocell_value;
    }

    // Cube button
    cube_button = !digitalRead(PIN_CUBE_BUTTON);
    if (cube_button != last_cube_button)
    {
        JsonDocument doc;
        doc["button_state"] = cube_button ? "PRESSED" : "RELEASED";
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_CUBE_BUTTON, doc);
        last_cube_button = cube_button;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// IR SIGNAL HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_ir_signal()
{
    static uint32_t last_raw_data = 0;
    static unsigned long last_timestamp = 0;

    bool is_duplicate = (IrReceiver.decodedIRData.decodedRawData == last_raw_data &&
                         (millis() - last_timestamp) < 500);
    bool is_weak = (IrReceiver.decodedIRData.protocol == UNKNOWN ||
                    IrReceiver.decodedIRData.protocol == 2);

    if (!is_duplicate && !is_weak && sentient.isConnected())
    {
        JsonDocument doc;
        doc["ir_code"] = IrReceiver.decodedIRData.command;
        doc["address"] = IrReceiver.decodedIRData.address;
        doc["protocol"] = IrReceiver.decodedIRData.protocol;
        doc["raw"] = (unsigned long)IrReceiver.decodedIRData.decodedRawData;
        sentient.publishJson(naming::CAT_SENSORS, naming::DEV_IR_RECEIVER, doc);

        Serial.print(F("[IR] Code: 0x"));
        Serial.print(IrReceiver.decodedIRData.command, HEX);
        Serial.print(F(" Address: 0x"));
        Serial.println(IrReceiver.decodedIRData.address, HEX);
    }

    last_raw_data = IrReceiver.decodedIRData.decodedRawData;
    last_timestamp = millis();
}

// ══════════════════════════════════════════════════════════════════════════════
// LED HELPER
// ══════════════════════════════════════════════════════════════════════════════

void set_led_color(CRGB *led_array, int count, const char *color)
{
    CRGB rgb;
    if (strcmp(color, "red") == 0)
        rgb = CRGB::Red;
    else if (strcmp(color, "green") == 0)
        rgb = CRGB::Green;
    else if (strcmp(color, "blue") == 0)
        rgb = CRGB::Blue;
    else if (strcmp(color, "yellow") == 0)
        rgb = CRGB::Yellow;
    else if (strcmp(color, "purple") == 0)
        rgb = CRGB::Purple;
    else if (strcmp(color, "white") == 0)
        rgb = CRGB::White;
    else if (strcmp(color, "off") == 0)
        rgb = CRGB::Black;
    else
        rgb = CRGB::White;

    fill_solid(led_array, count, rgb);
    FastLED.show();
}
