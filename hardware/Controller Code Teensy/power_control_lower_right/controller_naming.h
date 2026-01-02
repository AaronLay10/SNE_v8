#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Power Control Lower Right v2 — Canonical naming
// Purpose: single source of truth for identifiers used in MQTT topics and manifests.

#include "FirmwareMetadata.h" // Provides firmware::UNIQUE_ID (must equal controller_id)

namespace naming
{
    // Tenant and room identifiers (snake_case, DB-sourced)
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";

    // Controller identifier: must match DB controllers.controller_id
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "power_control_lower_right"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Power Control - Lower Right";

    // ========================================================================
    // DEVICE IDENTIFIERS (snake_case, one per physical relay/device)
    // ========================================================================

    // Gear Puzzle Power Rails
    constexpr const char *DEV_GEAR_24V = "gear_24v";
    constexpr const char *DEV_GEAR_12V = "gear_12v";
    constexpr const char *DEV_GEAR_5V = "gear_5v";

    // Floor Puzzle Power Rails
    constexpr const char *DEV_FLOOR_24V = "floor_24v";
    constexpr const char *DEV_FLOOR_12V = "floor_12v";
    constexpr const char *DEV_FLOOR_5V = "floor_5v";

    // Riddle RPi Power Rails
    constexpr const char *DEV_RIDDLE_RPI_5V = "riddle_rpi_5v";
    constexpr const char *DEV_RIDDLE_RPI_12V = "riddle_rpi_12v";
    constexpr const char *DEV_RIDDLE_5V = "riddle_5v";

    // Boiler Room Subpanel Power Rails
    constexpr const char *DEV_BOILER_ROOM_SUBPANEL_24V = "boiler_room_subpanel_24v";
    constexpr const char *DEV_BOILER_ROOM_SUBPANEL_12V = "boiler_room_subpanel_12v";
    constexpr const char *DEV_BOILER_ROOM_SUBPANEL_5V = "boiler_room_subpanel_5v";

    // Lab Room Subpanel Power Rails
    constexpr const char *DEV_LAB_ROOM_SUBPANEL_24V = "lab_room_subpanel_24v";
    constexpr const char *DEV_LAB_ROOM_SUBPANEL_12V = "lab_room_subpanel_12v";
    constexpr const char *DEV_LAB_ROOM_SUBPANEL_5V = "lab_room_subpanel_5v";

    // Study Room Subpanel Power Rails
    constexpr const char *DEV_STUDY_ROOM_SUBPANEL_24V = "study_room_subpanel_24v";
    constexpr const char *DEV_STUDY_ROOM_SUBPANEL_12V = "study_room_subpanel_12v";
    constexpr const char *DEV_STUDY_ROOM_SUBPANEL_5V = "study_room_subpanel_5v";

    // Gun Drawers Power Rails
    constexpr const char *DEV_GUN_DRAWERS_24V = "gun_drawers_24v";
    constexpr const char *DEV_GUN_DRAWERS_12V = "gun_drawers_12v";
    constexpr const char *DEV_GUN_DRAWERS_5V = "gun_drawers_5v";

    // Keys Puzzle Power
    constexpr const char *DEV_KEYS_5V = "keys_5v";

    // Reserved/Empty Relays
    constexpr const char *DEV_EMPTY_35 = "empty_35";
    constexpr const char *DEV_EMPTY_34 = "empty_34";

    // Controller virtual device
    constexpr const char *DEV_CONTROLLER = "controller";

    // ========================================================================
    // FRIENDLY NAMES (UI-only, never in MQTT topics)
    // ========================================================================

    // Gear
    constexpr const char *FRIENDLY_GEAR_24V = "Gear Puzzle 24V";
    constexpr const char *FRIENDLY_GEAR_12V = "Gear Puzzle 12V";
    constexpr const char *FRIENDLY_GEAR_5V = "Gear Puzzle 5V";

    // Floor
    constexpr const char *FRIENDLY_FLOOR_24V = "Floor Puzzle 24V";
    constexpr const char *FRIENDLY_FLOOR_12V = "Floor Puzzle 12V";
    constexpr const char *FRIENDLY_FLOOR_5V = "Floor Puzzle 5V";

    // Riddle
    constexpr const char *FRIENDLY_RIDDLE_RPI_5V = "Riddle RPi 5V";
    constexpr const char *FRIENDLY_RIDDLE_RPI_12V = "Riddle RPi 12V";
    constexpr const char *FRIENDLY_RIDDLE_5V = "Riddle 5V";

    // Boiler Room Subpanel
    constexpr const char *FRIENDLY_BOILER_ROOM_SUBPANEL_24V = "Boiler Room Subpanel 24V";
    constexpr const char *FRIENDLY_BOILER_ROOM_SUBPANEL_12V = "Boiler Room Subpanel 12V";
    constexpr const char *FRIENDLY_BOILER_ROOM_SUBPANEL_5V = "Boiler Room Subpanel 5V";

    // Lab Room Subpanel
    constexpr const char *FRIENDLY_LAB_ROOM_SUBPANEL_24V = "Lab Room Subpanel 24V";
    constexpr const char *FRIENDLY_LAB_ROOM_SUBPANEL_12V = "Lab Room Subpanel 12V";
    constexpr const char *FRIENDLY_LAB_ROOM_SUBPANEL_5V = "Lab Room Subpanel 5V";

    // Study Room Subpanel
    constexpr const char *FRIENDLY_STUDY_ROOM_SUBPANEL_24V = "Study Room Subpanel 24V";
    constexpr const char *FRIENDLY_STUDY_ROOM_SUBPANEL_12V = "Study Room Subpanel 12V";
    constexpr const char *FRIENDLY_STUDY_ROOM_SUBPANEL_5V = "Study Room Subpanel 5V";

    // Gun Drawers
    constexpr const char *FRIENDLY_GUN_DRAWERS_24V = "Gun Drawers 24V";
    constexpr const char *FRIENDLY_GUN_DRAWERS_12V = "Gun Drawers 12V";
    constexpr const char *FRIENDLY_GUN_DRAWERS_5V = "Gun Drawers 5V";

    // Keys
    constexpr const char *FRIENDLY_KEYS_5V = "Keys Puzzle 5V";

    // Empty
    constexpr const char *FRIENDLY_EMPTY_35 = "Reserved Relay 35";
    constexpr const char *FRIENDLY_EMPTY_34 = "Reserved Relay 34";

    // Controller
    constexpr const char *FRIENDLY_CONTROLLER = "Power Control - Lower Right";

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
