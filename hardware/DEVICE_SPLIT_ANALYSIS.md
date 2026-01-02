# Device Split Analysis - Multi-Function Devices

## Summary

Analysis of firmware to identify single devices that combine multiple distinct functions and should be split into separate devices.

## Critical Findings

### 1. **Intro TV** (boiler_room_subpanel_v2) - **REQUIRES SPLIT**

**Current State:**

- Single device `intro_tv` with 4 commands
- Combines TV power control + TV lift motor control

**Commands:**

```c
const char *intro_tv_commands[] = {
    naming::CMD_TV_POWER_ON,      // Power relay
    naming::CMD_TV_POWER_OFF,     // Power relay
    naming::CMD_TV_LIFT_UP,       // Motor control
    naming::CMD_TV_LIFT_DOWN      // Motor control
};
```

**Hardware:**

- Pin 4: TV Power relay
- Pins 2-3: TV Lift motor (up/down)

**Recommendation:** Split into TWO devices:

1. **`intro_tv_power`** - TV Power control (power_on, power_off)
2. **`intro_tv_lift`** - TV Lift control (lift_up, lift_down, stop?)

**Rationale:**

- Completely different hardware subsystems (relay vs motor)
- Different action types (power vs movement)
- Independent operation required
- Better state tracking (power state vs lift position)

---

### 2. **Fog Machine** (boiler_room_subpanel_v2) - **CONSIDER SPLIT**

**Current State:**

- Single device `fog_machine` with 5 commands
- Combines 3 distinct subsystems

**Commands:**

```c
const char *fog_machine_commands[] = {
    naming::CMD_FOG_POWER_ON,      // Main power relay
    naming::CMD_FOG_POWER_OFF,     // Main power relay
    naming::CMD_FOG_TRIGGER,       // Fog trigger relay (momentary)
    naming::CMD_ULTRASONIC_ON,     // Ultrasonic water relay
    naming::CMD_ULTRASONIC_OFF     // Ultrasonic water relay
};
```

**Hardware:**

- Pin 5: Fog power relay (main)
- Pin 6: Fog trigger relay (momentary pulse)
- Pin 7: Ultrasonic water relay

**Recommendation:** Split into THREE devices:

1. **`fog_machine_power`** - Main power control
2. **`fog_machine_trigger`** - Fog trigger (momentary action)
3. **`fog_machine_ultrasonic`** - Ultrasonic water system

**Rationale:**

- Three independent relay controls
- Trigger is momentary (pulse), others are state-based
- Ultrasonic can operate independently
- Better separation of concerns for orchestration

**Alternative:** Keep as one device if these always operate together in sequences (e.g., power on → wait → trigger → power off)

---

### 3. **Study Door** (boiler_room_subpanel_v2) - **ACCEPTABLE AS-IS**

**Current State:**

- Single device controlling 3 maglocks as a group

**Commands:**

```c
const char *study_door_commands[] = {
    naming::CMD_DOOR_LOCK,
    naming::CMD_DOOR_UNLOCK
};
```

**Hardware:**

- Three maglocks (top, bottom_a, bottom_b) controlled together

**Recommendation:** Keep as single device

**Rationale:** These represent a logical single unit (one door) that always operates in unison.

---

## Other Controllers Reviewed

### Devices That Are Correctly Split:

- **study_b_v2**: TVs, motors, and fog are separate devices ✅
- **lever_fan_safe_v2**: Maglocks, solenoids, motors are separate ✅
- **fuse_v2**: RFID readers, maglocks, actuators are separate ✅
- **chemical_v2**: RFID readers and maglocks are separate ✅
- **study_a_v2**: Motors and sensors are separate ✅

### Devices That Combine Related Functions (Acceptable):

- **Porthole Controller**: Has sensors + control commands (logically one unit)
- **IR Receivers**: Have activate/deactivate commands + sensors (sensor device)
- **Gauge devices**: Multiple LED commands for one gauge display unit

## Action Items

### Immediate (High Priority):

1. **Split Intro TV in boiler_room_subpanel_v2**
   - Create `intro_tv_power` device
   - Create `intro_tv_lift` device
   - Update command handling
   - Update state publishing
   - Update device registration

### Consider (Medium Priority):

2. **Evaluate Fog Machine split**
   - Review game design sequences
   - If subsystems operate independently in gameplay, split them
   - If they always operate together, can remain combined

### Database/API Updates Needed:

- Add new device entries to database
- Update API device registration
- Update UI to show separate device nodes
- Update scene editor to handle new device IDs
- Migrate existing scenes that reference `intro_tv`

## Implementation Notes

### Firmware Changes (boiler_room_subpanel_v2.ino)

**Device Definitions:**

```c
// Intro TV Power
const char *intro_tv_power_commands[] = {
    naming::CMD_TV_POWER_ON,
    naming::CMD_TV_POWER_OFF
};

// Intro TV Lift
const char *intro_tv_lift_commands[] = {
    naming::CMD_TV_LIFT_UP,
    naming::CMD_TV_LIFT_DOWN,
    naming::CMD_TV_LIFT_STOP  // Add stop command
};

SentientDeviceDef dev_intro_tv_power(
    naming::DEV_INTRO_TV_POWER, "Intro TV Power", "relay",
    intro_tv_power_commands, 2);

SentientDeviceDef dev_intro_tv_lift(
    naming::DEV_INTRO_TV_LIFT, "Intro TV Lift", "motor",
    intro_tv_lift_commands, 3);
```

**Command Handling:**

```c
if (device == naming::DEV_INTRO_TV_POWER) {
    if (command == naming::CMD_TV_POWER_ON) {
        tv_power_on = true;
        digitalWrite(tv_power_pin, HIGH);
    } else if (command == naming::CMD_TV_POWER_OFF) {
        tv_power_on = false;
        digitalWrite(tv_power_pin, LOW);
    }
    publish_hardware_status();
}
else if (device == naming::DEV_INTRO_TV_LIFT) {
    if (command == naming::CMD_TV_LIFT_UP) {
        tv_lift_state = 1;
        digitalWrite(tv_lift_up_pin, HIGH);
        digitalWrite(tv_lift_down_pin, LOW);
    } else if (command == naming::CMD_TV_LIFT_DOWN) {
        tv_lift_state = -1;
        digitalWrite(tv_lift_down_pin, HIGH);
        digitalWrite(tv_lift_up_pin, LOW);
    } else if (command == naming::CMD_TV_LIFT_STOP) {
        tv_lift_state = 0;
        digitalWrite(tv_lift_up_pin, LOW);
        digitalWrite(tv_lift_down_pin, LOW);
    }
    publish_hardware_status();
}
```

### Naming Constants Update (SentientNaming.h)

```c
// Device IDs
static constexpr const char* DEV_INTRO_TV_POWER = "intro_tv_power";
static constexpr const char* DEV_INTRO_TV_LIFT = "intro_tv_lift";

// Friendly Names
static constexpr const char* FRIENDLY_INTRO_TV_POWER = "Intro TV Power";
static constexpr const char* FRIENDLY_INTRO_TV_LIFT = "Intro TV Lift";

// Device Types
static constexpr const char* TYPE_INTRO_TV_POWER = "relay";
static constexpr const char* TYPE_INTRO_TV_LIFT = "motor";

// Commands (add stop)
static constexpr const char* CMD_TV_LIFT_STOP = "lift_stop";
```

## Migration Strategy

1. **Phase 1:** Update firmware with new device definitions
2. **Phase 2:** Compile and test on hardware
3. **Phase 3:** Update database with new device entries
4. **Phase 4:** Update API to recognize new device IDs
5. **Phase 5:** Update UI scenes to use new device nodes
6. **Phase 6:** Deploy firmware to production Teensy
7. **Phase 7:** Verify end-to-end command execution

## Implementation Status

✅ **COMPLETED** - Firmware updated and compiled successfully (v2.3.8)

### Changes Made:

**Intro TV Split:**

- `intro_tv` → `intro_tv_power` (relay) + `intro_tv_lift` (motor)
- Added `lift_stop` command for lift control
- Separate device IDs, command arrays, and handlers

**Fog Machine Split:**

- `boiler_room_fog_machine` → `fog_power` + `fog_trigger` + `fog_ultrasonic`
- Power: relay control (on/off)
- Trigger: momentary relay with duration parameter
- Ultrasonic: relay control (on/off)

**Files Modified:**

1. `hardware/Controller Code Teensy/boiler_room_subpanel_v2/controller_naming.h`
2. `hardware/Controller Code Teensy/boiler_room_subpanel_v2/boiler_room_subpanel_v2.ino`

**Compiled Output:**

- `hardware/HEX_OUTPUT/boiler_room_subpanel_v2.ino.hex` (v2.3.8)

## Testing Checklist

### Pre-Deployment (Database & API):

- [ ] Add `intro_tv_power` device to database
- [ ] Add `intro_tv_lift` device to database
- [ ] Add `fog_power` device to database
- [ ] Add `fog_trigger` device to database
- [ ] Add `fog_ultrasonic` device to database
- [ ] Update device actions for each new device
- [ ] Test API returns new devices
- [ ] Update UI scene nodes to use new device IDs

### Post-Firmware Flash:

- [ ] Flash firmware to Teensy 4.1
- [ ] Verify controller registers with 5 new devices
- [ ] Intro TV Power On command works
- [ ] Intro TV Power Off command works
- [ ] Intro TV Lift Up command works
- [ ] Intro TV Lift Down command works
- [ ] Intro TV Lift Stop command works (new)
- [ ] Fog Power On command works
- [ ] Fog Power Off command works
- [ ] Fog Trigger command works (with duration parameter)
- [ ] Fog Ultrasonic On command works
- [ ] Fog Ultrasonic Off command works
- [ ] All devices can be controlled independently
- [ ] State updates correctly for each device
- [ ] UI nodes show correct states
- [ ] Scene sequences execute properly
- [ ] E-stop/power-off sequence handles all devices
