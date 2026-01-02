/*
 * =============================================================================
 * Lever Boiler Controller v2
 * =============================================================================
 * Devices:
 *  - lever_boiler: maglock + lever LED + sensors (photocell, IR code)
 *  - lever_stairs: maglock + lever LED + sensors (photocell, IR code)
 *  - newell_post: light + stepper motor (up/down/stop) + 2 proximity sensors
 *
 * Features:
 *  - Device-scoped commands (unlock/lock, led_on/off, light_on/off, stepper_up/down/stop)
 *  - Change-based sensor publishing + periodic refresh
 *  - IR receiver alternates between two pins to read two guns
 *
 * Target: Teensy 4.1
 */

#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <SentientCapabilityManifest.h>

// Suppress IRremote begin() error - we're using receiver only
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN
#include <IRremote.hpp>

#include "FirmwareMetadata.h"
#include "controller_naming.h"

using namespace naming;

// =============================================================================
// MQTT CONFIGURATION
// =============================================================================

const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_user = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
const unsigned long heartbeat_interval_ms = 5000; // 5 seconds

// =============================================================================
// PIN DEFINITIONS (from legacy LeverBoiler_v2.1)
// =============================================================================

const int power_led_pin = 13;
const int photocell_boiler_pin = A1;
const int photocell_stairs_pin = A0;
const int ir_sensor_1_pin = 16; // boiler gun
const int ir_sensor_2_pin = 17; // stairs gun
const int maglock_boiler_pin = 33;
const int maglock_stairs_pin = 34;
const int lever_led_boiler_pin = 20;
const int lever_led_stairs_pin = 19;
const int newell_post_light_pin = 36;
const int newell_prox_up_pin = 39;
const int newell_prox_down_pin = 38;

// Stepper motor pins (4-phase)
const int stepper_pin_1 = 1;
const int stepper_pin_2 = 2;
const int stepper_pin_3 = 3;
const int stepper_pin_4 = 4;

// =============================================================================
// CONSTANTS
// =============================================================================

const int photocell_threshold = 500;                    // tune as needed
const unsigned long sensor_publish_interval_ms = 60000; // 60s
const unsigned long ir_switch_interval = 200;           // ms
const uint32_t target_ir_code = 0x51;                   // allowed gun code

// Stepper sequence and timing
const int step_sequence[4][4] = {
    {1, 0, 0, 0},
    {0, 1, 0, 0},
    {0, 0, 1, 0},
    {0, 0, 0, 1}};
const int stepper_delay_us = 1000; // microseconds between steps

// =============================================================================
// RUNTIME STATE
// =============================================================================

// Locks and lights
bool boiler_unlocked = false; // HIGH=lock, LOW=unlock
bool stairs_unlocked = false;
bool newell_light_on = false;

// IR handling
int current_ir_pin = ir_sensor_1_pin; // start on boiler
unsigned long last_ir_switch_time = 0;
bool ir_signal_in_progress = false;
bool ir_enabled = true;

// Photocells
int photocell_boiler = 0;
int photocell_stairs = 0;
bool boiler_valve_open = false;
bool stairs_valve_open = false;
bool last_boiler_valve_open = false;
bool last_stairs_valve_open = false;
int last_photocell_boiler = 0;
int last_photocell_stairs = 0;

// Proximity sensors (newell limits)
bool prox_up = false;
bool prox_down = false;
bool last_prox_up = false;
bool last_prox_down = false;

// Stepper
enum StepperDir
{
    DIR_STOP = 0,
    DIR_UP = -1,
    DIR_DOWN = 1
};
StepperDir stepper_dir = DIR_STOP;
bool stepper_moving = false;
int step_index = 0;
unsigned long last_step_time_us = 0;

// Publishing cadence
unsigned long last_sensor_publish_time = 0;
bool sensors_initialized = false;

// =============================================================================
// DEVICE REGISTRY
// =============================================================================

// Lever Boiler
const char *boiler_commands[] = {
    CMD_MAGLOCK_BOILER_UNLOCK,
    CMD_MAGLOCK_BOILER_LOCK,
    CMD_LEVER_LED_BOILER_ON,
    CMD_LEVER_LED_BOILER_OFF};
const char *boiler_sensors[] = {
    SENSOR_BOILER_PHOTOCELL,
    SENSOR_BOILER_IR_CODE};

// Lever Stairs
const char *stairs_commands[] = {
    CMD_MAGLOCK_STAIRS_UNLOCK,
    CMD_MAGLOCK_STAIRS_LOCK,
    CMD_LEVER_LED_STAIRS_ON,
    CMD_LEVER_LED_STAIRS_OFF};
const char *stairs_sensors[] = {
    SENSOR_STAIRS_PHOTOCELL,
    SENSOR_STAIRS_IR_CODE};

// Newell Post
const char *newell_commands[] = {
    CMD_NEWELL_LIGHT_ON,
    CMD_NEWELL_LIGHT_OFF,
    CMD_STEPPER_UP,
    CMD_STEPPER_DOWN,
    CMD_STEPPER_STOP};
const char *newell_sensors[] = {
    SENSOR_NEWELL_POST_TOP_PROXIMITY,
    SENSOR_NEWELL_POST_BOTTOM_PROXIMITY};

SentientDeviceDef dev_lever_boiler(
    DEV_LEVER_BOILER,
    FRIENDLY_LEVER_BOILER,
    "lever_station",
    boiler_commands, 4,
    boiler_sensors, 2);

SentientDeviceDef dev_lever_stairs(
    DEV_LEVER_STAIRS,
    FRIENDLY_LEVER_STAIRS,
    "lever_station",
    stairs_commands, 4,
    stairs_sensors, 2);

SentientDeviceDef dev_newell_post(
    DEV_NEWELL_POST,
    FRIENDLY_NEWELL_POST,
    "newell_post",
    newell_commands, 5,
    newell_sensors, 2);

SentientDeviceRegistry deviceRegistry;

// =============================================================================
// FORWARD DECLARATIONS
// =============================================================================

void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);
void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void publish_command_acknowledgement(const char *device_id, const char *command);
void read_sensors();
void publish_sensor_changes(bool force_publish);
void publish_hardware_status();
void handle_ir_signal(int pin);
void set_stepper_pins(int a, int b, int c, int d);
void step_motor(int direction);
void stop_stepper();
void move_stepper_up();
void move_stepper_down();

// =============================================================================
// MQTT OBJECTS
// =============================================================================

SentientCapabilityManifest manifest;
SentientMQTT mqtt(build_mqtt_config());

// =============================================================================
// SETUP
// =============================================================================

void setup()
{
    Serial.begin(115200);
    unsigned long waited = 0;
    while (!Serial && waited < 2000)
    {
        delay(10);
        waited += 10;
    }

    Serial.println();
    Serial.println("╔════════════════════════════════════════════════════════════╗");
    Serial.println("║       Sentient Engine - Lever Boiler Controller v2        ║");
    Serial.println("╚════════════════════════════════════════════════════════════╝");
    Serial.print("[LeverBoiler] Firmware Version: ");
    Serial.println(firmware::VERSION);
    Serial.print("[LeverBoiler] Unique ID: ");
    Serial.println(firmware::UNIQUE_ID);
    Serial.print("[LeverBoiler] Controller ID: ");
    Serial.println(CONTROLLER_ID);
    Serial.println();

    // GPIO setup
    pinMode(power_led_pin, OUTPUT);
    pinMode(maglock_boiler_pin, OUTPUT);
    pinMode(maglock_stairs_pin, OUTPUT);
    pinMode(lever_led_boiler_pin, OUTPUT);
    pinMode(lever_led_stairs_pin, OUTPUT);
    pinMode(newell_post_light_pin, OUTPUT);
    pinMode(newell_prox_up_pin, INPUT_PULLDOWN);
    pinMode(newell_prox_down_pin, INPUT_PULLDOWN);

    pinMode(stepper_pin_1, OUTPUT);
    pinMode(stepper_pin_2, OUTPUT);
    pinMode(stepper_pin_3, OUTPUT);
    pinMode(stepper_pin_4, OUTPUT);

    // Initial states
    digitalWrite(power_led_pin, HIGH);
    digitalWrite(maglock_boiler_pin, HIGH); // Locked
    digitalWrite(maglock_stairs_pin, HIGH);
    digitalWrite(lever_led_boiler_pin, HIGH); // LED on by default
    digitalWrite(lever_led_stairs_pin, HIGH);
    digitalWrite(newell_post_light_pin, LOW);
    set_stepper_pins(0, 0, 0, 0);

    // IR init
    IrReceiver.begin(current_ir_pin, DISABLE_LED_FEEDBACK, power_led_pin);
    last_ir_switch_time = millis();

    // Register devices and build manifest
    Serial.println("[LeverBoiler] Registering devices...");
    deviceRegistry.addDevice(&dev_lever_boiler);
    deviceRegistry.addDevice(&dev_lever_stairs);
    deviceRegistry.addDevice(&dev_newell_post);
    deviceRegistry.printSummary();

    Serial.println("[LeverBoiler] Building capability manifest...");
    build_capability_manifest();

    // MQTT init
    Serial.println("[LeverBoiler] Initializing MQTT...");
    if (!mqtt.begin())
    {
        Serial.println("[LeverBoiler] MQTT init failed - continuing offline");
    }
    else
    {
        mqtt.setCommandCallback(handle_mqtt_command);
        mqtt.setHeartbeatBuilder(build_heartbeat_payload);
        Serial.println("[LeverBoiler] Waiting for broker connection...");
        unsigned long t0 = millis();
        while (!mqtt.isConnected() && millis() - t0 < 5000)
        {
            mqtt.loop();
            delay(100);
        }
        if (mqtt.isConnected())
        {
            Serial.println("[LeverBoiler] Broker connected!");
            if (manifest.publish_registration(mqtt.get_client(), ROOM_ID, CONTROLLER_ID))
            {
                Serial.println("[LeverBoiler] Registration successful!");
            }
            else
            {
                Serial.println("[LeverBoiler] Registration failed - will retry later");
            }
            publish_hardware_status();
        }
        else
        {
            Serial.println("[LeverBoiler] Broker connection timeout - offline");
        }
    }
}

// =============================================================================
// LOOP
// =============================================================================

void loop()
{
    mqtt.loop();

    // IR read and alternate sensors
    if (ir_enabled && IrReceiver.decode())
    {
        ir_signal_in_progress = true;
        handle_ir_signal(current_ir_pin);
        IrReceiver.resume();
        ir_signal_in_progress = false;
        last_ir_switch_time = millis();
    }
    if (ir_enabled && !ir_signal_in_progress && millis() - last_ir_switch_time > ir_switch_interval)
    {
        current_ir_pin = (current_ir_pin == ir_sensor_1_pin) ? ir_sensor_2_pin : ir_sensor_1_pin;
        IrReceiver.begin(current_ir_pin, ENABLE_LED_FEEDBACK, power_led_pin);
        last_ir_switch_time = millis();
    }

    // Read sensors and publish
    read_sensors();
    bool force_pub = (millis() - last_sensor_publish_time >= sensor_publish_interval_ms);
    publish_sensor_changes(force_pub);
    if (force_pub)
        last_sensor_publish_time = millis();

    // Stepper control
    if (stepper_moving)
    {
        // Stop at limits
        if ((stepper_dir == DIR_UP && digitalRead(newell_prox_up_pin) == HIGH) ||
            (stepper_dir == DIR_DOWN && digitalRead(newell_prox_down_pin) == HIGH))
        {
            stop_stepper();
            publish_hardware_status();
        }
        else
        {
            // Run motor
            step_motor((int)stepper_dir);
        }
    }
}

// =============================================================================
// MQTT CONFIGURATION + MANIFEST
// =============================================================================

SentientMQTTConfig build_mqtt_config()
{
    SentientMQTTConfig cfg{};
    if (mqtt_host && mqtt_host[0] != '\0')
        cfg.brokerHost = mqtt_host;
    cfg.brokerIp = mqtt_broker_ip;
    cfg.brokerPort = mqtt_port;
    cfg.username = mqtt_user;
    cfg.password = mqtt_password;
    cfg.namespaceId = CLIENT_ID;
    cfg.roomId = ROOM_ID;
    cfg.controllerId = CONTROLLER_ID;
    cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
    cfg.displayName = CONTROLLER_FRIENDLY_NAME;
    cfg.publishJsonCapacity = 1536;
    cfg.heartbeatIntervalMs = heartbeat_interval_ms;
    cfg.autoHeartbeat = true;
    cfg.useDhcp = true;
    return cfg;
}

void build_capability_manifest()
{
    manifest.set_controller_info(
        CONTROLLER_ID,
        CONTROLLER_FRIENDLY_NAME,
        firmware::VERSION,
        ROOM_ID,
        CONTROLLER_ID);

    deviceRegistry.buildManifest(manifest);
}

bool build_heartbeat_payload(JsonDocument &doc, void * /*ctx*/)
{
    doc["uid"] = CONTROLLER_ID;
    doc["fw"] = firmware::VERSION;
    doc["up"] = millis();
    return true;
}

// =============================================================================
// COMMAND HANDLER
// =============================================================================

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    const char *device_ctx = (const char *)ctx; // expected device id
    String cmd = String(command);

    Serial.print(F("[COMMAND] device="));
    Serial.print(device_ctx ? device_ctx : "<null>");
    Serial.print(F(" cmd="));
    Serial.println(cmd);

    // Lever Boiler commands
    if (device_ctx && strcmp(device_ctx, DEV_LEVER_BOILER) == 0)
    {
        if (cmd.equals(CMD_MAGLOCK_BOILER_UNLOCK))
        {
            digitalWrite(maglock_boiler_pin, LOW); // unlock
            boiler_unlocked = true;
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_MAGLOCK_BOILER_LOCK))
        {
            digitalWrite(maglock_boiler_pin, HIGH); // lock
            boiler_unlocked = false;
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_LEVER_LED_BOILER_ON))
        {
            digitalWrite(lever_led_boiler_pin, HIGH);
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_LEVER_LED_BOILER_OFF))
        {
            digitalWrite(lever_led_boiler_pin, LOW);
            publish_hardware_status();
        }
    }
    // Lever Stairs commands
    else if (device_ctx && strcmp(device_ctx, DEV_LEVER_STAIRS) == 0)
    {
        if (cmd.equals(CMD_MAGLOCK_STAIRS_UNLOCK))
        {
            digitalWrite(maglock_stairs_pin, LOW);
            stairs_unlocked = true;
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_MAGLOCK_STAIRS_LOCK))
        {
            digitalWrite(maglock_stairs_pin, HIGH);
            stairs_unlocked = false;
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_LEVER_LED_STAIRS_ON))
        {
            digitalWrite(lever_led_stairs_pin, HIGH);
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_LEVER_LED_STAIRS_OFF))
        {
            digitalWrite(lever_led_stairs_pin, LOW);
            publish_hardware_status();
        }
    }
    // Newell Post commands
    else if (device_ctx && strcmp(device_ctx, DEV_NEWELL_POST) == 0)
    {
        if (cmd.equals(CMD_NEWELL_LIGHT_ON))
        {
            newell_light_on = true;
            digitalWrite(newell_post_light_pin, HIGH);
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_NEWELL_LIGHT_OFF))
        {
            newell_light_on = false;
            digitalWrite(newell_post_light_pin, LOW);
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_STEPPER_UP))
        {
            move_stepper_up();
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_STEPPER_DOWN))
        {
            move_stepper_down();
            publish_hardware_status();
        }
        else if (cmd.equals(CMD_STEPPER_STOP))
        {
            stop_stepper();
            publish_hardware_status();
        }
    }
    else
    {
        Serial.println(F("[WARNING] Unknown device or command"));
        return;
    }

    // Publish acknowledgement for successful command
    publish_command_acknowledgement(device_ctx, command);
}

// =============================================================================
// COMMAND ACKNOWLEDGEMENT
// =============================================================================

void publish_command_acknowledgement(const char *device_id, const char *command)
{
    if (!mqtt.isConnected())
        return;

    StaticJsonDocument<160> ack;
    ack["controller_id"] = CONTROLLER_ID;
    ack["device_id"] = device_id;
    ack["command"] = command;
    ack["success"] = true;
    ack["timestamp_ms"] = millis();

    char buf[196];
    serializeJson(ack, buf, sizeof(buf));

    // Topic: <tenant>/<room>/acknowledgement/<controller>/<device>/<command>
    String ackTopic = String(CLIENT_ID) + "/" + String(ROOM_ID) + "/" +
                      String(CAT_ACKNOWLEDGEMENT) + "/" + String(CONTROLLER_ID) + "/" +
                      String(device_id) + "/" + String(command);

    mqtt.get_client().publish(ackTopic.c_str(), buf, false);

    Serial.print(F("[ACK] -> "));
    Serial.println(ackTopic);
}

// =============================================================================
// SENSOR READING + PUBLISHING
// =============================================================================

void read_sensors()
{
    // Photocells
    photocell_boiler = analogRead(photocell_boiler_pin);
    photocell_stairs = analogRead(photocell_stairs_pin);
    boiler_valve_open = (photocell_boiler > photocell_threshold);
    stairs_valve_open = (photocell_stairs > photocell_threshold);

    // Proximity
    prox_up = digitalRead(newell_prox_up_pin) == HIGH;
    prox_down = digitalRead(newell_prox_down_pin) == HIGH;
}

void publish_sensor_changes(bool force_publish)
{
    if (!mqtt.isConnected())
        return;
    JsonDocument doc;

    // Boiler sensors - Only publish on STATE change (OPEN/CLOSED), not raw value fluctuations
    if (!sensors_initialized || force_publish || boiler_valve_open != last_boiler_valve_open)
    {
        doc.clear();
        doc["open"] = boiler_valve_open ? 1 : 0;
        doc["raw"] = photocell_boiler;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_LEVER_BOILER) + "/" + SENSOR_BOILER_PHOTOCELL).c_str(), doc);
        last_boiler_valve_open = boiler_valve_open;
        last_photocell_boiler = photocell_boiler;
    }

    // Stairs sensors - Only publish on STATE change (OPEN/CLOSED), not raw value fluctuations
    if (!sensors_initialized || force_publish || stairs_valve_open != last_stairs_valve_open)
    {
        doc.clear();
        doc["open"] = stairs_valve_open ? 1 : 0;
        doc["raw"] = photocell_stairs;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_LEVER_STAIRS) + "/" + SENSOR_STAIRS_PHOTOCELL).c_str(), doc);
        last_stairs_valve_open = stairs_valve_open;
        last_photocell_stairs = photocell_stairs;
    }

    // Newell proximity
    if (!sensors_initialized || force_publish || prox_up != last_prox_up)
    {
        doc.clear();
        doc["state"] = prox_up ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_NEWELL_POST) + "/" + SENSOR_NEWELL_POST_TOP_PROXIMITY).c_str(), doc);
        last_prox_up = prox_up;
    }
    if (!sensors_initialized || force_publish || prox_down != last_prox_down)
    {
        doc.clear();
        doc["state"] = prox_down ? 1 : 0;
        mqtt.publishJson(CAT_SENSORS, (String(DEV_NEWELL_POST) + "/" + SENSOR_NEWELL_POST_BOTTOM_PROXIMITY).c_str(), doc);
        last_prox_down = prox_down;
    }

    sensors_initialized = true;
}

void publish_hardware_status()
{
    if (!mqtt.isConnected())
        return;
    JsonDocument doc;
    doc["boilerUnlocked"] = boiler_unlocked;
    doc["stairsUnlocked"] = stairs_unlocked;
    doc["newellLight"] = newell_light_on;
    doc["stepperMoving"] = stepper_moving;
    doc["dir"] = (stepper_dir == DIR_UP ? "up" : (stepper_dir == DIR_DOWN ? "down" : "stop"));
    mqtt.publishState(ITEM_HARDWARE, doc);
}

// =============================================================================
// IR HANDLER
// =============================================================================

void handle_ir_signal(int pin)
{
    // Ignore while stepper running (safety)
    if (stepper_moving)
        return;

    // Validate signal (ignore empty)
    bool is_noise = (IrReceiver.decodedIRData.command == 0 &&
                     IrReceiver.decodedIRData.address == 0 &&
                     IrReceiver.decodedIRData.decodedRawData == 0 &&
                     IrReceiver.decodedIRData.numberOfBits == 0);
    if (is_noise)
        return;

    uint32_t command = IrReceiver.decodedIRData.command;
    uint32_t raw = IrReceiver.decodedIRData.decodedRawData;

    // Determine which device to publish to
    const char *dev = (pin == ir_sensor_1_pin) ? DEV_LEVER_BOILER : DEV_LEVER_STAIRS;
    const char *sensor = (pin == ir_sensor_1_pin) ? SENSOR_BOILER_IR_CODE : SENSOR_STAIRS_IR_CODE;

    JsonDocument doc;
    doc["code"] = (int)command;
    doc["raw"] = (int)raw;
    mqtt.publishJson(CAT_SENSORS, (String(dev) + "/" + sensor).c_str(), doc);

    // Feedback blink on power LED
    digitalWrite(power_led_pin, LOW);
    delay(60);
    digitalWrite(power_led_pin, HIGH);
    delay(60);
    digitalWrite(power_led_pin, LOW);
    delay(60);
    digitalWrite(power_led_pin, HIGH);
}

// =============================================================================
// STEPPER CONTROL
// =============================================================================

void set_stepper_pins(int a, int b, int c, int d)
{
    digitalWrite(stepper_pin_1, a);
    digitalWrite(stepper_pin_2, b);
    digitalWrite(stepper_pin_3, c);
    digitalWrite(stepper_pin_4, d);
}

void step_motor(int direction)
{
    unsigned long now = micros();
    if ((long)(now - last_step_time_us) < stepper_delay_us)
        return;

    step_index += direction;
    if (step_index < 0)
        step_index = 3;
    if (step_index > 3)
        step_index = 0;

    set_stepper_pins(
        step_sequence[step_index][0],
        step_sequence[step_index][1],
        step_sequence[step_index][2],
        step_sequence[step_index][3]);

    last_step_time_us = now;
}

void stop_stepper()
{
    stepper_dir = DIR_STOP;
    stepper_moving = false;
    set_stepper_pins(0, 0, 0, 0);
    Serial.println(F("[Newell] Stepper stopped"));
}

void move_stepper_up()
{
    if (digitalRead(newell_prox_up_pin) == HIGH)
    {
        Serial.println(F("[Newell] Already at UP limit"));
        stop_stepper();
        return;
    }
    stepper_dir = DIR_UP;
    stepper_moving = true;
    Serial.println(F("[Newell] Moving UP"));
}

void move_stepper_down()
{
    if (digitalRead(newell_prox_down_pin) == HIGH)
    {
        Serial.println(F("[Newell] Already at DOWN limit"));
        stop_stepper();
        return;
    }
    stepper_dir = DIR_DOWN;
    stepper_moving = true;
    Serial.println(F("[Newell] Moving DOWN"));
}
