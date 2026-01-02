#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Lever Riddle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "lever_riddle"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Lever Riddle Controller";

    // Hall effect sensors (input only)
    constexpr const char *DEV_HALL_A = "hall_sensor_a";
    constexpr const char *DEV_HALL_B = "hall_sensor_b";
    constexpr const char *DEV_HALL_C = "hall_sensor_c";
    constexpr const char *DEV_HALL_D = "hall_sensor_d";

    // Photocell sensor (input only)
    constexpr const char *DEV_PHOTOCELL = "photocell_lever";

    // Cube button (input only)
    constexpr const char *DEV_CUBE_BUTTON = "cube_button";

    // IR receiver (input only)
    constexpr const char *DEV_IR_RECEIVER = "ir_receiver";

    // Maglock (output device)
    constexpr const char *DEV_MAGLOCK = "maglock";

    // LED strips (output devices)
    constexpr const char *DEV_LED_STRIP = "led_strip_main";
    constexpr const char *DEV_LED_LEVER = "led_strip_lever";

    // COB light (output device)
    constexpr const char *DEV_COB_LIGHT = "cob_light";

    // Sensor types
    constexpr const char *SENSOR_MAGNET = "magnet_detected";
    constexpr const char *SENSOR_LEVER_POSITION = "lever_position";
    constexpr const char *SENSOR_BUTTON_STATE = "button_state";
    constexpr const char *SENSOR_IR_CODE = "ir_code";

    // Commands
    constexpr const char *CMD_LOCK = "lock";
    constexpr const char *CMD_UNLOCK = "unlock";
    constexpr const char *CMD_SET_COLOR = "set_color";
    constexpr const char *CMD_LIGHT_ON = "on";
    constexpr const char *CMD_LIGHT_OFF = "off";
    constexpr const char *CMD_IR_ENABLE = "enable";
    constexpr const char *CMD_IR_DISABLE = "disable";

    // Categories (fixed, lowercase)
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
