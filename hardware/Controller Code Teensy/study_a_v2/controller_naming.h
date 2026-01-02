#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Study A Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "study_a"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Study A Puzzle Controller";

    // Device IDs
    constexpr const char *DEV_TENTACLE_MOVER_A = "tentacle_mover_a";
    constexpr const char *DEV_TENTACLE_MOVER_B = "tentacle_mover_b";
    constexpr const char *DEV_RIDDLE_MOTOR = "riddle_motor";
    constexpr const char *DEV_PORTHOLE_CONTROLLER = "porthole_controller";
    constexpr const char *DEV_TENTACLE_SENSORS = "tentacle_sensors";

    // Sensor types
    constexpr const char *SENSOR_PORTHOLE_A1 = "porthole_a1";
    constexpr const char *SENSOR_PORTHOLE_A2 = "porthole_a2";
    constexpr const char *SENSOR_PORTHOLE_B1 = "porthole_b1";
    constexpr const char *SENSOR_PORTHOLE_B2 = "porthole_b2";
    constexpr const char *SENSOR_PORTHOLE_C1 = "porthole_c1";
    constexpr const char *SENSOR_PORTHOLE_C2 = "porthole_c2";
    constexpr const char *SENSOR_TENTACLE_A1 = "tentacle_a1";
    constexpr const char *SENSOR_TENTACLE_A2 = "tentacle_a2";
    constexpr const char *SENSOR_TENTACLE_A3 = "tentacle_a3";
    constexpr const char *SENSOR_TENTACLE_A4 = "tentacle_a4";
    constexpr const char *SENSOR_TENTACLE_B1 = "tentacle_b1";
    constexpr const char *SENSOR_TENTACLE_B2 = "tentacle_b2";
    constexpr const char *SENSOR_TENTACLE_B3 = "tentacle_b3";
    constexpr const char *SENSOR_TENTACLE_B4 = "tentacle_b4";
    constexpr const char *SENSOR_TENTACLE_C1 = "tentacle_c1";
    constexpr const char *SENSOR_TENTACLE_C2 = "tentacle_c2";
    constexpr const char *SENSOR_TENTACLE_C3 = "tentacle_c3";
    constexpr const char *SENSOR_TENTACLE_C4 = "tentacle_c4";
    constexpr const char *SENSOR_TENTACLE_D1 = "tentacle_d1";
    constexpr const char *SENSOR_TENTACLE_D2 = "tentacle_d2";
    constexpr const char *SENSOR_TENTACLE_D3 = "tentacle_d3";
    constexpr const char *SENSOR_TENTACLE_D4 = "tentacle_d4";

    // Commands
    constexpr const char *CMD_UP = "up";
    constexpr const char *CMD_DOWN = "down";
    constexpr const char *CMD_STOP = "stop";
    constexpr const char *CMD_MOTOR_ON = "on";
    constexpr const char *CMD_MOTOR_OFF = "off";
    constexpr const char *CMD_OPEN = "open";
    constexpr const char *CMD_CLOSE = "close";

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
