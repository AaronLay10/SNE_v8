#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Crank Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "crank"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Crank Puzzle Controller";

    // Devices
    constexpr const char *DEV_ENCODER_A = "encoder_a";
    constexpr const char *DEV_ENCODER_B = "encoder_b";

    // Friendly names
    constexpr const char *FRIENDLY_ENCODER_A = "Rotary Encoder A";
    constexpr const char *FRIENDLY_ENCODER_B = "Rotary Encoder B";

    // Commands (if any - this appears to be mostly a sensor device)
    constexpr const char *CMD_RESET_COUNTERS = "reset_counters";

    // Friendly command names
    constexpr const char *FRIENDLY_CMD_RESET_COUNTERS = "Reset Counters";

    // Sensors
    constexpr const char *SENSOR_ENCODER_COUNT = "encoder_count";

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
