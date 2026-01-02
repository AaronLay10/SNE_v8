#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Gauge 1-3-4 Controller v2 — Canonical naming
// Purpose: single source of truth for identifiers used in MQTT topics and manifests.

#include "FirmwareMetadata.h"

namespace naming
{
    // Tenant and room identifiers (snake_case, DB-sourced)
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";

    // Controller identifier: must match DB controllers.controller_id
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "gauge_1_3_4"

    // Friendly names (UI-only; never used in MQTT topics)
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Gauges 1-3-4 Controller";

    // Device identifiers (one physical unit = one device_id)
    constexpr const char *DEV_GAUGE_1 = "gauge_1";
    constexpr const char *DEV_GAUGE_3 = "gauge_3";
    constexpr const char *DEV_GAUGE_4 = "gauge_4";

    // Device friendly names (UI-only)
    constexpr const char *FRIENDLY_GAUGE_1 = "Gauge 1 PSI";
    constexpr const char *FRIENDLY_GAUGE_3 = "Gauge 3 PSI";
    constexpr const char *FRIENDLY_GAUGE_4 = "Gauge 4 PSI";

    // Command slugs (snake_case)
    // Gauge commands (apply to all gauges)
    constexpr const char *CMD_ACTIVATE_GAUGES = "activate_gauges";
    constexpr const char *CMD_DEACTIVATE_GAUGES = "deactivate_gauges";

    // Calibration commands
    constexpr const char *CMD_ADJUST_GAUGE_ZERO = "adjust_gauge_zero";
    constexpr const char *CMD_SET_CURRENT_AS_ZERO = "set_current_as_zero";

    // Friendly names for commands (UI-only)
    constexpr const char *FRIENDLY_CMD_ACTIVATE_GAUGES = "Activate Gauges";
    constexpr const char *FRIENDLY_CMD_DEACTIVATE_GAUGES = "Deactivate Gauges";
    constexpr const char *FRIENDLY_CMD_ADJUST_GAUGE_ZERO = "Adjust Gauge Zero";
    constexpr const char *FRIENDLY_CMD_SET_CURRENT_AS_ZERO = "Set Current As Zero";

    // Sensor names
    constexpr const char *SENSOR_VALVE_1_PSI = "valve_1_psi";
    constexpr const char *SENSOR_VALVE_3_PSI = "valve_3_psi";
    constexpr const char *SENSOR_VALVE_4_PSI = "valve_4_psi";

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
