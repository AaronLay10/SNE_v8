#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Clock Puzzle Controller v2 — Canonical naming
#include "FirmwareMetadata.h"

namespace naming
{
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "clock"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Clock Puzzle Controller";

    // Devices
    constexpr const char *DEV_CLOCK_HOUR = "clock_hour_hand";
    constexpr const char *DEV_CLOCK_MINUTE = "clock_minute_hand";
    constexpr const char *DEV_CLOCK_GEARS = "clock_gears";
    constexpr const char *DEV_OPERATOR_RESISTORS = "operator_resistors";
    constexpr const char *DEV_CRANK_ENCODERS = "crank_encoders";
    constexpr const char *DEV_OPERATOR_MAGLOCK = "operator_maglock";
    constexpr const char *DEV_WOOD_DOOR_MAGLOCK = "wood_door_maglock";
    constexpr const char *DEV_METAL_DOOR_ACTUATOR = "metal_door_actuator";
    constexpr const char *DEV_EXIT_DOOR_ACTUATOR = "exit_door_actuator";
    constexpr const char *DEV_CLOCK_FOG = "clock_fog_machine";
    constexpr const char *DEV_BLACKLIGHT = "blacklight";
    constexpr const char *DEV_LASER = "laser_light";
    constexpr const char *DEV_LED_STRIPS = "led_strips";

    // Friendly names
    constexpr const char *FRIENDLY_CLOCK_HOUR = "Clock Hour Hand Stepper";
    constexpr const char *FRIENDLY_CLOCK_MINUTE = "Clock Minute Hand Stepper";
    constexpr const char *FRIENDLY_CLOCK_GEARS = "Clock Gears Stepper";
    constexpr const char *FRIENDLY_OPERATOR_RESISTORS = "Operator Puzzle Resistors (8x)";
    constexpr const char *FRIENDLY_CRANK_ENCODERS = "Crank Puzzle Encoders (3x)";
    constexpr const char *FRIENDLY_OPERATOR_MAGLOCK = "Operator Puzzle Maglock";
    constexpr const char *FRIENDLY_WOOD_DOOR_MAGLOCK = "Wood Door Maglock";
    constexpr const char *FRIENDLY_METAL_DOOR_ACTUATOR = "Metal Door Actuator";
    constexpr const char *FRIENDLY_EXIT_DOOR_ACTUATOR = "Exit Door Actuator";
    constexpr const char *FRIENDLY_CLOCK_FOG = "Clock Fog Machine";
    constexpr const char *FRIENDLY_BLACKLIGHT = "Blacklight";
    constexpr const char *FRIENDLY_LASER = "Laser Light";
    constexpr const char *FRIENDLY_LED_STRIPS = "LED Light Strips (4x)";

    // Commands (examples - adjust based on actual commands in code)
    constexpr const char *CMD_CLOCK_SET_TIME = "set_time";
    constexpr const char *CMD_ACTUATOR_FORWARD = "actuator_forward";
    constexpr const char *CMD_ACTUATOR_REVERSE = "actuator_reverse";
    constexpr const char *CMD_ACTUATOR_STOP = "actuator_stop";
    constexpr const char *CMD_FOG_ON = "fog_on";
    constexpr const char *CMD_FOG_OFF = "fog_off";
    constexpr const char *CMD_BLACKLIGHT_ON = "blacklight_on";
    constexpr const char *CMD_BLACKLIGHT_OFF = "blacklight_off";
    constexpr const char *CMD_LASER_ON = "laser_on";
    constexpr const char *CMD_LASER_OFF = "laser_off";
    constexpr const char *CMD_LED_PATTERN = "led_pattern";
    constexpr const char *CMD_MAGLOCK_LOCK = "lock";
    constexpr const char *CMD_MAGLOCK_UNLOCK = "unlock";

    // Friendly command names
    constexpr const char *FRIENDLY_CMD_SET_TIME = "Set Clock Time";
    constexpr const char *FRIENDLY_CMD_FORWARD = "Forward";
    constexpr const char *FRIENDLY_CMD_REVERSE = "Reverse";
    constexpr const char *FRIENDLY_CMD_STOP = "Stop";
    constexpr const char *FRIENDLY_CMD_ON = "Turn On";
    constexpr const char *FRIENDLY_CMD_OFF = "Turn Off";
    constexpr const char *FRIENDLY_CMD_LED_PATTERN = "Set LED Pattern";
    constexpr const char *FRIENDLY_CMD_LOCK = "Lock";
    constexpr const char *FRIENDLY_CMD_UNLOCK = "Unlock";

    // Sensors
    constexpr const char *SENSOR_RESISTOR_VALUE = "resistor_value";
    constexpr const char *SENSOR_ENCODER_COUNT = "encoder_count";
    constexpr const char *SENSOR_PROXIMITY = "proximity";

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
