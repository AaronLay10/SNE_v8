# Separate Command Pattern for Teensy Controllers

## Overview

For devices that have distinct states (maglocks, LEDs, relays, etc.), register **two separate commands** - one for each action - instead of one command that accepts a parameter.

## Command Naming Standards

### Maglocks
- **Commands**: `lock` and `unlock`
- **unlock** = LOW (de-energize maglock)
- **lock** = HIGH (energize maglock)

### LEDs / Lights
- **Commands**: `on` and `off`
- **on** = HIGH (turn on LED)
- **off** = LOW (turn off LED)

### Relays / Generic On/Off Devices
- **Commands**: `on` and `off`
- Use the action name that describes what the device DOES, not the electrical state

## Old Pattern (Parameter-Based)

```cpp
// ❌ OLD WAY - Single command with parameter
manifest.add_device("drop_panel_fuse", "Drop Panel - Fuse", "maglock", "output");
manifest.add_device_topic("drop_panel_fuse", "commands/dropPanelFuse", "command");

// Command handler
if (cmd.equalsIgnoreCase("dropPanelFuse")) {
  if (parse_truth(value)) {
    digitalWrite(maglock_panel_fuse_pin, LOW);  // Unlock
  } else {
    digitalWrite(maglock_panel_fuse_pin, HIGH); // Lock
  }
}
```

## New Pattern (Multiple Command Topics per Device)

### For Maglocks

```cpp
// ✅ CORRECT - One device with multiple command topics

// Register ONE device with TWO command topics
manifest.add_device("drop_panel_fuse", "Drop Panel Fuse", "maglock", "output");
manifest.add_device_topic("drop_panel_fuse", "commands/dropPanelFuse_unlock", "command");
manifest.add_device_topic("drop_panel_fuse", "commands/dropPanelFuse_lock", "command");
manifest.add_device_topic("drop_panel_fuse", "Status/Hardware", "state");

// Command handlers (no parameter parsing needed)
if (cmd.equalsIgnoreCase("dropPanelFuse_unlock")) {
  digitalWrite(maglock_panel_fuse_pin, LOW);  // Unlock (LOW = de-energize)
  panel_fuse_locked = false;
  Serial.println("[FUSE PANEL] Unlocked");
  publish_hardware_status();
}
else if (cmd.equalsIgnoreCase("dropPanelFuse_lock")) {
  digitalWrite(maglock_panel_fuse_pin, HIGH); // Lock (HIGH = energize)
  panel_fuse_locked = true;
  Serial.println("[FUSE PANEL] Locked");
  publish_hardware_status();
}
```

### For LEDs

```cpp
// ✅ CORRECT - One device with multiple command topics

// Register ONE device with TWO command topics
manifest.add_device("boiler_fire_led", "Boiler Fire LED", "led", "output");
manifest.add_device_topic("boiler_fire_led", "commands/boilerFireLED_on", "command");
manifest.add_device_topic("boiler_fire_led", "commands/boilerFireLED_off", "command");
manifest.add_device_topic("boiler_fire_led", "Status/Hardware", "state");

// Command handlers
if (cmd.equalsIgnoreCase("boilerFireLED_on")) {
  digitalWrite(boiler_fire_led_pin, HIGH);  // Turn on (HIGH = on)
  boiler_led_on = true;
  Serial.println("[BOILER LED] Turned on");
}
else if (cmd.equalsIgnoreCase("boilerFireLED_off")) {
  digitalWrite(boiler_fire_led_pin, LOW);   // Turn off (LOW = off)
  boiler_led_on = false;
  Serial.println("[BOILER LED] Turned off");
}
```

## Benefits

1. **Clarity**: Each command is a separate, explicit action
2. **Simplicity**: No parameter parsing required
3. **Safety**: Each command has a single, clear purpose
4. **Correct Architecture**: One device with multiple commands (not multiple devices)
5. **UI**: Sentient web interface shows two distinct buttons per device
6. **Logging**: Audit trail shows exactly what command was sent

## Device and Command Naming Convention

### Device Names (device_id)
- Use simple, descriptive names for the **physical device**
- Examples: `drop_panel_fuse`, `metal_gate_maglock`, `boiler_fire_led`

### Command Topic Names

**For Maglocks:**
```
commands/<deviceName>_unlock  →  Unlocks the maglock (LOW)
commands/<deviceName>_lock    →  Locks the maglock (HIGH)
```

**For LEDs and Generic On/Off Devices:**
```
commands/<deviceName>_on   →  Turns device ON (usually HIGH)
commands/<deviceName>_off  →  Turns device OFF (usually LOW)
```

### Friendly Name Convention

Use simple, descriptive names for the device itself:
- `"Drop Panel Fuse"` (not "Drop Panel Fuse - UNLOCK")
- `"Metal Gate Maglock"`
- `"Boiler Fire LED"`
- `"Pilot Light Indicator"`

## Full Example: Maglock with Multiple Commands

```cpp
// In build_capability_manifest():

// Drop Panel Fuse (ONE device with TWO command topics)
manifest.add_device("drop_panel_fuse", "Drop Panel Fuse", "maglock", "output");
manifest.add_device_topic("drop_panel_fuse", "commands/dropPanelFuse_unlock", "command");
manifest.add_device_topic("drop_panel_fuse", "commands/dropPanelFuse_lock", "command");
manifest.add_device_topic("drop_panel_fuse", "Status/Hardware", "state");

// Drop Panel Riddle (ONE device with TWO command topics)
manifest.add_device("drop_panel_riddle", "Drop Panel Riddle", "maglock", "output");
manifest.add_device_topic("drop_panel_riddle", "commands/dropPanelRiddle_unlock", "command");
manifest.add_device_topic("drop_panel_riddle", "commands/dropPanelRiddle_lock", "command");
manifest.add_device_topic("drop_panel_riddle", "Status/Hardware", "state");

// Metal Gate (ONE device with TWO command topics)
manifest.add_device("metal_gate_maglock", "Metal Gate Maglock", "maglock", "output");
manifest.add_device_topic("metal_gate_maglock", "commands/metalGate_unlock", "command");
manifest.add_device_topic("metal_gate_maglock", "commands/metalGate_lock", "command");
manifest.add_device_topic("metal_gate_maglock", "Status/Hardware", "state");
```

```cpp
// In handle_mqtt_command():

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx) {
  String cmd(command);

  Serial.print("[COMMAND] ");
  Serial.println(cmd);

  // Drop Panel Fuse - UNLOCK
  if (cmd.equalsIgnoreCase("dropPanelFuse_unlock")) {
    digitalWrite(maglock_panel_fuse_pin, LOW);
    panel_fuse_locked = false;
    Serial.println("[FUSE PANEL] Unlocked");
    publish_hardware_status();
  }

  // Drop Panel Fuse - LOCK
  else if (cmd.equalsIgnoreCase("dropPanelFuse_lock")) {
    digitalWrite(maglock_panel_fuse_pin, HIGH);
    panel_fuse_locked = true;
    Serial.println("[FUSE PANEL] Locked");
    publish_hardware_status();
  }

  // Drop Panel Riddle - UNLOCK
  else if (cmd.equalsIgnoreCase("dropPanelRiddle_unlock")) {
    digitalWrite(maglock_panel_riddle_pin, LOW);
    panel_riddle_locked = false;
    Serial.println("[RIDDLE PANEL] Unlocked");
    publish_hardware_status();
  }

  // Drop Panel Riddle - LOCK
  else if (cmd.equalsIgnoreCase("dropPanelRiddle_lock")) {
    digitalWrite(maglock_panel_riddle_pin, HIGH);
    panel_riddle_locked = true;
    Serial.println("[RIDDLE PANEL] Locked");
    publish_hardware_status();
  }

  // ... continue for all devices

  else {
    Serial.print("[UNKNOWN COMMAND] ");
    Serial.println(cmd);
  }
}
```

## Migration Steps

1. **Update firmware** with new command registration pattern
2. **Upload to Teensy** and verify registration via Serial monitor
3. **Verify in database** that new device records are created
4. **Test commands** via Sentient web interface
5. **Remove old device records** from database (optional cleanup)

## Database Impact

Each device still creates **1 database record**, but now has **multiple MQTT topics** registered:

**Before (Single command with parameter):**
```
device_id: drop_panel_fuse
friendly_name: Drop Panel Fuse
device_command_name: dropPanelFuse  (accepts parameter: lock/unlock)
```

**After (Multiple command topics):**
```
device_id: drop_panel_fuse
friendly_name: Drop Panel Fuse
mqtt_topics:
  - commands/dropPanelFuse_unlock
  - commands/dropPanelFuse_lock
  - Status/Hardware
```

This is the **correct approach**:
- ✅ **4 maglocks** = 4 device records
- ✅ Each maglock has **2 command topics** (lock + unlock)
- ❌ **NOT** 8 devices (that would be wrong)

## Backward Compatibility

The old parameter-based commands can be kept alongside the new commands for a transition period, but should be marked as deprecated.
