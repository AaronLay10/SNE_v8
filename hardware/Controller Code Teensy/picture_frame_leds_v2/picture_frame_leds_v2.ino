// ══════════════════════════════════════════════════════════════════════════════
// Picture Frame LEDs Controller v2.1.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "controller_naming.h"

// ══════════════════════════════════════════════════════════════════════════════
// HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

const int PIN_POWER_LED = 13;
const int NUM_STRIPS = 32;
const int NUM_LEDS_PER_STRIP = 12;
const int STRIPS_PER_TV = 8;

// Pin mappings for 4 TVs (8 strips each)
const int TV_PINS[NUM_STRIPS] = {
    // Vincent (0-7): Pins 0-7
    0, 1, 2, 3, 4, 5, 6, 7,
    // Edith (8-15): Pins 16-23
    16, 17, 18, 19, 20, 21, 22, 23,
    // Maks (16-23): Pins 25-32
    25, 26, 27, 28, 29, 30, 31, 32,
    // Oliver (24-31): Pins 14,15,8,9,10,11,12,24
    14, 15, 8, 9, 10, 11, 12, 24};

// Default TV colors (mustard yellow, dark lavender, forest green, greyish blue)
struct TVColor
{
    uint8_t r, g, b;
};

TVColor default_colors[4] = {
    {218, 190, 0},  // Vincent - Mustard Yellow
    {70, 0, 150},   // Edith - Dark Lavender
    {20, 91, 0},    // Maks - Forest Green
    {10, 10, 100}}; // Oliver - Greyish Blue

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

Adafruit_NeoPixel strips[NUM_STRIPS];
bool tv_power[4] = {true, true, true, true};
uint8_t tv_brightness[4] = {10, 10, 10, 10};
TVColor current_colors[4];

// ══════════════════════════════════════════════════════════════════════════════
// DEVICE REGISTRY
// ══════════════════════════════════════════════════════════════════════════════

const char *tv_commands[] = {naming::CMD_POWER_ON, naming::CMD_POWER_OFF, naming::CMD_SET_COLOR, naming::CMD_SET_BRIGHTNESS, naming::CMD_FLICKER};

SentientDeviceDef dev_tv_vincent(naming::DEV_TV_VINCENT, "Vincent TV LEDs", "led_strip", tv_commands, 5);
SentientDeviceDef dev_tv_edith(naming::DEV_TV_EDITH, "Edith TV LEDs", "led_strip", tv_commands, 5);
SentientDeviceDef dev_tv_maks(naming::DEV_TV_MAKS, "Maks TV LEDs", "led_strip", tv_commands, 5);
SentientDeviceDef dev_tv_oliver(naming::DEV_TV_OLIVER, "Oliver TV LEDs", "led_strip", tv_commands, 5);
SentientDeviceDef dev_all_tvs(naming::DEV_ALL_TVS, "All TVs", "led_strip", tv_commands, 5);

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
void set_tv_color(int tv_index, uint8_t r, uint8_t g, uint8_t b);
void set_tv_power(int tv_index, bool on);
void set_tv_brightness(int tv_index, uint8_t brightness);

// ══════════════════════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    // Initialize all NeoPixel strips
    for (int i = 0; i < NUM_STRIPS; i++)
    {
        strips[i] = Adafruit_NeoPixel(NUM_LEDS_PER_STRIP, TV_PINS[i], NEO_GRB + NEO_KHZ800);
        strips[i].begin();
        strips[i].setBrightness(10);
        strips[i].clear();
        strips[i].show();
    }

    // Set default colors
    for (int i = 0; i < 4; i++)
    {
        current_colors[i] = default_colors[i];
    }

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[PictureFrameLEDs] Starting..."));

    deviceRegistry.addDevice(&dev_tv_vincent);
    deviceRegistry.addDevice(&dev_tv_edith);
    deviceRegistry.addDevice(&dev_tv_maks);
    deviceRegistry.addDevice(&dev_tv_oliver);
    deviceRegistry.addDevice(&dev_all_tvs);
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

    // Initialize with default colors
    for (int i = 0; i < 4; i++)
    {
        set_tv_color(i, current_colors[i].r, current_colors[i].g, current_colors[i].b);
        set_tv_power(i, true);
    }

    Serial.println(F("[PictureFrameLEDs] Ready"));
}

// ══════════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    sentient.loop();
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

    // Determine which TV(s) to control
    int tv_start = -1, tv_end = -1;

    if (strcmp(device_id, naming::DEV_TV_VINCENT) == 0)
    {
        tv_start = tv_end = 0;
    }
    else if (strcmp(device_id, naming::DEV_TV_EDITH) == 0)
    {
        tv_start = tv_end = 1;
    }
    else if (strcmp(device_id, naming::DEV_TV_MAKS) == 0)
    {
        tv_start = tv_end = 2;
    }
    else if (strcmp(device_id, naming::DEV_TV_OLIVER) == 0)
    {
        tv_start = tv_end = 3;
    }
    else if (strcmp(device_id, naming::DEV_ALL_TVS) == 0)
    {
        tv_start = 0;
        tv_end = 3;
    }

    if (tv_start == -1)
        return;

    // Execute command on selected TV(s)
    for (int tv = tv_start; tv <= tv_end; tv++)
    {
        if (strcmp(command, naming::CMD_POWER_ON) == 0)
        {
            set_tv_power(tv, true);
            Serial.print(F("[CMD] TV "));
            Serial.print(tv);
            Serial.println(F(" ON"));
        }
        else if (strcmp(command, naming::CMD_POWER_OFF) == 0)
        {
            set_tv_power(tv, false);
            Serial.print(F("[CMD] TV "));
            Serial.print(tv);
            Serial.println(F(" OFF"));
        }
        else if (strcmp(command, naming::CMD_SET_COLOR) == 0)
        {
            if (payload["r"].is<int>() && payload["g"].is<int>() && payload["b"].is<int>())
            {
                uint8_t r = payload["r"];
                uint8_t g = payload["g"];
                uint8_t b = payload["b"];
                set_tv_color(tv, r, g, b);
                Serial.print(F("[CMD] TV "));
                Serial.print(tv);
                Serial.print(F(" color: "));
                Serial.print(r);
                Serial.print(F(","));
                Serial.print(g);
                Serial.print(F(","));
                Serial.println(b);
            }
        }
        else if (strcmp(command, naming::CMD_SET_BRIGHTNESS) == 0 && payload["brightness"].is<int>())
        {
            uint8_t brightness = payload["brightness"];
            set_tv_brightness(tv, brightness);
            Serial.print(F("[CMD] TV "));
            Serial.print(tv);
            Serial.print(F(" brightness: "));
            Serial.println(brightness);
        }
        else if (strcmp(command, naming::CMD_FLICKER) == 0)
        {
            // Simple flicker effect
            for (int i = 0; i < 5; i++)
            {
                set_tv_power(tv, false);
                delay(100);
                set_tv_power(tv, true);
                delay(100);
            }
            Serial.print(F("[CMD] TV "));
            Serial.print(tv);
            Serial.println(F(" flickered"));
        }
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
// LED CONTROL HELPERS
// ══════════════════════════════════════════════════════════════════════════════

void set_tv_color(int tv_index, uint8_t r, uint8_t g, uint8_t b)
{
    current_colors[tv_index] = {r, g, b};

    if (tv_power[tv_index])
    {
        int strip_start = tv_index * STRIPS_PER_TV;
        int strip_end = strip_start + STRIPS_PER_TV;

        for (int strip = strip_start; strip < strip_end; strip++)
        {
            uint32_t color = strips[strip].Color(r, g, b);
            for (int led = 0; led < NUM_LEDS_PER_STRIP; led++)
            {
                strips[strip].setPixelColor(led, color);
            }
            strips[strip].show();
        }
    }
}

void set_tv_power(int tv_index, bool on)
{
    tv_power[tv_index] = on;

    int strip_start = tv_index * STRIPS_PER_TV;
    int strip_end = strip_start + STRIPS_PER_TV;

    for (int strip = strip_start; strip < strip_end; strip++)
    {
        if (on)
        {
            uint32_t color = strips[strip].Color(current_colors[tv_index].r,
                                                 current_colors[tv_index].g,
                                                 current_colors[tv_index].b);
            for (int led = 0; led < NUM_LEDS_PER_STRIP; led++)
            {
                strips[strip].setPixelColor(led, color);
            }
        }
        else
        {
            strips[strip].clear();
        }
        strips[strip].show();
    }
}

void set_tv_brightness(int tv_index, uint8_t brightness)
{
    tv_brightness[tv_index] = brightness;

    int strip_start = tv_index * STRIPS_PER_TV;
    int strip_end = strip_start + STRIPS_PER_TV;

    for (int strip = strip_start; strip < strip_end; strip++)
    {
        strips[strip].setBrightness(brightness);
        strips[strip].show();
    }
}
