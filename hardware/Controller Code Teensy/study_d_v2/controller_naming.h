#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Study D Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "study_d"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Study D Puzzle Controller";

    // Device IDs
    constexpr const char *DEV_MOTOR_LEFT = "motor_left";
    constexpr const char *DEV_MOTOR_RIGHT = "motor_right";
    constexpr const char *DEV_PROXIMITY_SENSORS = "proximity_sensors";
    constexpr const char *DEV_FOG_DMX = "fog_dmx";

    // Sensor types
    constexpr const char *SENSOR_LEFT_TOP_1 = "left_top_1";
    constexpr const char *SENSOR_LEFT_TOP_2 = "left_top_2";
    constexpr const char *SENSOR_LEFT_BOTTOM_1 = "left_bottom_1";
    constexpr const char *SENSOR_LEFT_BOTTOM_2 = "left_bottom_2";
    constexpr const char *SENSOR_RIGHT_TOP_1 = "right_top_1";
    constexpr const char *SENSOR_RIGHT_TOP_2 = "right_top_2";
    constexpr const char *SENSOR_RIGHT_BOTTOM_1 = "right_bottom_1";
    constexpr const char *SENSOR_RIGHT_BOTTOM_2 = "right_bottom_2";

    // Commands
    constexpr const char *CMD_UP = "up";
    constexpr const char *CMD_DOWN = "down";
    constexpr const char *CMD_STOP = "stop";
    constexpr const char *CMD_SET_VOLUME = "set_volume";
    constexpr const char *CMD_SET_TIMER = "set_timer";
    constexpr const char *CMD_SET_FAN_SPEED = "set_fan_speed";

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
