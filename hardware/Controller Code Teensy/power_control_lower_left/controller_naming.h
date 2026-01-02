#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Power Control Lower Left v2 — Canonical naming
// Purpose: single source of truth for identifiers used in MQTT topics and manifests.

#include "FirmwareMetadata.h" // Provides firmware::UNIQUE_ID (must equal controller_id)

namespace naming
{
    // Tenant and room identifiers (snake_case, DB-sourced)
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";

    // Controller identifier: must match DB controllers.controller_id
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "power_control_lower_left"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Power Control - Lower Left";

    // ========================================================================
    // DEVICE IDENTIFIERS (snake_case, one per physical relay/device)
    // ========================================================================

    // Lever Riddle Cube Power Rails
    constexpr const char *DEV_LEVER_RIDDLE_CUBE_24V = "lever_riddle_cube_24v";
    constexpr const char *DEV_LEVER_RIDDLE_CUBE_12V = "lever_riddle_cube_12v";
    constexpr const char *DEV_LEVER_RIDDLE_CUBE_5V = "lever_riddle_cube_5v";

    // Clock Puzzle Power Rails
    constexpr const char *DEV_CLOCK_24V = "clock_24v";
    constexpr const char *DEV_CLOCK_12V = "clock_12v";
    constexpr const char *DEV_CLOCK_5V = "clock_5v";

    // ========================================================================
    // FRIENDLY NAMES (UI-only, never in MQTT topics)
    // ========================================================================

    // Lever Riddle Cube
    constexpr const char *FRIENDLY_LEVER_RIDDLE_CUBE_24V = "Lever Riddle Cube 24V";
    constexpr const char *FRIENDLY_LEVER_RIDDLE_CUBE_12V = "Lever Riddle Cube 12V";
    constexpr const char *FRIENDLY_LEVER_RIDDLE_CUBE_5V = "Lever Riddle Cube 5V";

    // Clock
    constexpr const char *FRIENDLY_CLOCK_24V = "Clock Puzzle 24V";
    constexpr const char *FRIENDLY_CLOCK_12V = "Clock Puzzle 12V";
    constexpr const char *FRIENDLY_CLOCK_5V = "Clock Puzzle 5V";

    // Controller
    constexpr const char *FRIENDLY_CONTROLLER = "Power Control - Lower Left";

    // ========================================================================
    // COMMAND SLUGS (snake_case, standardized on/off pattern)
    // ========================================================================

    constexpr const char *CMD_POWER_ON = "power_on";
    constexpr const char *CMD_POWER_OFF = "power_off";

    // Controller-level commands
    constexpr const char *CMD_ALL_ON = "all_on";
    constexpr const char *CMD_ALL_OFF = "all_off";
    constexpr const char *CMD_EMERGENCY_OFF = "emergency_off";
    constexpr const char *CMD_RESET = "reset";
    constexpr const char *CMD_REQUEST_STATUS = "request_status";

    // Friendly names for commands (UI-only)
    constexpr const char *FRIENDLY_CMD_POWER_ON = "Power On";
    constexpr const char *FRIENDLY_CMD_POWER_OFF = "Power Off";
    constexpr const char *FRIENDLY_CMD_ALL_ON = "All Devices On";
    constexpr const char *FRIENDLY_CMD_ALL_OFF = "All Devices Off";
    constexpr const char *FRIENDLY_CMD_EMERGENCY_OFF = "Emergency Power Off";
    constexpr const char *FRIENDLY_CMD_RESET = "Reset Controller";
    constexpr const char *FRIENDLY_CMD_REQUEST_STATUS = "Request Status";

    // ========================================================================
    // CATEGORIES (fixed, lowercase)
    // ========================================================================

    constexpr const char *CAT_COMMANDS = "commands";
    constexpr const char *CAT_SENSORS = "sensors";
    constexpr const char *CAT_STATUS = "status";
    constexpr const char *CAT_EVENTS = "events";
    constexpr const char *CAT_ACKNOWLEDGEMENT = "acknowledgement";
    constexpr const char *ITEM_HEARTBEAT = "heartbeat";
    constexpr const char *ITEM_HARDWARE = "hardware";
    constexpr const char *ITEM_COMMAND_ACK = "command_ack";

} // namespace naming

#endif // CONTROLLER_NAMING_H
