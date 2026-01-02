#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Gauge 6-Leds Controller v2 — Canonical naming
// Purpose: single source of truth for identifiers used in MQTT topics and manifests.

#include "FirmwareMetadata.h"

namespace naming
{
    // Tenant and room identifiers (snake_case, DB-sourced)
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";

    // Controller identifier: must match DB controllers.controller_id
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "gauge_6_leds"

    // Friendly names (UI-only; never used in MQTT topics)
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Gauge 6 & LED Controller";

    // Device identifiers (one physical unit = one device_id)
    constexpr const char *DEV_GAUGE_6 = "gauge_6";
    constexpr const char *DEV_LEVER_1_RED = "lever_1_red";
    constexpr const char *DEV_LEVER_2_BLUE = "lever_2_blue";
    constexpr const char *DEV_LEVER_3_GREEN = "lever_3_green";
    constexpr const char *DEV_LEVER_4_WHITE = "lever_4_white";
    constexpr const char *DEV_LEVER_5_ORANGE = "lever_5_orange";
    constexpr const char *DEV_LEVER_6_YELLOW = "lever_6_yellow";
    constexpr const char *DEV_LEVER_7_PURPLE = "lever_7_purple";
    constexpr const char *DEV_CEILING_LEDS = "ceiling_leds";
    constexpr const char *DEV_GAUGE_LEDS = "gauge_leds";

    // Device friendly names (UI-only)
    constexpr const char *FRIENDLY_GAUGE_6 = "Gauge 6 (Valve + Motor)";
    constexpr const char *FRIENDLY_LEVER_1_RED = "Ball Valve Lever 1 (Red)";
    constexpr const char *FRIENDLY_LEVER_2_BLUE = "Ball Valve Lever 2 (Blue)";
    constexpr const char *FRIENDLY_LEVER_3_GREEN = "Ball Valve Lever 3 (Green)";
    constexpr const char *FRIENDLY_LEVER_4_WHITE = "Ball Valve Lever 4 (White)";
    constexpr const char *FRIENDLY_LEVER_5_ORANGE = "Ball Valve Lever 5 (Orange)";
    constexpr const char *FRIENDLY_LEVER_6_YELLOW = "Ball Valve Lever 6 (Yellow)";
    constexpr const char *FRIENDLY_LEVER_7_PURPLE = "Ball Valve Lever 7 (Purple)";
    constexpr const char *FRIENDLY_CEILING_LEDS = "Ceiling Clock LEDs (219 WS2811)";
    constexpr const char *FRIENDLY_GAUGE_LEDS = "Gauge LEDs (7 WS2812B)";

    // Command slugs (snake_case)
    // Gauge 6 commands
    constexpr const char *CMD_ACTIVATE_GAUGES = "activate_gauges";
    constexpr const char *CMD_DEACTIVATE_GAUGES = "deactivate_gauges";
    // Ceiling LED commands
    constexpr const char *CMD_CEILING_OFF = "ceiling_off";
    constexpr const char *CMD_CEILING_PATTERN_1 = "ceiling_pattern_1";
    constexpr const char *CMD_CEILING_PATTERN_2 = "ceiling_pattern_2";
    constexpr const char *CMD_CEILING_PATTERN_3 = "ceiling_pattern_3";
    // Gauge Indicator LED commands
    constexpr const char *CMD_FLICKER_OFF = "flicker_off";
    constexpr const char *CMD_FLICKER_MODE_2 = "flicker_mode_1";
    constexpr const char *CMD_FLICKER_MODE_5 = "flicker_mode_2";
    constexpr const char *CMD_FLICKER_MODE_8 = "flicker_mode_3";
    constexpr const char *CMD_GAUGE_LEDS_ON = "gauge_leds_on";
    constexpr const char *CMD_GAUGE_LEDS_OFF = "gauge_leds_off";
    // Calibration commands
    constexpr const char *CMD_ADJUST_GAUGE_ZERO = "adjust_gauge_zero";
    constexpr const char *CMD_SET_CURRENT_AS_ZERO = "set_current_as_zero";

    // Friendly names for commands (UI-only)
    constexpr const char *FRIENDLY_CMD_ACTIVATE_GAUGES = "Activate Gauges";
    constexpr const char *FRIENDLY_CMD_DEACTIVATE_GAUGES = "Deactivate Gauges";
    constexpr const char *FRIENDLY_CMD_CEILING_OFF = "Ceiling Off";
    constexpr const char *FRIENDLY_CMD_CEILING_PATTERN_1 = "Ceiling Pattern 1";
    constexpr const char *FRIENDLY_CMD_CEILING_PATTERN_2 = "Ceiling Pattern 2";
    constexpr const char *FRIENDLY_CMD_CEILING_PATTERN_3 = "Ceiling Pattern 3";
    constexpr const char *FRIENDLY_CMD_FLICKER_OFF = "Flicker Off";
    constexpr const char *FRIENDLY_CMD_FLICKER_MODE_1 = "Flicker Mode 1";
    constexpr const char *FRIENDLY_CMD_FLICKER_MODE_2 = "Flicker Mode 2";
    constexpr const char *FRIENDLY_CMD_FLICKER_MODE_3 = "Flicker Mode 3";
    constexpr const char *FRIENDLY_CMD_GAUGE_LEDS_ON = "Gauge LEDs On";
    constexpr const char *FRIENDLY_CMD_GAUGE_LEDS_OFF = "Gauge LEDs Off";
    constexpr const char *FRIENDLY_CMD_ADJUST_GAUGE_ZERO = "Adjust Gauge Zero";
    constexpr const char *FRIENDLY_CMD_SET_CURRENT_AS_ZERO = "Set Current As Zero";

    // Sensor names
    constexpr const char *SENSOR_VALVE_6_PSI = "valve_6_psi";

    // Categories (fixed, lowercase)
    constexpr const char *CAT_COMMANDS = "commands";
    constexpr const char *CAT_SENSORS = "sensors";
    constexpr const char *CAT_STATUS = "status";
    constexpr const char *CAT_EVENTS = "events";
    constexpr const char *CAT_ACKNOWLEDGEMENT = "acknowledgement";
    constexpr const char *ITEM_HEARTBEAT = "heartbeat";
    constexpr const char *ITEM_HARDWARE = "hardware";
    constexpr const char *ITEM_COMMAND_ACK = "command_ack";
} // namespace naming

#endif // CONTROLLER_NAMING_H
