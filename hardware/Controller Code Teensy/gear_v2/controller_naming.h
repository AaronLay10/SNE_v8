#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Gear Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "gear"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Gear Puzzle Controller";

    // ═══════════════════════════════════════════════════════════════════════════
    // DEVICE IDENTIFIERS
    // ═══════════════════════════════════════════════════════════════════════════

    // Rotary encoders (sensors only)
    constexpr const char *DEV_ENCODER_A = "encoder_a";
    constexpr const char *FRIENDLY_ENCODER_A = "Encoder A";

    constexpr const char *DEV_ENCODER_B = "encoder_b";
    constexpr const char *FRIENDLY_ENCODER_B = "Encoder B";

    // Controller device
    constexpr const char *DEV_CONTROLLER = "gear";
    constexpr const char *FRIENDLY_CONTROLLER = "Gear Puzzle Controller";

    // ═══════════════════════════════════════════════════════════════════════════
    // COMMANDS - Controller
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *CMD_LAB = "lab";
    constexpr const char *CMD_STUDY = "study";
    constexpr const char *CMD_BOILER = "boiler";
    constexpr const char *CMD_RESET = "reset";

    // ═══════════════════════════════════════════════════════════════════════════
    // SENSORS - Encoders
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *SENSOR_ENCODER_A_COUNT = "encoder_a_count";
    constexpr const char *SENSOR_ENCODER_B_COUNT = "encoder_b_count";
    constexpr const char *SENSOR_COUNTERS = "counters";

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
