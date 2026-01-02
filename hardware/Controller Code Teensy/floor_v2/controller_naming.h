#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Floor Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "floor"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Floor Puzzle Controller";

    // Devices
    constexpr const char *DEV_FLOOR_BUTTONS = "floor_buttons"; // 9 buttons
    constexpr const char *DEV_FLOOR_LEDS = "floor_leds";       // 9 LED strips
    constexpr const char *DEV_DRAWER_MAGLOCK = "drawer_maglock";
    constexpr const char *DEV_CUCKOO_SOLENOID = "cuckoo_solenoid";
    constexpr const char *DEV_DRAWER_LIGHTS = "drawer_cob_lights";
    constexpr const char *DEV_CRYSTAL_LIGHT = "crystal_light";
    constexpr const char *DEV_LEVER_MOTOR = "lever_motor";
    constexpr const char *DEV_LEVER_LED = "lever_rgb_led";
    constexpr const char *DEV_IR_SENSOR = "ir_sensor";
    constexpr const char *DEV_PHOTOCELL = "photocell";
    constexpr const char *DEV_DRAWER_PROXIMITY = "drawer_proximity";

    // Friendly names
    constexpr const char *FRIENDLY_FLOOR_BUTTONS = "Floor Buttons (9x)";
    constexpr const char *FRIENDLY_FLOOR_LEDS = "Floor LED Strips (9x60 LEDs)";
    constexpr const char *FRIENDLY_DRAWER_MAGLOCK = "Drawer Maglock";
    constexpr const char *FRIENDLY_CUCKOO_SOLENOID = "Cuckoo Clock Solenoid";
    constexpr const char *FRIENDLY_DRAWER_LIGHTS = "Drawer COB Lights";
    constexpr const char *FRIENDLY_CRYSTAL_LIGHT = "Crystal Light";
    constexpr const char *FRIENDLY_LEVER_MOTOR = "Lever Stepper Motor";
    constexpr const char *FRIENDLY_LEVER_LED = "Lever RGB LED";
    constexpr const char *FRIENDLY_IR_SENSOR = "IR Sensor";
    constexpr const char *FRIENDLY_PHOTOCELL = "Photocell Sensor";
    constexpr const char *FRIENDLY_DRAWER_PROXIMITY = "Drawer Proximity Sensors";

    // Commands
    constexpr const char *CMD_LED_PATTERN = "led_pattern";
    constexpr const char *CMD_MAGLOCK_LOCK = "lock";
    constexpr const char *CMD_MAGLOCK_UNLOCK = "unlock";
    constexpr const char *CMD_SOLENOID_ACTIVATE = "solenoid_activate";
    constexpr const char *CMD_LIGHTS_ON = "lights_on";
    constexpr const char *CMD_LIGHTS_OFF = "lights_off";
    constexpr const char *CMD_MOTOR_MOVE = "motor_move";
    constexpr const char *CMD_LEVER_LED_COLOR = "set_color";

    // Friendly command names
    constexpr const char *FRIENDLY_CMD_LED_PATTERN = "Set LED Pattern";
    constexpr const char *FRIENDLY_CMD_LOCK = "Lock";
    constexpr const char *FRIENDLY_CMD_UNLOCK = "Unlock";
    constexpr const char *FRIENDLY_CMD_SOLENOID = "Activate Solenoid";
    constexpr const char *FRIENDLY_CMD_ON = "Turn On";
    constexpr const char *FRIENDLY_CMD_OFF = "Turn Off";
    constexpr const char *FRIENDLY_CMD_MOTOR_MOVE = "Move Motor";
    constexpr const char *FRIENDLY_CMD_SET_COLOR = "Set Color";

    // Sensors
    constexpr const char *SENSOR_BUTTON_PRESS = "button_press";
    constexpr const char *SENSOR_IR_CODE = "ir_code";
    constexpr const char *SENSOR_PHOTOCELL = "photocell";
    constexpr const char *SENSOR_PROXIMITY = "proximity";

    // Categories
    constexpr const char *CAT_COMMANDS = "commands";
    constexpr const char *CAT_SENSORS = "sensors";
    constexpr const char *CAT_STATUS = "status";
    constexpr const char *CAT_EVENTS = "events";
    constexpr const char *CAT_ACKNOWLEDGEMENT = "acknowledgement";
    constexpr const char *ITEM_HEARTBEAT = "heartbeat";
    constexpr const char *ITEM_HARDWARE = "hardware";
    constexpr const char *ITEM_COMMAND_ACK = "command_ack";
}

#endif // CONTROLLER_NAMING_H
