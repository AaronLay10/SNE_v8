#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Gun Drawers Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "gun_drawers"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Gun Drawers Controller";

    // Drawer devices (electromagnets)
    constexpr const char *DEV_DRAWER_ELEGANT = "drawer_elegant";
    constexpr const char *DEV_DRAWER_ALCHEMIST = "drawer_alchemist";
    constexpr const char *DEV_DRAWER_BOUNTY = "drawer_bounty";
    constexpr const char *DEV_DRAWER_MECHANIC = "drawer_mechanic";

    constexpr const char *FRIENDLY_DRAWER_ELEGANT = "Elegant Drawer";
    constexpr const char *FRIENDLY_DRAWER_ALCHEMIST = "Alchemist Drawer";
    constexpr const char *FRIENDLY_DRAWER_BOUNTY = "Bounty Drawer";
    constexpr const char *FRIENDLY_DRAWER_MECHANIC = "Mechanic Drawer";

    // Commands
    constexpr const char *CMD_RELEASE_DRAWER = "release_drawer";
    constexpr const char *CMD_LOCK_DRAWER = "lock_drawer";
    constexpr const char *CMD_RELEASE_ALL = "release_all_drawers";

    constexpr const char *FRIENDLY_CMD_RELEASE_DRAWER = "Release Drawer";
    constexpr const char *FRIENDLY_CMD_LOCK_DRAWER = "Lock Drawer";
    constexpr const char *FRIENDLY_CMD_RELEASE_ALL = "Release All Drawers";

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
