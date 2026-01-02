#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Picture Frame LEDs Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "picture_frame_leds"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Picture Frame LEDs Controller";

    // TV LED devices (each TV has 8 strips)
    constexpr const char *DEV_TV_VINCENT = "tv_vincent";
    constexpr const char *DEV_TV_EDITH = "tv_edith";
    constexpr const char *DEV_TV_MAKS = "tv_maks";
    constexpr const char *DEV_TV_OLIVER = "tv_oliver";
    constexpr const char *DEV_ALL_TVS = "all_tvs";

    // Commands
    constexpr const char *CMD_POWER_ON = "power_on";
    constexpr const char *CMD_POWER_OFF = "power_off";
    constexpr const char *CMD_SET_COLOR = "set_color";
    constexpr const char *CMD_SET_BRIGHTNESS = "set_brightness";
    constexpr const char *CMD_FLICKER = "flicker";

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
