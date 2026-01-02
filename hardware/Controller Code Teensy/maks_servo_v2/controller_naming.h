#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Maks Servo Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "maks_servo"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Maks Servo Controller";

    // Servo device
    constexpr const char *DEV_SERVO = "servo";
    constexpr const char *FRIENDLY_SERVO = "Maks Servo";

    // Commands
    constexpr const char *CMD_OPEN = "open";
    constexpr const char *CMD_CLOSE = "close";
    constexpr const char *CMD_SET_POSITION = "set_position";

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
