#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Study B Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "study_b"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Study B Puzzle Controller";

    // Device IDs
    constexpr const char *DEV_STUDY_FAN = "study_fan";
    constexpr const char *DEV_WALL_GEAR_1 = "wall_gear_1";
    constexpr const char *DEV_WALL_GEAR_2 = "wall_gear_2";
    constexpr const char *DEV_WALL_GEAR_3 = "wall_gear_3";
    constexpr const char *DEV_TV_1 = "tv_1";
    constexpr const char *DEV_TV_2 = "tv_2";
    constexpr const char *DEV_MAKSERVO = "makservo";
    constexpr const char *DEV_FOG_MACHINE = "fog_machine";
    constexpr const char *DEV_STUDY_FAN_LIGHT = "study_fan_light";
    constexpr const char *DEV_BLACKLIGHTS = "blacklights";
    constexpr const char *DEV_NIXIE_LEDS = "nixie_leds";

    // Commands
    constexpr const char *CMD_START = "start";
    constexpr const char *CMD_STOP = "stop";
    constexpr const char *CMD_SLOW = "slow";
    constexpr const char *CMD_FAST = "fast";
    constexpr const char *CMD_ON = "on";
    constexpr const char *CMD_OFF = "off";
    constexpr const char *CMD_FOG_TRIGGER = "trigger";

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
