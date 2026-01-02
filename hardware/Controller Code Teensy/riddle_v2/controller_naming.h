#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Riddle Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "riddle"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Riddle Puzzle Controller";

    // ═══════════════════════════════════════════════════════════════════════════
    // DEVICE IDENTIFIERS
    // ═══════════════════════════════════════════════════════════════════════════

    // Door lift mechanism (stepper motors)
    constexpr const char *DEV_DOOR = "door_lift";
    constexpr const char *FRIENDLY_DOOR = "Door Lift Mechanism";

    // Lever maglock
    constexpr const char *DEV_MAGLOCK = "lever_maglock";
    constexpr const char *FRIENDLY_MAGLOCK = "Lever Maglock";

    // LED strip (knob puzzle feedback)
    constexpr const char *DEV_LED_STRIP = "knob_leds";
    constexpr const char *FRIENDLY_LED_STRIP = "Knob Puzzle LEDs";

    // Knob puzzle input (combines all 7 knobs as one logical device)
    constexpr const char *DEV_KNOBS = "knob_puzzle";
    constexpr const char *FRIENDLY_KNOBS = "Letter Knob Puzzle";

    // Clue selector buttons
    constexpr const char *DEV_BUTTONS = "clue_buttons";
    constexpr const char *FRIENDLY_BUTTONS = "Clue Selector Buttons";

    // ═══════════════════════════════════════════════════════════════════════════
    // COMMANDS - Door Lift
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *CMD_DOOR_LIFT = "door_lift";
    constexpr const char *CMD_DOOR_LOWER = "door_lower";
    constexpr const char *CMD_DOOR_STOP = "door_stop";

    // ═══════════════════════════════════════════════════════════════════════════
    // COMMANDS - Maglock
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *CMD_MAGLOCK_LOCK = "maglock_lock";
    constexpr const char *CMD_MAGLOCK_UNLOCK = "maglock_unlock";

    // ═══════════════════════════════════════════════════════════════════════════
    // COMMANDS - LED Strip
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *CMD_LEDS_ON = "leds_on";
    constexpr const char *CMD_LEDS_OFF = "leds_off";
    constexpr const char *CMD_LEDS_SET_BRIGHTNESS = "leds_brightness";

    // ═══════════════════════════════════════════════════════════════════════════
    // COMMANDS - Controller State
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *CMD_SET_STATE = "set_state";
    constexpr const char *CMD_RESET = "reset";

    // ═══════════════════════════════════════════════════════════════════════════
    // SENSORS - Door Position
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *SENSOR_DOOR_POSITION = "door_position";
    constexpr const char *SENSOR_ENDSTOP_UP_R = "endstop_up_right";
    constexpr const char *SENSOR_ENDSTOP_UP_L = "endstop_up_left";
    constexpr const char *SENSOR_ENDSTOP_DN_R = "endstop_down_right";
    constexpr const char *SENSOR_ENDSTOP_DN_L = "endstop_down_left";

    // ═══════════════════════════════════════════════════════════════════════════
    // SENSORS - Knob Puzzle
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *SENSOR_KNOB_STATE = "knob_state";
    constexpr const char *SENSOR_ACTIVE_CLUE = "active_clue";

    // ═══════════════════════════════════════════════════════════════════════════
    // SENSORS - Buttons
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *SENSOR_BUTTON_1 = "button_1";
    constexpr const char *SENSOR_BUTTON_2 = "button_2";
    constexpr const char *SENSOR_BUTTON_3 = "button_3";

    // ═══════════════════════════════════════════════════════════════════════════
    // SENSORS - Controller State
    // ═══════════════════════════════════════════════════════════════════════════
    constexpr const char *SENSOR_STATE = "puzzle_state";

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
