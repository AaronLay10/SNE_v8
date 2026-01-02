// ══════════════════════════════════════════════════════════════════════════════
// Riddle Puzzle Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <AccelStepper.h>
#include "controller_naming.h"

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 1: HARDWARE CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

// Pin Definitions
const int PIN_POWER_LED = 13;

// LED Strip
const int PIN_LED_STRIP = 29;
const int NUM_LEDS = 50;

// Knob Pins
const int PIN_KNOB_A2 = 20;
const int PIN_KNOB_A3 = 19;
const int PIN_B1 = 2;
const int PIN_B2 = 4;
const int PIN_B3 = 5;
const int PIN_B4 = 3;
const int PIN_C1 = 16;
const int PIN_C2 = 17;
const int PIN_C3 = 18;
const int PIN_C4 = 15;
const int PIN_D1 = 40;
const int PIN_D2 = 39;
const int PIN_D3 = 37;
const int PIN_D4 = 38;
const int PIN_E1 = 1;
const int PIN_E2 = 7;
const int PIN_E3 = 0;
const int PIN_E4 = 6;
const int PIN_F1 = 35;
const int PIN_F2 = 33;
const int PIN_F3 = 34;
const int PIN_F4 = 36;
const int PIN_G1 = 22;
const int PIN_G4 = 23;

// Button Pins
const int PIN_BUTTON_1 = 31;
const int PIN_BUTTON_2 = 30;
const int PIN_BUTTON_3 = 32;

// Sensor Pins
const int PIN_ENDSTOP_UP_R = 28;
const int PIN_ENDSTOP_UP_L = 8;
const int PIN_ENDSTOP_DN_R = 41;
const int PIN_ENDSTOP_DN_L = 21;

// Actuator Pins
const int PIN_MAGLOCK = 42;

// Stepper motor pins
const int PIN_STEPPER1_1 = 24;
const int PIN_STEPPER1_2 = 25;
const int PIN_STEPPER1_3 = 26;
const int PIN_STEPPER1_4 = 27;
const int PIN_STEPPER2_1 = 9;
const int PIN_STEPPER2_2 = 10;
const int PIN_STEPPER2_3 = 11;
const int PIN_STEPPER2_4 = 12;

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

enum PuzzleState
{
    STATE_STARTUP = 0,
    STATE_KNOBS = 1,
    STATE_MOTORS = 2,
    STATE_LEVER = 3,
    STATE_GUNS = 4,
    STATE_FINISHED = 5
};

PuzzleState current_state = STATE_STARTUP;
int active_clue = 0;

// Door motor control
int door_direction = 0; // 0=stopped, 1=up, -1=down
bool motors_running = false;
bool motor_speed_set = false;
int door_location = 0; // 1=closed, 2=open, 3=between

// LED control
int led_letters[NUM_LEDS];

// Knob LED mappings (LED indices for each knob position)
const int knob_pin_a2[] = {42, 41, 40, 39, 38, 37};
const int knob_pin_a3[] = {44, 45, 46, 47, 48, 49};
const int knob_pin_b1[] = {30, 29, 28, 27, 26};
const int knob_pin_b2[] = {32, 33, 34, 35, 36};
const int knob_pin_b3[] = {42};
const int knob_pin_b4[] = {44};
const int knob_pin_c1[] = {30, 45};
const int knob_pin_c2[] = {22, 23, 24, 25};
const int knob_pin_c3[] = {20, 19, 18, 17};
const int knob_pin_c4[] = {32, 41};
const int knob_pin_d1[] = {22, 29, 46};
const int knob_pin_d2[] = {12, 11, 10};
const int knob_pin_d3[] = {14, 15, 16};
const int knob_pin_d4[] = {20, 33, 40};
const int knob_pin_e1[] = {12, 23, 28, 47};
const int knob_pin_e2[] = {8, 9};
const int knob_pin_e3[] = {6, 5};
const int knob_pin_e4[] = {14, 19, 34, 39};
const int knob_pin_f1[] = {8, 11, 24, 27, 48};
const int knob_pin_f3[] = {2};
const int knob_pin_f4[] = {4};
const int knob_pin_f2[] = {6, 15, 18, 35, 38};
const int knob_pin_g1[] = {2, 9, 10, 25, 26, 49};
const int knob_pin_g4[] = {4, 5, 16, 17, 36, 37};

// Hardware objects
Adafruit_NeoPixel strip(NUM_LEDS, PIN_LED_STRIP, NEO_GRB + NEO_KHZ800);
AccelStepper stepper_one(AccelStepper::FULL4WIRE, PIN_STEPPER1_1, PIN_STEPPER1_2, PIN_STEPPER1_3, PIN_STEPPER1_4);
AccelStepper stepper_two(AccelStepper::FULL4WIRE, PIN_STEPPER2_1, PIN_STEPPER2_2, PIN_STEPPER2_3, PIN_STEPPER2_4);

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 2: DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!)
// ══════════════════════════════════════════════════════════════════════════════

// Door commands
const char *door_commands[] = {
    naming::CMD_DOOR_LIFT,
    naming::CMD_DOOR_LOWER,
    naming::CMD_DOOR_STOP};

// Maglock commands
const char *maglock_commands[] = {
    naming::CMD_MAGLOCK_LOCK,
    naming::CMD_MAGLOCK_UNLOCK};

// LED strip commands
const char *led_commands[] = {
    naming::CMD_LEDS_ON,
    naming::CMD_LEDS_OFF,
    naming::CMD_LEDS_SET_BRIGHTNESS};

// Controller state command
const char *controller_commands[] = {
    naming::CMD_SET_STATE,
    naming::CMD_RESET};

// Door sensors
const char *door_sensors[] = {
    naming::SENSOR_DOOR_POSITION,
    naming::SENSOR_ENDSTOP_UP_R,
    naming::SENSOR_ENDSTOP_UP_L,
    naming::SENSOR_ENDSTOP_DN_R,
    naming::SENSOR_ENDSTOP_DN_L};

// Knob sensors
const char *knob_sensors[] = {
    naming::SENSOR_KNOB_STATE,
    naming::SENSOR_ACTIVE_CLUE};

// Button sensors
const char *button_sensors[] = {
    naming::SENSOR_BUTTON_1,
    naming::SENSOR_BUTTON_2,
    naming::SENSOR_BUTTON_3};

// Device definitions
SentientDeviceDef dev_door(
    naming::DEV_DOOR,
    naming::FRIENDLY_DOOR,
    "actuator",
    door_commands, 3);

SentientDeviceDef dev_maglock(
    naming::DEV_MAGLOCK,
    naming::FRIENDLY_MAGLOCK,
    "relay",
    maglock_commands, 2);

SentientDeviceDef dev_leds(
    naming::DEV_LED_STRIP,
    naming::FRIENDLY_LED_STRIP,
    "led_strip",
    led_commands, 3);

SentientDeviceDef dev_controller(
    naming::CONTROLLER_ID,
    naming::CONTROLLER_FRIENDLY_NAME,
    "controller",
    controller_commands, 2);

SentientDeviceDef dev_door_sensors(
    naming::DEV_DOOR,
    naming::FRIENDLY_DOOR,
    "sensor",
    door_sensors, 5, true); // input only

SentientDeviceDef dev_knobs(
    naming::DEV_KNOBS,
    naming::FRIENDLY_KNOBS,
    "sensor",
    knob_sensors, 2, true); // input only

SentientDeviceDef dev_buttons(
    naming::DEV_BUTTONS,
    naming::FRIENDLY_BUTTONS,
    "sensor",
    button_sensors, 3, true); // input only

// Create the device registry
SentientDeviceRegistry deviceRegistry;

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 3: SENTIENT MQTT INITIALIZATION
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
void read_sensors();
void update_leds();
void handle_knobs();
void button_riddle();
void check_motors();
void run_motors();

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 4: SETUP
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
    pinMode(PIN_POWER_LED, OUTPUT);
    digitalWrite(PIN_POWER_LED, HIGH);

    Serial.begin(115200);
    delay(2000);
    Serial.println(F("[Riddle] Starting..."));

    // Setup input pins
    pinMode(PIN_KNOB_A2, INPUT_PULLUP);
    pinMode(PIN_KNOB_A3, INPUT_PULLUP);
    pinMode(PIN_B1, INPUT_PULLUP);
    pinMode(PIN_B2, INPUT_PULLUP);
    pinMode(PIN_B3, INPUT_PULLUP);
    pinMode(PIN_B4, INPUT_PULLUP);
    pinMode(PIN_C1, INPUT_PULLUP);
    pinMode(PIN_C2, INPUT_PULLUP);
    pinMode(PIN_C3, INPUT_PULLUP);
    pinMode(PIN_C4, INPUT_PULLUP);
    pinMode(PIN_D1, INPUT_PULLUP);
    pinMode(PIN_D2, INPUT_PULLUP);
    pinMode(PIN_D3, INPUT_PULLUP);
    pinMode(PIN_D4, INPUT_PULLUP);
    pinMode(PIN_E1, INPUT_PULLUP);
    pinMode(PIN_E2, INPUT_PULLUP);
    pinMode(PIN_E3, INPUT_PULLUP);
    pinMode(PIN_E4, INPUT_PULLUP);
    pinMode(PIN_F1, INPUT_PULLUP);
    pinMode(PIN_F2, INPUT_PULLUP);
    pinMode(PIN_F3, INPUT_PULLUP);
    pinMode(PIN_F4, INPUT_PULLUP);
    pinMode(PIN_G1, INPUT_PULLUP);
    pinMode(PIN_G4, INPUT_PULLUP);

    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
    pinMode(PIN_BUTTON_3, INPUT_PULLUP);

    pinMode(PIN_ENDSTOP_UP_R, INPUT_PULLUP);
    pinMode(PIN_ENDSTOP_UP_L, INPUT_PULLUP);
    pinMode(PIN_ENDSTOP_DN_R, INPUT_PULLUP);
    pinMode(PIN_ENDSTOP_DN_L, INPUT_PULLUP);

    // Setup outputs
    pinMode(PIN_MAGLOCK, OUTPUT);
    digitalWrite(PIN_MAGLOCK, HIGH); // Locked by default

    // Initialize stepper motors
    stepper_one.setMaxSpeed(8000);
    stepper_one.setAcceleration(800);
    stepper_two.setMaxSpeed(8000);
    stepper_two.setAcceleration(800);

    // Initialize LED strip
    strip.begin();
    strip.setBrightness(50);
    strip.show();

    // Register all devices
    Serial.println(F("[INIT] Registering devices..."));
    deviceRegistry.addDevice(&dev_door);
    deviceRegistry.addDevice(&dev_maglock);
    deviceRegistry.addDevice(&dev_leds);
    deviceRegistry.addDevice(&dev_controller);
    deviceRegistry.addDevice(&dev_door_sensors);
    deviceRegistry.addDevice(&dev_knobs);
    deviceRegistry.addDevice(&dev_buttons);
    deviceRegistry.printSummary(); // Build capability manifest
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

    Serial.println(F("[Riddle] Ready - awaiting Sentient commands"));
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 5: MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
    // 1. LISTEN for commands from Sentient
    sentient.loop();

    // 2. DETECT sensor changes and publish if needed
    read_sensors();

    // 3. EXECUTE state-specific operations
    switch (current_state)
    {
    case STATE_STARTUP:
        // Standby mode
        break;

    case STATE_KNOBS:
        button_riddle();
        handle_knobs();
        update_leds();
        break;

    case STATE_MOTORS:
        check_motors();
        run_motors();
        break;

    case STATE_LEVER:
        // Lever state logic
        break;

    case STATE_GUNS:
        // Gun puzzle logic
        break;

    case STATE_FINISHED:
        // Puzzle completed
        break;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 6: COMMAND HANDLER
// ══════════════════════════════════════════════════════════════════════════════

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    Serial.print(F("[CMD] Received: "));
    Serial.println(command);

    // Parse device_id from payload
    const char *device_id = payload["device_id"];
    if (!device_id)
    {
        Serial.println(F("[CMD] ERROR: No device_id in payload"));
        return;
    }

    // Route to appropriate device handler
    if (strcmp(device_id, naming::DEV_DOOR) == 0)
    {
        // Door lift commands
        if (strcmp(command, naming::CMD_DOOR_LIFT) == 0)
        {
            if (!motors_running)
            {
                door_direction = 1;
                current_state = STATE_MOTORS;
                Serial.println(F("[DOOR] Command: LIFT"));
            }
        }
        else if (strcmp(command, naming::CMD_DOOR_LOWER) == 0)
        {
            if (!motors_running)
            {
                door_direction = -1;
                current_state = STATE_MOTORS;
                Serial.println(F("[DOOR] Command: LOWER"));
            }
        }
        else if (strcmp(command, naming::CMD_DOOR_STOP) == 0)
        {
            stepper_one.stop();
            stepper_two.stop();
            motors_running = false;
            motor_speed_set = false;
            door_direction = 0;
            Serial.println(F("[DOOR] Command: STOP"));
        }
    }
    else if (strcmp(device_id, naming::DEV_MAGLOCK) == 0)
    {
        // Maglock commands
        if (strcmp(command, naming::CMD_MAGLOCK_LOCK) == 0)
        {
            digitalWrite(PIN_MAGLOCK, HIGH);
            Serial.println(F("[MAGLOCK] LOCKED"));
        }
        else if (strcmp(command, naming::CMD_MAGLOCK_UNLOCK) == 0)
        {
            digitalWrite(PIN_MAGLOCK, LOW);
            Serial.println(F("[MAGLOCK] UNLOCKED"));
        }
    }
    else if (strcmp(device_id, naming::DEV_LED_STRIP) == 0)
    {
        // LED strip commands
        if (strcmp(command, naming::CMD_LEDS_ON) == 0)
        {
            strip.setBrightness(50);
            strip.show();
            Serial.println(F("[LEDS] ON"));
        }
        else if (strcmp(command, naming::CMD_LEDS_OFF) == 0)
        {
            strip.setBrightness(0);
            strip.show();
            Serial.println(F("[LEDS] OFF"));
        }
        else if (strcmp(command, naming::CMD_LEDS_SET_BRIGHTNESS) == 0)
        {
            int brightness = payload["value"] | 50;
            strip.setBrightness(brightness);
            strip.show();
            Serial.print(F("[LEDS] Brightness: "));
            Serial.println(brightness);
        }
    }
    else if (strcmp(device_id, naming::CONTROLLER_ID) == 0)
    {
        // Controller commands
        if (strcmp(command, naming::CMD_SET_STATE) == 0)
        {
            int new_state = payload["value"] | 0;
            current_state = (PuzzleState)new_state;
            Serial.print(F("[STATE] Changed to: "));
            Serial.println(new_state);
        }
        else if (strcmp(command, naming::CMD_RESET) == 0)
        {
            current_state = STATE_STARTUP;
            active_clue = 0;
            door_direction = 0;
            motors_running = false;
            Serial.println(F("[RESET] Controller reset"));
        }
        else
        {
            return; // Unknown command
        }
    }
    else
    {
        return; // Unknown device
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
// SECTION 7: SENSOR MONITORING
// ══════════════════════════════════════════════════════════════════════════════

void read_sensors()
{
    static unsigned long last_publish = 0;
    unsigned long now = millis();

    // Publish sensor data every 1 second
    if (now - last_publish >= 1000)
    {
        last_publish = now;

        JsonDocument doc;

        // Publish current state
        doc["state"] = (int)current_state;
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_STATE, doc);
        doc.clear();

        // Publish door sensors
        if (current_state == STATE_MOTORS)
        {
            doc["position"] = door_location;
            doc["direction"] = door_direction;
            doc["running"] = motors_running;
            sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_DOOR_POSITION, doc);
            doc.clear();

            doc["up_right"] = digitalRead(PIN_ENDSTOP_UP_R);
            doc["up_left"] = digitalRead(PIN_ENDSTOP_UP_L);
            doc["down_right"] = digitalRead(PIN_ENDSTOP_DN_R);
            doc["down_left"] = digitalRead(PIN_ENDSTOP_DN_L);
            sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_ENDSTOP_UP_R, doc);
            doc.clear();
        }

        // Publish knob state
        if (current_state == STATE_KNOBS)
        {
            doc["active_clue"] = active_clue;
            sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_ACTIVE_CLUE, doc);
            doc.clear();
        }

        // Publish button states
        doc["button_1"] = !digitalRead(PIN_BUTTON_1);
        doc["button_2"] = !digitalRead(PIN_BUTTON_2);
        doc["button_3"] = !digitalRead(PIN_BUTTON_3);
        sentient.publishJson(naming::CAT_SENSORS, naming::SENSOR_BUTTON_1, doc);
        doc.clear();
    }
} // ══════════════════════════════════════════════════════════════════════════════
// SECTION 8: KNOB PUZZLE LOGIC
// ══════════════════════════════════════════════════════════════════════════════

void button_riddle()
{
    static bool last_button1 = HIGH;
    static bool last_button2 = HIGH;
    static bool last_button3 = HIGH;

    bool button1 = digitalRead(PIN_BUTTON_1);
    bool button2 = digitalRead(PIN_BUTTON_2);
    bool button3 = digitalRead(PIN_BUTTON_3);

    // Detect button press (transition from HIGH to LOW)
    if (button1 == LOW && last_button1 == HIGH)
    {
        active_clue = 1;
        Serial.println(F("[BUTTON] Clue 1 selected"));
    }
    else if (button2 == LOW && last_button2 == HIGH)
    {
        active_clue = 2;
        Serial.println(F("[BUTTON] Clue 2 selected"));
    }
    else if (button3 == LOW && last_button3 == HIGH)
    {
        active_clue = 3;
        Serial.println(F("[BUTTON] Clue 3 selected"));
    }

    last_button1 = button1;
    last_button2 = button2;
    last_button3 = button3;
}

void handle_knobs()
{
    // Reset LED letters array
    memset(led_letters, 0, sizeof(led_letters));

    // Check each knob and update LED mapping
    if (digitalRead(PIN_KNOB_A2) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_a2) / sizeof(knob_pin_a2[0]); i++)
            led_letters[knob_pin_a2[i]]++;
    }

    if (digitalRead(PIN_KNOB_A3) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_a3) / sizeof(knob_pin_a3[0]); i++)
            led_letters[knob_pin_a3[i]]++;
    }

    if (digitalRead(PIN_B1) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_b1) / sizeof(knob_pin_b1[0]); i++)
            led_letters[knob_pin_b1[i]]++;
    }

    if (digitalRead(PIN_B2) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_b2) / sizeof(knob_pin_b2[0]); i++)
            led_letters[knob_pin_b2[i]]++;
    }

    if (digitalRead(PIN_B3) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_b3) / sizeof(knob_pin_b3[0]); i++)
            led_letters[knob_pin_b3[i]]++;
    }

    if (digitalRead(PIN_B4) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_b4) / sizeof(knob_pin_b4[0]); i++)
            led_letters[knob_pin_b4[i]]++;
    }

    if (digitalRead(PIN_C1) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_c1) / sizeof(knob_pin_c1[0]); i++)
            led_letters[knob_pin_c1[i]]++;
    }

    if (digitalRead(PIN_C2) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_c2) / sizeof(knob_pin_c2[0]); i++)
            led_letters[knob_pin_c2[i]]++;
    }

    if (digitalRead(PIN_C3) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_c3) / sizeof(knob_pin_c3[0]); i++)
            led_letters[knob_pin_c3[i]]++;
    }

    if (digitalRead(PIN_C4) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_c4) / sizeof(knob_pin_c4[0]); i++)
            led_letters[knob_pin_c4[i]]++;
    }

    if (digitalRead(PIN_D1) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_d1) / sizeof(knob_pin_d1[0]); i++)
            led_letters[knob_pin_d1[i]]++;
    }

    if (digitalRead(PIN_D2) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_d2) / sizeof(knob_pin_d2[0]); i++)
            led_letters[knob_pin_d2[i]]++;
    }

    if (digitalRead(PIN_D3) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_d3) / sizeof(knob_pin_d3[0]); i++)
            led_letters[knob_pin_d3[i]]++;
    }

    if (digitalRead(PIN_D4) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_d4) / sizeof(knob_pin_d4[0]); i++)
            led_letters[knob_pin_d4[i]]++;
    }

    if (digitalRead(PIN_E1) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_e1) / sizeof(knob_pin_e1[0]); i++)
            led_letters[knob_pin_e1[i]]++;
    }

    if (digitalRead(PIN_E2) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_e2) / sizeof(knob_pin_e2[0]); i++)
            led_letters[knob_pin_e2[i]]++;
    }

    if (digitalRead(PIN_E3) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_e3) / sizeof(knob_pin_e3[0]); i++)
            led_letters[knob_pin_e3[i]]++;
    }

    if (digitalRead(PIN_E4) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_e4) / sizeof(knob_pin_e4[0]); i++)
            led_letters[knob_pin_e4[i]]++;
    }

    if (digitalRead(PIN_F1) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_f1) / sizeof(knob_pin_f1[0]); i++)
            led_letters[knob_pin_f1[i]]++;
    }

    if (digitalRead(PIN_F2) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_f2) / sizeof(knob_pin_f2[0]); i++)
            led_letters[knob_pin_f2[i]]++;
    }

    if (digitalRead(PIN_F3) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_f3) / sizeof(knob_pin_f3[0]); i++)
            led_letters[knob_pin_f3[i]]++;
    }

    if (digitalRead(PIN_F4) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_f4) / sizeof(knob_pin_f4[0]); i++)
            led_letters[knob_pin_f4[i]]++;
    }

    if (digitalRead(PIN_G1) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_g1) / sizeof(knob_pin_g1[0]); i++)
            led_letters[knob_pin_g1[i]]++;
    }

    if (digitalRead(PIN_G4) == HIGH)
    {
        for (size_t i = 0; i < sizeof(knob_pin_g4) / sizeof(knob_pin_g4[0]); i++)
            led_letters[knob_pin_g4[i]]++;
    }
}

void update_leds()
{
    static int last_led_letters[NUM_LEDS] = {0};
    bool has_changed = false;

    // Check if any LED state has changed
    for (int i = 0; i < NUM_LEDS; i++)
    {
        if (led_letters[i] != last_led_letters[i])
        {
            has_changed = true;
            last_led_letters[i] = led_letters[i];
        }
    }

    // Only update LEDs if something changed
    if (has_changed)
    {
        for (int i = 0; i < NUM_LEDS; i++)
        {
            if (led_letters[i] == 0)
            {
                strip.setPixelColor(i, 255, 0, 0); // Red = unlit letter
            }
            else
            {
                strip.setPixelColor(i, 0, 0, 0); // Off = lit letter
            }
        }
        strip.show();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 9: DOOR MOTOR CONTROL
// ══════════════════════════════════════════════════════════════════════════════

void check_motors()
{
    bool endstop_one_up = digitalRead(PIN_ENDSTOP_UP_R);
    bool endstop_one_dn = digitalRead(PIN_ENDSTOP_DN_R);
    bool endstop_two_up = digitalRead(PIN_ENDSTOP_UP_L);
    bool endstop_two_dn = digitalRead(PIN_ENDSTOP_DN_L);

    if (!endstop_one_up && !endstop_one_dn && !endstop_two_up && !endstop_two_dn)
    {
        door_location = 3; // Between
    }
    else if (endstop_one_dn || endstop_two_dn)
    {
        door_location = 1; // Closed
    }
    else if (endstop_one_up || endstop_two_up)
    {
        door_location = 2; // Open
    }
}

void run_motors()
{
    bool endstop_one_up = digitalRead(PIN_ENDSTOP_UP_R);
    bool endstop_one_dn = digitalRead(PIN_ENDSTOP_DN_R);
    bool endstop_two_up = digitalRead(PIN_ENDSTOP_UP_L);
    bool endstop_two_dn = digitalRead(PIN_ENDSTOP_DN_L);

    // Start motor movement if not already running and direction is set
    if (door_direction != 0 && !motors_running)
    {
        if (door_direction == 1 && !endstop_one_up && !endstop_two_up)
        {
            motors_running = true;
            motor_speed_set = false;
            Serial.println(F("[MOTOR] Starting UP"));
        }
        else if (door_direction == -1 && !endstop_one_dn && !endstop_two_dn)
        {
            motors_running = true;
            motor_speed_set = false;
            Serial.println(F("[MOTOR] Starting DOWN"));
        }
        else
        {
            door_direction = 0;
            Serial.println(F("[MOTOR] Cannot move - at limit"));
        }
    }

    // Continue running motors if they're moving
    if (motors_running)
    {
        // Check for endstop hits
        if (door_direction == 1 && (endstop_one_up || endstop_two_up))
        {
            stepper_one.stop();
            stepper_two.stop();
            motors_running = false;
            motor_speed_set = false;
            door_direction = 0;
            Serial.println(F("[MOTOR] Reached UP endstop"));
        }
        else if (door_direction == -1 && (endstop_one_dn || endstop_two_dn))
        {
            stepper_one.stop();
            stepper_two.stop();
            motors_running = false;
            motor_speed_set = false;
            door_direction = 0;
            Serial.println(F("[MOTOR] Reached DOWN endstop"));
        }
        else
        {
            // Set speed only once when starting movement
            if (!motor_speed_set)
            {
                if (door_direction == 1)
                {
                    stepper_one.setSpeed(1000);
                    stepper_two.setSpeed(1000);
                }
                else if (door_direction == -1)
                {
                    stepper_one.setSpeed(-1000);
                    stepper_two.setSpeed(-1000);
                }
                motor_speed_set = true;
            }

            // Run at the already-set speed
            stepper_one.runSpeed();
            stepper_two.runSpeed();
        }
    }
}
