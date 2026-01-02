#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Kraken Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "kraken"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Kraken Puzzle Controller";

    // TODO: Define device identifiers based on analysis of original .ino file
    // Example:
    // constexpr const char *DEV_DEVICE_NAME = "device_name";
    // constexpr const char *FRIENDLY_DEVICE_NAME = "Device Friendly Name";
    
    // TODO: Define command slugs (snake_case)
    // Example:
    // constexpr const char *CMD_COMMAND_NAME = "command_name";
    // constexpr const char *FRIENDLY_CMD_COMMAND_NAME = "Command Friendly Name";

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
