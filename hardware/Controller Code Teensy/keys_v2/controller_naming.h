#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Keys Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "keys"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Keys Puzzle Controller";

    // Device identifiers (one physical unit = one device_id)
    // One panel with 8 physical key switches arranged as 4 color pairs
    constexpr const char *DEV_BLUE_KEY_BOX = "blue_key_box";
    constexpr const char *DEV_GREEN_KEY_BOX = "green_key_box";
    constexpr const char *DEV_YELLOW_KEY_BOX = "yellow_key_box";
    constexpr const char *DEV_RED_KEY_BOX = "red_key_box";

    // freindly names for devices (UI-only)
    constexpr const char *FRIENDLY_BLUE_KEY_BOX = "Blue Key Box";
    constexpr const char *FRIENDLY_GREEN_KEY_BOX = "Green Key Box";
    constexpr const char *FRIENDLY_YELLOW_KEY_BOX = "Yellow Key Box";
    constexpr const char *FRIENDLY_RED_KEY_BOX = "Red Key Box";

    // Sensors (published by firmware)
    // Report whether each color pair is simultaneously pressed (1/0)
    constexpr const char *SENSOR_GREEN_PAIR = "green_pair";
    constexpr const char *SENSOR_YELLOW_PAIR = "yellow_pair";
    constexpr const char *SENSOR_BLUE_PAIR = "blue_pair";
    constexpr const char *SENSOR_RED_PAIR = "red_pair";

    // Individual switch states
    constexpr const char *SENSOR_GREEN_BOTTOM = "green_bottom";
    constexpr const char *SENSOR_GREEN_RIGHT = "green_right";
    constexpr const char *SENSOR_YELLOW_RIGHT = "yellow_right";
    constexpr const char *SENSOR_YELLOW_TOP = "yellow_top";
    constexpr const char *SENSOR_BLUE_LEFT = "blue_left";
    constexpr const char *SENSOR_BLUE_BOTTOM = "blue_bottom";
    constexpr const char *SENSOR_RED_LEFT = "red_left";
    constexpr const char *SENSOR_RED_BOTTOM = "red_bottom";

    // Friendly names for sensors (UI-only)
    constexpr const char *FRIENDLY_SENSOR_GREEN_PAIR = "Green Key Pair";
    constexpr const char *FRIENDLY_SENSOR_YELLOW_PAIR = "Yellow Key Pair";
    constexpr const char *FRIENDLY_SENSOR_BLUE_PAIR = "Blue Key Pair";
    constexpr const char *FRIENDLY_SENSOR_RED_PAIR = "Red Key Pair";
    constexpr const char *FRIENDLY_SENSOR_GREEN_BOTTOM = "Green Key Bottom";
    constexpr const char *FRIENDLY_SENSOR_GREEN_RIGHT = "Green Key Right";
    constexpr const char *FRIENDLY_SENSOR_YELLOW_RIGHT = "Yellow Key Right";
    constexpr const char *FRIENDLY_SENSOR_YELLOW_TOP = "Yellow Key Top";
    constexpr const char *FRIENDLY_SENSOR_BLUE_LEFT = "Blue Key Left";
    constexpr const char *FRIENDLY_SENSOR_BLUE_BOTTOM = "Blue Key Bottom";
    constexpr const char *FRIENDLY_SENSOR_RED_LEFT = "Red Key Left";
    constexpr const char *FRIENDLY_SENSOR_RED_BOTTOM = "Red Key Bottom";

    // Commands to control box LEDs
    // Blue Box LED
    constexpr const char *CMD_BLUE_BOX_LED_ON = "blue_box_led_on";
    constexpr const char *CMD_BLUE_BOX_LED_OFF = "blue_box_led_off";
    constexpr const char *CMD_BLUE_BOX_COLOR = "blue_box_color"; // e.g., "set_color"
    // Green Box LED
    constexpr const char *CMD_GREEN_BOX_LED_ON = "green_box_led_on";
    constexpr const char *CMD_GREEN_BOX_LED_OFF = "green_box_led_off";
    constexpr const char *CMD_GREEN_BOX_COLOR = "green_box_color"; // e.g., "set_color"
    // Yellow Box LED
    constexpr const char *CMD_YELLOW_BOX_LED_ON = "yellow_box_led_on";
    constexpr const char *CMD_YELLOW_BOX_LED_OFF = "yellow_box_led_off";
    constexpr const char *CMD_YELLOW_BOX_COLOR = "yellow_box_color"; // e.g., "set_color"
    // Red Box LED
    constexpr const char *CMD_RED_BOX_LED_ON = "red_box_led_on";
    constexpr const char *CMD_RED_BOX_LED_OFF = "red_box_led_off";
    constexpr const char *CMD_RED_BOX_COLOR = "red_box_color"; // e.g., "set_color"
    // ALL Boxes LED
    constexpr const char *CMD_PANEL_LEDS_ON = "panel_leds_on";
    constexpr const char *CMD_PANEL_LEDS_OFF = "panel_leds_off";

    // Friendly command names
    // Blue Box LED - The boxes have a default hardcoded color, but can be changed via command
    constexpr const char *FRIENDLY_CMD_BLUE_LEDS_ON = "Blue BOX LEDs On";
    constexpr const char *FRIENDLY_CMD_BLUE_LEDS_OFF = "Blue BOX LEDs Off";
    constexpr const char *FRIENDLY_CMD_BLUE_BOX_COLOR = "Blue BOX Color - Set Color";
    // Green Box LED
    constexpr const char *FRIENDLY_CMD_GREEN_LEDS_ON = "Green BOX LEDs On";
    constexpr const char *FRIENDLY_CMD_GREEN_LEDS_OFF = "Green BOX LEDs Off";
    constexpr const char *FRIENDLY_CMD_GREEN_BOX_COLOR = "Green BOX Color - Set Color";
    // Yellow Box LED
    constexpr const char *FRIENDLY_CMD_YELLOW_LEDS_ON = "Yellow BOX LEDs On";
    constexpr const char *FRIENDLY_CMD_YELLOW_LEDS_OFF = "Yellow BOX LEDs Off";
    constexpr const char *FRIENDLY_CMD_YELLOW_BOX_COLOR = "Yellow BOX Color - Set Color";
    // Red Box LED
    constexpr const char *FRIENDLY_CMD_RED_LEDS_ON = "Red BOX LEDs On";
    constexpr const char *FRIENDLY_CMD_RED_LEDS_OFF = "Red BOX LEDs Off";
    constexpr const char *FRIENDLY_CMD_RED_BOX_COLOR = "Red BOX Color - Set Color";
    // ALL Boxes LED
    constexpr const char *FRIENDLY_CMD_BOX_LEDS_ON = "BOX LEDs On";
    constexpr const char *FRIENDLY_CMD_BOX_LEDS_OFF = "BOX LEDs Off";

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
