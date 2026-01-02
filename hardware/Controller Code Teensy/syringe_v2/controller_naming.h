#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Syringe Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "syringe"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Syringe Puzzle Controller";

    // Encoder sensors (input only)
    constexpr const char *DEV_ENCODER_LT = "encoder_left_top";
    constexpr const char *DEV_ENCODER_LM = "encoder_left_middle";
    constexpr const char *DEV_ENCODER_LB = "encoder_left_bottom";
    constexpr const char *DEV_ENCODER_RT = "encoder_right_top";
    constexpr const char *DEV_ENCODER_RM = "encoder_right_middle";
    constexpr const char *DEV_ENCODER_RB = "encoder_right_bottom";

    // LED Rings (output devices)
    constexpr const char *DEV_LED_RING_A = "led_ring_a";
    constexpr const char *DEV_LED_RING_B = "led_ring_b";
    constexpr const char *DEV_LED_RING_C = "led_ring_c";
    constexpr const char *DEV_LED_RING_D = "led_ring_d";
    constexpr const char *DEV_LED_RING_E = "led_ring_e";
    constexpr const char *DEV_LED_RING_F = "led_ring_f";
    constexpr const char *DEV_FILAMENT_LED = "filament_led";

    // Actuators (output devices)
    constexpr const char *DEV_MAIN_ACTUATOR = "main_actuator";
    constexpr const char *DEV_FORGE_ACTUATOR = "forge_actuator";

    // Sensor types
    constexpr const char *SENSOR_ENCODER_COUNT = "encoder_count";

    // Commands
    constexpr const char *CMD_SET_COLOR = "set_color";
    constexpr const char *CMD_ACTUATOR_UP = "up";
    constexpr const char *CMD_ACTUATOR_DOWN = "down";
    constexpr const char *CMD_ACTUATOR_STOP = "stop";
    constexpr const char *CMD_FORGE_EXTEND = "extend";
    constexpr const char *CMD_FORGE_RETRACT = "retract";
    constexpr const char *CMD_LED_ON = "on";
    constexpr const char *CMD_LED_OFF = "off";

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
