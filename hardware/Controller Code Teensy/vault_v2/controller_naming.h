#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Vault Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "vault"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Vault Puzzle Controller";

    // RFID Reader (sensor only)
    constexpr const char *DEV_RFID_READER = "rfid_reader";
    constexpr const char *FRIENDLY_RFID_READER = "RFID Reader";

    // Sensors
    constexpr const char *SENSOR_VAULT_NUMBER = "vault_number";
    constexpr const char *SENSOR_TAG_ID = "tag_id";

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
