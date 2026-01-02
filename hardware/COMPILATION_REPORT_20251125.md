# Sentient Controller Firmware - Compilation Report

**Date:** November 25, 2025  
**Time:** 17:03:50  
**Compiler:** arduino-cli (teensy:avr:teensy41)

---

## Summary

- **Total Controllers:** 35
- **✅ Successful:** 34 (97.1%)
- **❌ Failed:** 1 (2.9%)
- **⚠️ Skipped:** 1 (gun_edith_v2 - no .ino file)

---

## Successfully Compiled Controllers (34)

All HEX files located in: `/Users/aaron/Sentient/hardware/HEX_OUTPUT/`

| #   | Controller Name               | HEX Size | Output Directory                        |
| --- | ----------------------------- | -------- | --------------------------------------- |
| 1   | boiler_room_subpanel_v2       | 1.2M     | `HEX_OUTPUT/boiler_room_subpanel_v2/`   |
| 2   | chemical_v2                   | 1.1M     | `HEX_OUTPUT/chemical_v2/`               |
| 3   | clock_v2                      | 1.2M     | `HEX_OUTPUT/clock_v2/`                  |
| 4   | crank_v2                      | 1.1M     | `HEX_OUTPUT/crank_v2/`                  |
| 5   | floor_v2                      | 1.2M     | `HEX_OUTPUT/floor_v2/`                  |
| 6   | fuse_v2                       | 1.1M     | `HEX_OUTPUT/fuse_v2/`                   |
| 7   | gauge_1_3_4_v2                | 1.1M     | `HEX_OUTPUT/gauge_1_3_4_v2/`            |
| 8   | gauge_2_5_7_v2                | 1.1M     | `HEX_OUTPUT/gauge_2_5_7_v2/`            |
| 9   | gauge_6_leds_v2               | 1.2M     | `HEX_OUTPUT/gauge_6_leds_v2/`           |
| 10  | gear_v2                       | 1.1M     | `HEX_OUTPUT/gear_v2/`                   |
| 11  | gun_drawers_v2                | 1.1M     | `HEX_OUTPUT/gun_drawers_v2/`            |
| 12  | keys_v2                       | 1.2M     | `HEX_OUTPUT/keys_v2/`                   |
| 13  | kraken_v2                     | 1.1M     | `HEX_OUTPUT/kraken_v2/`                 |
| 14  | lab_rm_cage_a_v2              | 1.1M     | `HEX_OUTPUT/lab_rm_cage_a_v2/`          |
| 15  | lab_rm_cage_b_v2              | 1.1M     | `HEX_OUTPUT/lab_rm_cage_b_v2/`          |
| 16  | lab_rm_doors_hoist_v2         | 1.1M     | `HEX_OUTPUT/lab_rm_doors_hoist_v2/`     |
| 17  | lever_fan_safe_v2             | 1.1M     | `HEX_OUTPUT/lever_fan_safe_v2/`         |
| 18  | lever_riddle_v2               | 1.1M     | `HEX_OUTPUT/lever_riddle_v2/`           |
| 19  | main_lighting_v2              | 1.2M     | `HEX_OUTPUT/main_lighting_v2/`          |
| 20  | maks_servo_v2                 | 1.1M     | `HEX_OUTPUT/maks_servo_v2/`             |
| 21  | music_v2                      | 1.0M     | `HEX_OUTPUT/music_v2/`                  |
| 22  | picture_frame_leds_v2         | 1.1M     | `HEX_OUTPUT/picture_frame_leds_v2/`     |
| 23  | pilaster_v2                   | 1.1M     | `HEX_OUTPUT/pilaster_v2/`               |
| 24  | pilot_light_v2                | 1.2M     | `HEX_OUTPUT/pilot_light_v2/`            |
| 25  | **power_control_lower_left**  | 1.1M     | `HEX_OUTPUT/power_control_lower_left/`  |
| 26  | **power_control_lower_right** | 1.1M     | `HEX_OUTPUT/power_control_lower_right/` |
| 27  | **power_control_upper_right** | 1.1M     | `HEX_OUTPUT/power_control_upper_right/` |
| 28  | riddle_v2                     | 1.1M     | `HEX_OUTPUT/riddle_v2/`                 |
| 29  | study_a_v2                    | 1.1M     | `HEX_OUTPUT/study_a_v2/`                |
| 30  | study_b_v2                    | 1.1M     | `HEX_OUTPUT/study_b_v2/`                |
| 31  | study_d_v2                    | 1.1M     | `HEX_OUTPUT/study_d_v2/`                |
| 32  | syringe_v2                    | 1.2M     | `HEX_OUTPUT/syringe_v2/`                |
| 33  | vault_v2                      | 1.1M     | `HEX_OUTPUT/vault_v2/`                  |
| 34  | vern_v2                       | 1.1M     | `HEX_OUTPUT/vern_v2/`                   |

**Note:** All three power control controllers compiled successfully ✅

---

## Failed Controllers (1)

### lever_boiler_v2

**Error:** IRremote library API compatibility issue

**Root Cause:**

```
/Users/aaron/Library/Arduino15/packages/teensy/hardware/avr/1.59.0/libraries/IRremote/src/IRSend.hpp:135:20: error:
call to 'beginUsageError' declared with attribute error:
Error: You must use begin(<sendPin>, <EnableLEDFeedback>, <aFeedbackLEDPin>) if IR_SEND_PIN and
SEND_PWM_BY_TIMER are not defined or USE_NO_SEND_PWM is defined.
```

**Fix Required:**
The IRremote `begin()` function call needs to be updated with proper parameters:

- Current: `begin()`
- Required: `begin(<sendPin>, <EnableLEDFeedback>, <aFeedbackLEDPin>)`

**Location:** `/Users/aaron/Sentient/hardware/Controller Code Teensy/lever_boiler_v2/lever_boiler_v2.ino`

**Additional Warnings:**

- Deprecated ArduinoJson `StaticJsonDocument` (line 521) - should use `JsonDocument` instead

---

## Skipped Controllers (1)

### gun_edith_v2

**Reason:** No `.ino` file found in directory

---

## Build Details

**Compilation Command:**

```bash
arduino-cli compile \
  --fqbn teensy:avr:teensy41 \
  --build-path ./BUILD_FILES/<controller_name> \
  --output-dir ./HEX_OUTPUT/<controller_name> \
  --libraries ./Custom\ Libraries \
  <controller_name>.ino
```

**Custom Libraries Used:**

- `SentientMQTT` - MQTT communication layer
- `SentientDeviceRegistry` v2.0.2 - Device registration
- `SentientCapabilityManifest` v2.0.2 - Capability declarations

**Standard Libraries:**

- ArduinoJson 7.4.2
- PubSubClient 2.8
- NativeEthernet 1.0.5
- FNET 0.1.3
- TeensyID 1.4.0

---

## Directory Structure

```
hardware/
├── HEX_OUTPUT/                          # All compiled HEX files
│   ├── boiler_room_subpanel_v2/
│   │   └── boiler_room_subpanel_v2.ino.hex
│   ├── chemical_v2/
│   │   └── chemical_v2.ino.hex
│   ├── ...
│   └── vern_v2/
│       └── vern_v2.ino.hex
├── BUILD_FILES/                         # Intermediate build artifacts
│   ├── boiler_room_subpanel_v2/
│   ├── chemical_v2/
│   └── ...
├── compile_all_controllers.sh           # Batch compilation script
└── compile_all_20251125_170350.log      # Full compilation log
```

---

## Next Steps

### Immediate Actions

1. **Fix lever_boiler_v2** - Update IRremote begin() call with proper parameters
2. **Investigate gun_edith_v2** - Determine if firmware is needed
3. **Flash Power Control Controllers** - All 3 power controllers ready for deployment

### Deployment Preparation

All 34 compiled firmwares are ready for flashing to Teensy 4.1 controllers using Teensy Loader.

**Flash Command (per controller):**

```bash
teensy_loader_cli --mcu=TEENSY41 -w -v \
  /Users/aaron/Sentient/hardware/HEX_OUTPUT/<controller_name>/<controller_name>.ino.hex
```

### Troubleshooting Power Control Issues

If the 3 devices still show ON after "All OFF" command:

1. Reflash power control firmwares:
   - `power_control_upper_right`
   - `power_control_lower_right`
   - `power_control_lower_left`
2. Verify MQTT topics match firmware expectations
3. Check WebSocket state synchronization

---

## Log Files

**Full Compilation Log:**  
`/Users/aaron/Sentient/hardware/compile_all_20251125_170350.log`

**To Review Errors:**

```bash
grep -i "error" compile_all_20251125_170350.log
```

**To See Warnings:**

```bash
grep -i "warning" compile_all_20251125_170350.log
```

---

## Script Usage

**Run Full Batch Compilation:**

```bash
cd /Users/aaron/Sentient/hardware
./compile_all_controllers.sh
```

**Output:**

- HEX files → `hardware/HEX_OUTPUT/`
- Build artifacts → `hardware/BUILD_FILES/`
- Compilation log → `hardware/compile_all_YYYYMMDD_HHMMSS.log`

---

## Success Rate: 97.1% ✅

34 out of 35 controllers ready for deployment.
