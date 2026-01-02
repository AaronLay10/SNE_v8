#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Pilot Light v2 — Canonical naming
// Purpose: single source of truth for identifiers used in MQTT topics and manifests.
// This file does NOT change behavior until included by the sketch; safe to edit iteratively.

#include "FirmwareMetadata.h" // Provides firmware::UNIQUE_ID (must equal controller_id)

namespace naming
{
    // Tenant and room identifiers (snake_case, DB-sourced)
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";

    // Controller identifier: must match DB controllers.controller_id
    // By convention, this equals firmware::UNIQUE_ID
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "pilot_light"

    // Friendly names (UI-only; never used in MQTT topics)
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Pilot Light Controller";

    // Device identifiers (one physical unit = one device_id)
    // Use these in capability manifests and topic construction.
    constexpr const char *DEV_FIRE_LEDS = "boiler_fire_leds";
    constexpr const char *DEV_MONITOR_POWER_RELAY = "boiler_monitor_power_relay";
    constexpr const char *DEV_NEWELL_POWER_RELAY = "newell_power_relay";
    constexpr const char *DEV_FLANGE_LEDS = "flange_leds";
    constexpr const char *DEV_PILOTLIGHT_COLOR_SENSOR = "pilotlight_color_sensor";
    // Controller-scoped virtual device (for controller-level commands)
    constexpr const char *DEV_CONTROLLER = "controller";

    // Device friendly names (UI-only)
    constexpr const char *FRIENDLY_FIRE_LEDS = "Boiler Fire Animation";
    constexpr const char *FRIENDLY_MONITOR_RELAY = "Boiler Monitor Power";
    constexpr const char *FRIENDLY_NEWELL_RELAY = "Newell Post Power";
    constexpr const char *FRIENDLY_FLANGE_LEDS = "Flange Status LEDs";
    constexpr const char *FRIENDLY_COLOR_SENSOR = "Color Temperature Sensor";
    constexpr const char *FRIENDLY_CONTROLLER = "Pilot Light Controller";

    // Command slugs (snake_case)
    // Fire LEDs
    constexpr const char *CMD_FIRE_LEDS_ON = "fire_leds_on";
    constexpr const char *CMD_FIRE_LEDS_OFF = "fire_leds_off";
    // Monitor Relay
    constexpr const char *CMD_MONITOR_ON = "monitor_on";
    constexpr const char *CMD_MONITOR_OFF = "monitor_off";
    // Newell Power Relay
    constexpr const char *CMD_NEWELL_POWER_ON = "newell_power_on";
    constexpr const char *CMD_NEWELL_POWER_OFF = "newell_power_off";
    // Flange LEDs
    constexpr const char *CMD_FLANGE_ON = "flange_on";
    constexpr const char *CMD_FLANGE_OFF = "flange_off";
    // Controller-level commands
    constexpr const char *CMD_RESET = "reset";
    constexpr const char *CMD_REQUEST_STATUS = "request_status";

    // Friendly names for commands (UI-only; sent during registration)
    // Fire LEDs
    constexpr const char *FRIENDLY_CMD_FIRE_LEDS_ON = "Fire LEDs On";
    constexpr const char *FRIENDLY_CMD_FIRE_LEDS_OFF = "Fire LEDs Off";
    // Monitor Relay
    constexpr const char *FRIENDLY_CMD_MONITOR_ON = "Boiler Monitor On";
    constexpr const char *FRIENDLY_CMD_MONITOR_OFF = "Boiler Monitor Off";
    // Newell Power Relay
    constexpr const char *FRIENDLY_CMD_NEWELL_POWER_ON = "Newell Power On";
    constexpr const char *FRIENDLY_CMD_NEWELL_POWER_OFF = "Newell Power Off";
    // Flange LEDs
    constexpr const char *FRIENDLY_CMD_FLANGE_ON = "Flange LEDs On";
    constexpr const char *FRIENDLY_CMD_FLANGE_OFF = "Flange LEDs Off";
    // Controller-level commands
    constexpr const char *FRIENDLY_CMD_RESET = "Reset All Hardware";
    constexpr const char *FRIENDLY_CMD_REQUEST_STATUS = "Request Status";

    // Sensor names
    constexpr const char *SENSOR_COLOR_TEMP = "color_temperature";
    constexpr const char *SENSOR_LUX = "lux";

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
