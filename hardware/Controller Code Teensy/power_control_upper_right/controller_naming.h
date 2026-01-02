#ifndef CONTROLLER_NAMING_H
#define CONTROLLER_NAMING_H

// Power Control Upper Right v2 — Canonical naming
// Purpose: single source of truth for identifiers used in MQTT topics and manifests.

#include "FirmwareMetadata.h" // Provides firmware::UNIQUE_ID (must equal controller_id)

namespace naming
{
    // Tenant and room identifiers (snake_case, DB-sourced)
    constexpr const char *CLIENT_ID = "paragon";
    constexpr const char *ROOM_ID = "clockwork";

    // Controller identifier: must match DB controllers.controller_id
    constexpr const char *CONTROLLER_ID = firmware::UNIQUE_ID; // "power_control_upper_right"
    constexpr const char *CONTROLLER_FRIENDLY_NAME = "Power Control - Upper Right";

    // ========================================================================
    // DEVICE IDENTIFIERS (snake_case, one per physical relay/device)
    // ========================================================================

    // Main Lighting Power Rails
    constexpr const char *DEV_MAIN_LIGHTING_24V = "main_lighting_24v";
    constexpr const char *DEV_MAIN_LIGHTING_12V = "main_lighting_12v";
    constexpr const char *DEV_MAIN_LIGHTING_5V = "main_lighting_5v";

    // Gauge Cluster Power Rails
    constexpr const char *DEV_GAUGES_12V_A = "gauges_12v_a";
    constexpr const char *DEV_GAUGES_12V_B = "gauges_12v_b";
    constexpr const char *DEV_GAUGES_5V = "gauges_5v";

    // Lever Boiler Power Rails
    constexpr const char *DEV_LEVER_BOILER_5V = "lever_boiler_5v";
    constexpr const char *DEV_LEVER_BOILER_12V = "lever_boiler_12v";

    // Pilot Light Power
    constexpr const char *DEV_PILOT_LIGHT_5V = "pilot_light_5v";

    // Kraken Controls Power
    constexpr const char *DEV_KRAKEN_CONTROLS_5V = "kraken_controls_5v";

    // Fuse Puzzle Power Rails
    constexpr const char *DEV_FUSE_12V = "fuse_12v";
    constexpr const char *DEV_FUSE_5V = "fuse_5v";

    // Syringe Puzzle Power Rails
    constexpr const char *DEV_SYRINGE_24V = "syringe_24v";
    constexpr const char *DEV_SYRINGE_12V = "syringe_12v";
    constexpr const char *DEV_SYRINGE_5V = "syringe_5v";

    // Chemical Puzzle Power Rails
    constexpr const char *DEV_CHEMICAL_24V = "chemical_24v";
    constexpr const char *DEV_CHEMICAL_12V = "chemical_12v";
    constexpr const char *DEV_CHEMICAL_5V = "chemical_5v";

    // Special Effects Power
    constexpr const char *DEV_CRAWL_SPACE_BLACKLIGHT = "crawl_space_blacklight";
    constexpr const char *DEV_FLOOR_AUDIO_AMP = "floor_audio_amp";
    constexpr const char *DEV_KRAKEN_RADAR_AMP = "kraken_radar_amp";

    // Vault Power Rails
    constexpr const char *DEV_VAULT_24V = "vault_24v";
    constexpr const char *DEV_VAULT_12V = "vault_12v";
    constexpr const char *DEV_VAULT_5V = "vault_5v";

    // Controller virtual device
    constexpr const char *DEV_CONTROLLER = "controller";

    // ========================================================================
    // FRIENDLY NAMES (UI-only, never in MQTT topics)
    // ========================================================================

    // Main Lighting
    constexpr const char *FRIENDLY_MAIN_LIGHTING_24V = "Main Lighting 24V";
    constexpr const char *FRIENDLY_MAIN_LIGHTING_12V = "Main Lighting 12V";
    constexpr const char *FRIENDLY_MAIN_LIGHTING_5V = "Main Lighting 5V";

    // Gauges
    constexpr const char *FRIENDLY_GAUGES_12V_A = "Gauges 12V A";
    constexpr const char *FRIENDLY_GAUGES_12V_B = "Gauges 12V B";
    constexpr const char *FRIENDLY_GAUGES_5V = "Gauges 5V";

    // Lever Boiler
    constexpr const char *FRIENDLY_LEVER_BOILER_5V = "Lever Boiler 5V";
    constexpr const char *FRIENDLY_LEVER_BOILER_12V = "Lever Boiler 12V";

    // Pilot Light
    constexpr const char *FRIENDLY_PILOT_LIGHT_5V = "Pilot Light 5V";

    // Kraken Controls
    constexpr const char *FRIENDLY_KRAKEN_CONTROLS_5V = "Kraken Controls 5V";

    // Fuse
    constexpr const char *FRIENDLY_FUSE_12V = "Fuse Puzzle 12V";
    constexpr const char *FRIENDLY_FUSE_5V = "Fuse Puzzle 5V";

    // Syringe
    constexpr const char *FRIENDLY_SYRINGE_24V = "Syringe Puzzle 24V";
    constexpr const char *FRIENDLY_SYRINGE_12V = "Syringe Puzzle 12V";
    constexpr const char *FRIENDLY_SYRINGE_5V = "Syringe Puzzle 5V";

    // Chemical
    constexpr const char *FRIENDLY_CHEMICAL_24V = "Chemical Puzzle 24V";
    constexpr const char *FRIENDLY_CHEMICAL_12V = "Chemical Puzzle 12V";
    constexpr const char *FRIENDLY_CHEMICAL_5V = "Chemical Puzzle 5V";

    // Special Effects
    constexpr const char *FRIENDLY_CRAWL_SPACE_BLACKLIGHT = "Crawl Space Blacklight";
    constexpr const char *FRIENDLY_FLOOR_AUDIO_AMP = "Floor Audio Amplifier";
    constexpr const char *FRIENDLY_KRAKEN_RADAR_AMP = "Kraken Radar Amplifier";

    // Vault
    constexpr const char *FRIENDLY_VAULT_24V = "Vault 24V";
    constexpr const char *FRIENDLY_VAULT_12V = "Vault 12V";
    constexpr const char *FRIENDLY_VAULT_5V = "Vault 5V";

    // Controller
    constexpr const char *FRIENDLY_CONTROLLER = "Power Control - Upper Right";

    // ========================================================================
    // COMMAND SLUGS (snake_case, standardized on/off pattern)
    // ========================================================================

    constexpr const char *CMD_POWER_ON = "power_on";
    constexpr const char *CMD_POWER_OFF = "power_off";

    // Controller-level commands
    constexpr const char *CMD_ALL_ON = "all_on";
    constexpr const char *CMD_ALL_OFF = "all_off";
    constexpr const char *CMD_EMERGENCY_OFF = "emergency_off";
    constexpr const char *CMD_RESET = "reset";
    constexpr const char *CMD_REQUEST_STATUS = "request_status";

    // Friendly names for commands (UI-only)
    constexpr const char *FRIENDLY_CMD_POWER_ON = "Power On";
    constexpr const char *FRIENDLY_CMD_POWER_OFF = "Power Off";
    constexpr const char *FRIENDLY_CMD_ALL_ON = "All Devices On";
    constexpr const char *FRIENDLY_CMD_ALL_OFF = "All Devices Off";
    constexpr const char *FRIENDLY_CMD_EMERGENCY_OFF = "Emergency Power Off";
    constexpr const char *FRIENDLY_CMD_RESET = "Reset Controller";
    constexpr const char *FRIENDLY_CMD_REQUEST_STATUS = "Request Status";

    // ========================================================================
    // CATEGORIES (fixed, lowercase)
    // ========================================================================

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
