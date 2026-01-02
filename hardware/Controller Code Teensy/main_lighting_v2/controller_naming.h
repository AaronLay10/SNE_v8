#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Main Lighting Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "main_lighting"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Main Lighting Controller";

    // Devices
    constexpr const char *DEV_STUDY_LIGHTS = "study_lights";
    constexpr const char *DEV_BOILER_LIGHTS = "boiler_lights";
    constexpr const char *DEV_LAB_LIGHTS_SQUARES = "lab_lights_squares";
    constexpr const char *DEV_LAB_LIGHTS_GRATES = "lab_lights_grates";
    constexpr const char *DEV_SCONCES = "sconces";
    constexpr const char *DEV_CRAWLSPACE_LIGHTS = "crawlspace_lights";

    // Friendly names
    constexpr const char *FRIENDLY_STUDY_LIGHTS = "Study Lights (Analog Dimmer)";
    constexpr const char *FRIENDLY_BOILER_LIGHTS = "Boiler Room Lights (Analog Dimmer)";
    constexpr const char *FRIENDLY_LAB_LIGHTS_SQUARES = "Lab Lights (FastLED - Ceiling Squares)";
    constexpr const char *FRIENDLY_LAB_LIGHTS_GRATES = "Lab Lights (FastLED - Ceiling Grates)";
    constexpr const char *FRIENDLY_SCONCES = "Sconces (Digital Relay)";
    constexpr const char *FRIENDLY_CRAWLSPACE_LIGHTS = "Crawlspace Lights (Digital Relay)";

    // Commands (snake_case)
    constexpr const char *CMD_STUDY_SET_BRIGHTNESS = "set_brightness";
    constexpr const char *CMD_BOILER_SET_BRIGHTNESS = "set_brightness";
    constexpr const char *CMD_LAB_SET_SQUARES_BRIGHTNESS = "set_squares_brightness";
    constexpr const char *CMD_LAB_SET_SQUARES_COLOR = "set_squares_color";
    constexpr const char *CMD_LAB_SET_GRATES_BRIGHTNESS = "set_grates_brightness";
    constexpr const char *CMD_LAB_SET_GRATES_COLOR = "set_grates_color";
    constexpr const char *CMD_SCONCES_ON = "sconces_on";
    constexpr const char *CMD_SCONCES_OFF = "sconces_off";
    constexpr const char *CMD_CRAWLSPACE_ON = "crawlspace_on";
    constexpr const char *CMD_CRAWLSPACE_OFF = "crawlspace_off";

    // Friendly command names
    constexpr const char *FRIENDLY_CMD_SET_BRIGHTNESS = "Set Brightness";
    constexpr const char *FRIENDLY_CMD_SET_COLOR = "Set Color";
    constexpr const char *FRIENDLY_CMD_ON = "Turn On";
    constexpr const char *FRIENDLY_CMD_OFF = "Turn Off";

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
