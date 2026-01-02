#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Chemical Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "chemical"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Chemical Puzzle Controller";

    // Devices - 6 RFID readers + actuator + maglock
    constexpr const char *DEV_RFID_A = "rfid_a";
    constexpr const char *DEV_RFID_B = "rfid_b";
    constexpr const char *DEV_RFID_C = "rfid_c";
    constexpr const char *DEV_RFID_D = "rfid_d";
    constexpr const char *DEV_RFID_E = "rfid_e";
    constexpr const char *DEV_RFID_F = "rfid_f";
    constexpr const char *DEV_ACTUATOR = "engine_block_actuator";
    constexpr const char *DEV_MAGLOCKS = "chest_maglocks";

    // Friendly names
    constexpr const char *FRIENDLY_RFID_A = "RFID Reader A";
    constexpr const char *FRIENDLY_RFID_B = "RFID Reader B";
    constexpr const char *FRIENDLY_RFID_C = "RFID Reader C";
    constexpr const char *FRIENDLY_RFID_D = "RFID Reader D";
    constexpr const char *FRIENDLY_RFID_E = "RFID Reader E";
    constexpr const char *FRIENDLY_RFID_F = "RFID Reader F";
    constexpr const char *FRIENDLY_ACTUATOR = "Engine Block Actuator";
    constexpr const char *FRIENDLY_MAGLOCKS = "Chest Maglocks";

    // Commands
    constexpr const char *CMD_ACTUATOR_FORWARD = "actuator_up";
    constexpr const char *CMD_ACTUATOR_REVERSE = "actuator_down";
    constexpr const char *CMD_ACTUATOR_STOP = "actuator_stop";
    constexpr const char *CMD_MAGLOCKS_LOCK = "lock";
    constexpr const char *CMD_MAGLOCKS_UNLOCK = "unlock";

    // Friendly command names
    constexpr const char *FRIENDLY_CMD_ACTUATOR_FORWARD = "Actuator Up";
    constexpr const char *FRIENDLY_CMD_ACTUATOR_REVERSE = "Actuator Down";
    constexpr const char *FRIENDLY_CMD_ACTUATOR_STOP = "Actuator Stop";
    constexpr const char *FRIENDLY_CMD_LOCK = "Lock";
    constexpr const char *FRIENDLY_CMD_UNLOCK = "Unlock";

    // Sensors
    constexpr const char *SENSOR_RFID_TAG_A = "rfid_tag_a";
    constexpr const char *SENSOR_RFID_TAG_B = "rfid_tag_b";
    constexpr const char *SENSOR_RFID_TAG_C = "rfid_tag_c";
    constexpr const char *SENSOR_RFID_TAG_D = "rfid_tag_d";
    constexpr const char *SENSOR_RFID_TAG_E = "rfid_tag_e";
    constexpr const char *SENSOR_RFID_TAG_F = "rfid_tag_f";
    constexpr const char *SENSOR_TIR_A = "tag_in_range_a";
    constexpr const char *SENSOR_TIR_B = "tag_in_range_b";
    constexpr const char *SENSOR_TIR_C = "tag_in_range_c";
    constexpr const char *SENSOR_TIR_D = "tag_in_range_d";
    constexpr const char *SENSOR_TIR_E = "tag_in_range_e";
    constexpr const char *SENSOR_TIR_F = "tag_in_range_f";

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
