#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Lab Room Cage A Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "lab_rm_cage_a"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Lab Room Cage A Controller";

    // Door devices
    constexpr const char *DEV_DOOR_ONE = "door_one";
    constexpr const char *DEV_DOOR_TWO = "door_two";

    // Canister charging
    constexpr const char *DEV_CANISTER_CHARGING = "canister_charging";

    // Sensor types
    constexpr const char *SENSOR_DOOR_POSITION = "door_position";
    constexpr const char *SENSOR_OPEN_A = "open_sensor_a";
    constexpr const char *SENSOR_OPEN_B = "open_sensor_b";
    constexpr const char *SENSOR_CLOSED_A = "closed_sensor_a";
    constexpr const char *SENSOR_CLOSED_B = "closed_sensor_b";

    // Commands
    constexpr const char *CMD_DOOR_OPEN = "open";
    constexpr const char *CMD_DOOR_CLOSE = "close";
    constexpr const char *CMD_DOOR_STOP = "stop";
    constexpr const char *CMD_CHARGING_ON = "on";
    constexpr const char *CMD_CHARGING_OFF = "off";

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
