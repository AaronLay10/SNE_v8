#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Lab Room Doors & Hoist Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "lab_rm_doors_hoist"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Lab Room Doors & Hoist Controller";

    // Device IDs
    constexpr const char *DEV_HOIST = "hoist";
    constexpr const char *DEV_LAB_DOOR_LEFT = "lab_door_left";
    constexpr const char *DEV_LAB_DOOR_RIGHT = "lab_door_right";
    constexpr const char *DEV_IR_RECEIVER = "gun_ir_receiver";
    constexpr const char *DEV_ROPE_DROP = "rope_drop";

    // Sensor types
    constexpr const char *SENSOR_UP_A = "up_sensor_a";
    constexpr const char *SENSOR_UP_B = "up_sensor_b";
    constexpr const char *SENSOR_DOWN_A = "down_sensor_a";
    constexpr const char *SENSOR_DOWN_B = "down_sensor_b";
    constexpr const char *SENSOR_OPEN_A = "open_sensor_a";
    constexpr const char *SENSOR_OPEN_B = "open_sensor_b";
    constexpr const char *SENSOR_CLOSED_A = "closed_sensor_a";
    constexpr const char *SENSOR_CLOSED_B = "closed_sensor_b";
    constexpr const char *SENSOR_IR_CODE = "ir_code";

    // Commands
    constexpr const char *CMD_UP = "up";
    constexpr const char *CMD_DOWN = "down";
    constexpr const char *CMD_STOP = "stop";
    constexpr const char *CMD_DOOR_OPEN = "open";
    constexpr const char *CMD_DOOR_CLOSE = "close";
    constexpr const char *CMD_DOOR_STOP = "stop";
    constexpr const char *CMD_DROP = "drop";
    constexpr const char *CMD_RESET = "reset";

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
