#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Lever Boiler Controller v2 — Canonical naming
// Purpose: single source of truth for identifiers used in MQTT topics and manifests.

#include "FirmwareMetadata.h"

namespace naming
{
    // Tenant and room identifiers (snake_case, DB-sourced)
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";

    // Controller identifier: must match DB controllers.controller_id
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "lever_boiler"

    // Friendly names (UI-only; never used in MQTT topics)
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Lever Boiler Controller";

    // Device identifiers (one physical unit = one device_id)
    constexpr const char *DEV_LEVER_BOILER = "lever_boiler";
    constexpr const char *DEV_LEVER_STAIRS = "lever_stairs";
    constexpr const char *DEV_NEWELL_POST = "newell_post";

    // Device friendly names (UI-only)
    constexpr const char *FRIENDLY_LEVER_BOILER = "Boiler Lever";
    constexpr const char *FRIENDLY_LEVER_STAIRS = "Stairs Lever";
    constexpr const char *FRIENDLY_NEWELL_POST = "Newell Post";

    // Lever Boiler and Lever Stairs Explanation - Each of these devices have a Photocell Sensor, IR Sensor, Maglock, and LED associated with them.

    // Command slugs (snake_case)
    // Lever Boiler commands
    constexpr const char *CMD_MAGLOCK_BOILER_UNLOCK = "unlock";
    constexpr const char *CMD_MAGLOCK_BOILER_LOCK = "lock";
    constexpr const char *CMD_LEVER_LED_BOILER_ON = "led_on";
    constexpr const char *CMD_LEVER_LED_BOILER_OFF = "led_off";

    // Lever Stairs commands
    constexpr const char *CMD_MAGLOCK_STAIRS_UNLOCK = "unlock";
    constexpr const char *CMD_MAGLOCK_STAIRS_LOCK = "lock";
    constexpr const char *CMD_LEVER_LED_STAIRS_ON = "led_on";
    constexpr const char *CMD_LEVER_LED_STAIRS_OFF = "led_off";

    // Newell Post commands
    constexpr const char *CMD_NEWELL_LIGHT_ON = "light_on";
    constexpr const char *CMD_NEWELL_LIGHT_OFF = "light_off";
    constexpr const char *CMD_STEPPER_UP = "stepper_up";
    constexpr const char *CMD_STEPPER_DOWN = "stepper_down";
    constexpr const char *CMD_STEPPER_STOP = "stepper_stop";

    // Friendly names for commands (UI-only)
    constexpr const char *FRIENDLY_CMD_BOILER_MAGLOCK_UNLOCK = "Maglock Unlock";
    constexpr const char *FRIENDLY_CMD_BOILER_MAGLOCK_LOCK = "Maglock Lock";
    constexpr const char *FRIENDLY_CMD_BOILER_LED_ON = "Light On";
    constexpr const char *FRIENDLY_CMD_BOILER_LED_OFF = "Light Off";
    constexpr const char *FRIENDLY_CMD_STAIRS_LED_ON = "Light On";
    constexpr const char *FRIENDLY_CMD_STAIRS_LED_OFF = "Light Off";
    constexpr const char *FRIENDLY_CMD_NEWELL_LIGHT_ON = "Light On";
    constexpr const char *FRIENDLY_CMD_NEWELL_LIGHT_OFF = "Light Off";
    constexpr const char *FRIENDLY_CMD_STEPPER_UP = "Lift Up";
    constexpr const char *FRIENDLY_CMD_STEPPER_DOWN = "Lower Down";
    constexpr const char *FRIENDLY_CMD_STEPPER_STOP = "Motor Stop";

    // Sensor names
    constexpr const char *SENSOR_BOILER_PHOTOCELL = "boiler_photocell";
    constexpr const char *SENSOR_BOILER_IR_CODE = "boiler_ir_code";
    constexpr const char *SENSOR_STAIRS_PHOTOCELL = "stairs_photocell";
    constexpr const char *SENSOR_STAIRS_IR_CODE = "stairs_ir_code";
    constexpr const char *SENSOR_NEWELL_POST_TOP_PROXIMITY = "newell_post_proximity_top";
    constexpr const char *SENSOR_NEWELL_POST_BOTTOM_PROXIMITY = "newell_post_proximity_bottom";

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
