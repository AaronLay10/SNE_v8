# Power Control Controllers - Pin Assignments

This document describes the pin assignments for all three power control Teensy 4.1 controllers in the Clockwork Corridor escape room.

---

## Overview

The power control system consists of three Teensy 4.1 microcontrollers that manage power distribution across the entire room. Each controller manages relay outputs that supply power to various puzzles, subpanels, and effects.

**Total Relays:** 54 (24 + 24 + 6)

---

## Controller 1: power_control_upper_right

**Controller ID:** `power_control_upper_right`  
**Relay Count:** 24  
**Firmware Version:** 2.0.1

### Pin Assignments

| Pin | Device ID              | Friendly Name          | Voltage |
| --- | ---------------------- | ---------------------- | ------- |
| 9   | main_lighting_24v      | Main Lighting 24V      | 24V     |
| 10  | main_lighting_12v      | Main Lighting 12V      | 12V     |
| 11  | main_lighting_5v       | Main Lighting 5V       | 5V      |
| 3   | gauges_12v_a           | Gauges 12V A           | 12V     |
| 4   | gauges_12v_b           | Gauges 12V B           | 12V     |
| 5   | gauges_5v              | Gauges 5V              | 5V      |
| 6   | lever_boiler_5v        | Lever Boiler 5V        | 5V      |
| 7   | lever_boiler_12v       | Lever Boiler 12V       | 12V     |
| 8   | pilot_light_5v         | Pilot Light 5V         | 5V      |
| 0   | kraken_controls_5v     | Kraken Controls 5V     | 5V      |
| 1   | fuse_12v               | Fuse Puzzle 12V        | 12V     |
| 2   | fuse_5v                | Fuse Puzzle 5V         | 5V      |
| 28  | syringe_24v            | Syringe Puzzle 24V     | 24V     |
| 27  | syringe_12v            | Syringe Puzzle 12V     | 12V     |
| 26  | syringe_5v             | Syringe Puzzle 5V      | 5V      |
| 25  | chemical_24v           | Chemical Puzzle 24V    | 24V     |
| 24  | chemical_12v           | Chemical Puzzle 12V    | 12V     |
| 12  | chemical_5v            | Chemical Puzzle 5V     | 5V      |
| 31  | crawl_space_blacklight | Crawl Space Blacklight | -       |
| 30  | floor_audio_amp        | Floor Audio Amplifier  | -       |
| 29  | kraken_radar_amp       | Kraken Radar Amplifier | -       |
| 33  | vault_24v              | Vault 24V              | 24V     |
| 34  | vault_12v              | Vault 12V              | 12V     |
| 32  | vault_5v               | Vault 5V               | 5V      |

### Device Groups

**Main Lighting:**

- Pins 9, 10, 11 (24V, 12V, 5V)

**Gauge Cluster:**

- Pins 3, 4, 5 (12V A, 12V B, 5V)

**Lever Boiler:**

- Pins 6, 7 (5V, 12V)

**Pilot Light:**

- Pin 8 (5V)

**Kraken Controls:**

- Pin 0 (5V)

**Fuse Puzzle:**

- Pins 1, 2 (12V, 5V)

**Syringe Puzzle:**

- Pins 28, 27, 26 (24V, 12V, 5V)

**Chemical Puzzle:**

- Pins 25, 24, 12 (24V, 12V, 5V)

**Special Effects:**

- Pins 31, 30, 29 (Blacklight, Audio Amp, Radar Amp)

**Vault:**

- Pins 33, 34, 32 (24V, 12V, 5V)

---

## Controller 2: power_control_lower_right

**Controller ID:** `power_control_lower_right`  
**Relay Count:** 24  
**Firmware Version:** 2.0.1

### Pin Assignments

| Pin | Device ID                | Friendly Name            | Voltage |
| --- | ------------------------ | ------------------------ | ------- |
| 11  | gear_24v                 | Gear Puzzle 24V          | 24V     |
| 10  | gear_12v                 | Gear Puzzle 12V          | 12V     |
| 9   | gear_5v                  | Gear Puzzle 5V           | 5V      |
| 8   | floor_24v                | Floor Puzzle 24V         | 24V     |
| 7   | floor_12v                | Floor Puzzle 12V         | 12V     |
| 6   | floor_5v                 | Floor Puzzle 5V          | 5V      |
| 5   | riddle_rpi_5v            | Riddle RPi 5V            | 5V      |
| 4   | riddle_rpi_12v           | Riddle RPi 12V           | 12V     |
| 3   | riddle_5v                | Riddle 5V                | 5V      |
| 1   | boiler_room_subpanel_24v | Boiler Room Subpanel 24V | 24V     |
| 0   | boiler_room_subpanel_12v | Boiler Room Subpanel 12V | 12V     |
| 2   | boiler_room_subpanel_5v  | Boiler Room Subpanel 5V  | 5V      |
| 26  | lab_room_subpanel_24v    | Lab Room Subpanel 24V    | 24V     |
| 25  | lab_room_subpanel_12v    | Lab Room Subpanel 12V    | 12V     |
| 24  | lab_room_subpanel_5v     | Lab Room Subpanel 5V     | 5V      |
| 29  | study_room_subpanel_24v  | Study Room Subpanel 24V  | 24V     |
| 28  | study_room_subpanel_12v  | Study Room Subpanel 12V  | 12V     |
| 27  | study_room_subpanel_5v   | Study Room Subpanel 5V   | 5V      |
| 32  | gun_drawers_24v          | Gun Drawers 24V          | 24V     |
| 31  | gun_drawers_12v          | Gun Drawers 12V          | 12V     |
| 30  | gun_drawers_5v           | Gun Drawers 5V           | 5V      |
| 33  | keys_5v                  | Keys Puzzle 5V           | 5V      |
| 35  | empty_35                 | Reserved Relay 35        | -       |
| 34  | empty_34                 | Reserved Relay 34        | -       |

### Device Groups

**Gear Puzzle:**

- Pins 11, 10, 9 (24V, 12V, 5V)

**Floor Puzzle:**

- Pins 8, 7, 6 (24V, 12V, 5V)

**Riddle System:**

- Pins 5, 4, 3 (RPi 5V, RPi 12V, Riddle 5V)

**Boiler Room Subpanel:**

- Pins 1, 0, 2 (24V, 12V, 5V)

**Lab Room Subpanel:**

- Pins 26, 25, 24 (24V, 12V, 5V)

**Study Room Subpanel:**

- Pins 29, 28, 27 (24V, 12V, 5V)

**Gun Drawers:**

- Pins 32, 31, 30 (24V, 12V, 5V)

**Keys Puzzle:**

- Pin 33 (5V)

**Reserved for Future Use:**

- Pins 35, 34 (Empty/Reserved)

---

## Controller 3: power_control_lower_left

**Controller ID:** `power_control_lower_left`  
**Relay Count:** 6  
**Firmware Version:** 2.0.1

### Pin Assignments

| Pin | Device ID             | Friendly Name         | Voltage |
| --- | --------------------- | --------------------- | ------- |
| 33  | lever_riddle_cube_24v | Lever Riddle Cube 24V | 24V     |
| 34  | lever_riddle_cube_12v | Lever Riddle Cube 12V | 12V     |
| 35  | lever_riddle_cube_5v  | Lever Riddle Cube 5V  | 5V      |
| 36  | clock_24v             | Clock Puzzle 24V      | 24V     |
| 37  | clock_12v             | Clock Puzzle 12V      | 12V     |
| 38  | clock_5v              | Clock Puzzle 5V       | 5V      |

### Device Groups

**Lever Riddle Cube:**

- Pins 33, 34, 35 (24V, 12V, 5V)

**Clock Puzzle:**

- Pins 36, 37, 38 (24V, 12V, 5V)

---

## MQTT Topic Structure

All power control devices follow the standard Sentient MQTT topic format:

```
paragon/clockwork/commands/{controller_id}/{device_id}/{command}
```

### Example Topics:

**Upper Right Controller:**

```
paragon/clockwork/commands/power_control_upper_right/main_lighting_24v/power_on
paragon/clockwork/commands/power_control_upper_right/main_lighting_24v/power_off
```

**Lower Right Controller:**

```
paragon/clockwork/commands/power_control_lower_right/gear_24v/power_on
paragon/clockwork/commands/power_control_lower_right/boiler_room_subpanel_5v/power_off
```

**Lower Left Controller:**

```
paragon/clockwork/commands/power_control_lower_left/clock_12v/power_on
paragon/clockwork/commands/power_control_lower_left/lever_riddle_cube_24v/power_off
```

---

## Controller-Level Commands

Each controller supports the following system-wide commands:

| Command          | Description                                        |
| ---------------- | -------------------------------------------------- |
| `all_on`         | Turn all relays on simultaneously                  |
| `all_off`        | Turn all relays off simultaneously                 |
| `emergency_off`  | Immediate emergency power cutoff (publishes event) |
| `reset`          | Reset controller (all relays off)                  |
| `request_status` | Request full status report                         |

### Example Controller Commands:

```
paragon/clockwork/commands/power_control_upper_right/controller/all_on
paragon/clockwork/commands/power_control_lower_right/controller/emergency_off
paragon/clockwork/commands/power_control_lower_left/controller/request_status
```

---

## Status Reporting

All controllers publish status updates on the following topics:

**Hardware Status:**

```
paragon/clockwork/status/{controller_id}/hardware
```

**Full Status:**

```
paragon/clockwork/status/{controller_id}/full
```

**Heartbeat:**

```
paragon/clockwork/status/{controller_id}/heartbeat
```

**Emergency Events:**

```
paragon/clockwork/events/{controller_id}/emergency
```

---

## Power Distribution Summary

### By Voltage Rail:

**24V Rails:** 17 total

- Upper Right: 6 (Main Lighting, Syringe, Chemical, Vault)
- Lower Right: 9 (Gear, Floor, Boiler Subpanel, Lab Subpanel, Study Subpanel, Gun Drawers)
- Lower Left: 2 (Lever Riddle Cube, Clock)

**12V Rails:** 19 total

- Upper Right: 8 (Main Lighting, Gauges A/B, Lever Boiler, Fuse, Syringe, Chemical, Vault)
- Lower Right: 9 (Gear, Floor, Riddle RPi, Boiler Subpanel, Lab Subpanel, Study Subpanel, Gun Drawers)
- Lower Left: 2 (Lever Riddle Cube, Clock)

**5V Rails:** 18 total

- Upper Right: 7 (Main Lighting, Gauges, Lever Boiler, Pilot Light, Kraken, Fuse, Syringe, Chemical, Vault)
- Lower Right: 9 (Gear, Floor, Riddle RPi, Riddle, Boiler Subpanel, Lab Subpanel, Study Subpanel, Gun Drawers, Keys)
- Lower Left: 2 (Lever Riddle Cube, Clock)

**Special Effects:** 3 total

- Upper Right: 3 (Blacklight, Floor Audio Amp, Kraken Radar Amp)

**Reserved:** 2 total

- Lower Right: 2 (Empty 34, Empty 35)

---

## Safety Features

1. **Emergency Power Off:** All controllers support immediate emergency shutdown via `emergency_off` command
2. **Default State:** All relays initialize to OFF on boot
3. **Status Reporting:** Real-time relay state reporting via MQTT
4. **Heartbeat Monitoring:** 5-second heartbeat interval for controller health tracking
5. **Event Logging:** Emergency events published to MQTT for audit trail

---

## Firmware Location

All controller firmware is located in:

```
/opt/sentient/hardware/Controller Code Teensy/
├── power_control_upper_right/
│   ├── FirmwareMetadata.h
│   ├── controller_naming.h
│   └── power_control_upper_right.ino
├── power_control_lower_right/
│   ├── FirmwareMetadata.h
│   ├── controller_naming.h
│   └── power_control_lower_right.ino
└── power_control_lower_left/
    ├── FirmwareMetadata.h
    ├── controller_naming.h
    └── power_control_lower_left.ino
```

---

**Document Version:** 1.0  
**Last Updated:** October 30, 2025  
**Author:** Sentient Engine Team
