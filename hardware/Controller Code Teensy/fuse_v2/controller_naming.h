#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Fuse Box Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "fuse"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Fuse Box Controller";

    // RFID Readers (sensors only - input devices)
    constexpr const char *DEV_RFID_A = "rfid_a";
    constexpr const char *DEV_RFID_B = "rfid_b";
    constexpr const char *DEV_RFID_C = "rfid_c";
    constexpr const char *DEV_RFID_D = "rfid_d";
    constexpr const char *DEV_RFID_E = "rfid_e";

    constexpr const char *FRIENDLY_RFID_A = "RFID Reader A";
    constexpr const char *FRIENDLY_RFID_B = "RFID Reader B";
    constexpr const char *FRIENDLY_RFID_C = "RFID Reader C";
    constexpr const char *FRIENDLY_RFID_D = "RFID Reader D";
    constexpr const char *FRIENDLY_RFID_E = "RFID Reader E";

    // Resistor/Fuse sensors (input devices)
    constexpr const char *DEV_FUSE_A = "fuse_a";
    constexpr const char *DEV_FUSE_B = "fuse_b";
    constexpr const char *DEV_FUSE_C = "fuse_c";

    constexpr const char *FRIENDLY_FUSE_A = "Main Fuse A";
    constexpr const char *FRIENDLY_FUSE_B = "Main Fuse B";
    constexpr const char *FRIENDLY_FUSE_C = "Main Fuse C";

    // Knife switch sensor (input device)
    constexpr const char *DEV_KNIFE_SWITCH = "knife_switch";
    constexpr const char *FRIENDLY_KNIFE_SWITCH = "Knife Switch";

    // Actuator (output device)
    constexpr const char *DEV_ACTUATOR = "actuator";
    constexpr const char *FRIENDLY_ACTUATOR = "Actuator";

    // Maglocks (output devices)
    constexpr const char *DEV_MAGLOCK_B = "maglock_b";
    constexpr const char *DEV_MAGLOCK_C = "maglock_c";
    constexpr const char *DEV_MAGLOCK_D = "maglock_d";

    constexpr const char *FRIENDLY_MAGLOCK_B = "Maglock B";
    constexpr const char *FRIENDLY_MAGLOCK_C = "Maglock C";
    constexpr const char *FRIENDLY_MAGLOCK_D = "Maglock D";

    // Metal gate (output device)
    constexpr const char *DEV_METAL_GATE = "metal_gate";
    constexpr const char *FRIENDLY_METAL_GATE = "Metal Gate";

    // Commands
    constexpr const char *CMD_ACTUATOR_FORWARD = "actuator_forward";
    constexpr const char *CMD_ACTUATOR_REVERSE = "actuator_reverse";
    constexpr const char *CMD_ACTUATOR_STOP = "actuator_stop";
    constexpr const char *CMD_DROP_PANEL = "drop_panel";
    constexpr const char *CMD_UNLOCK_GATE = "unlock_gate";

    constexpr const char *FRIENDLY_CMD_ACTUATOR_FORWARD = "Actuator Forward";
    constexpr const char *FRIENDLY_CMD_ACTUATOR_REVERSE = "Actuator Reverse";
    constexpr const char *FRIENDLY_CMD_ACTUATOR_STOP = "Actuator Stop";
    constexpr const char *FRIENDLY_CMD_DROP_PANEL = "Drop Panel";
    constexpr const char *FRIENDLY_CMD_UNLOCK_GATE = "Unlock Metal Gate";

    // Sensors
    constexpr const char *SENSOR_RFID_TAG = "rfid_tag";
    constexpr const char *SENSOR_RESISTOR_VALUE = "resistor_value";
    constexpr const char *SENSOR_SWITCH_STATE = "switch_state";

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
