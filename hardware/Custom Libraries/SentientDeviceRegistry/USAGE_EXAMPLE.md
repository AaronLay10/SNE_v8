# SentientDeviceRegistry - Usage Example

## The Problem It Solves

**BEFORE:** To add a device with commands, you had to update multiple places:
1. Manifest registration (capability_manifest.h)
2. Command handler (handle_mqtt_command)
3. Database (device_command_name)

**AFTER:** Define the device ONCE, everything auto-generated!

---

## Complete Example

```cpp
// ============================================================================
// INCLUDES
// ============================================================================
#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>  // NEW!
#include "FirmwareMetadata.h"

// ============================================================================
// DEVICE DEFINITIONS (SINGLE SOURCE OF TRUTH!)
// ============================================================================

// Define commands for Fire LEDs
const char* fireLEDs_commands[] = {"fireLEDs"};

// Define commands for Boiler Monitor
const char* boilerMonitor_commands[] = {"boilerMonitor"};

// Define commands for Newell Power
const char* newellPower_commands[] = {"newellPower"};

// Define commands for Flange LEDs
const char* flangeLEDs_commands[] = {"flangeLEDs"};

// Define sensors for Color Sensor
const char* colorSensor_sensors[] = {"ColorSensor"};

// Create device definitions
SentientDeviceDef dev_fire_leds(
  "boiler_fire_leds",           // device_id
  "Boiler Fire LEDs",           // friendly_name
  "led_strip",                  // device_type
  fireLEDs_commands,            // commands array
  1                             // number of commands
);

SentientDeviceDef dev_boiler_monitor(
  "boiler_monitor_relay",
  "Boiler Monitor Power",
  "relay",
  boilerMonitor_commands,
  1
);

SentientDeviceDef dev_newell_power(
  "newell_power_relay",
  "Newell Power Control",
  "relay",
  newellPower_commands,
  1
);

SentientDeviceDef dev_flange_leds(
  "flange_status_leds",
  "Flange Status LEDs",
  "led_strip",
  flangeLEDs_commands,
  1
);

SentientDeviceDef dev_color_sensor(
  "color_sensor",
  "Color Sensor",
  "sensor",
  colorSensor_sensors,
  1,
  true  // is_input flag
);

// Create the registry
SentientDeviceRegistry deviceRegistry;

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);

  // Register all devices (SINGLE PLACE!)
  deviceRegistry.addDevice(&dev_fire_leds);
  deviceRegistry.addDevice(&dev_boiler_monitor);
  deviceRegistry.addDevice(&dev_newell_power);
  deviceRegistry.addDevice(&dev_flange_leds);
  deviceRegistry.addDevice(&dev_color_sensor);

  // Print registry summary (helpful for debugging)
  deviceRegistry.printSummary();

  // Initialize MQTT...
  // Initialize hardware...
}

// ============================================================================
// MANIFEST BUILDING (AUTO-GENERATED!)
// ============================================================================

void build_capability_manifest() {
  // Set controller info
  manifest.set_controller_info(
    firmware::UNIQUE_ID,
    "Pilot Light Controller",
    firmware::VERSION,
    "PilotLight",
    room_id,
    puzzle_id
  );

  // Build entire manifest from registry - ONE LINE!
  deviceRegistry.buildManifest(manifest);

  // That's it! No manual topic registration needed!
}

// ============================================================================
// COMMAND HANDLER
// ============================================================================

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx) {
  String cmd(command);

  // Optional: Check if command is valid before processing
  if (!deviceRegistry.isValidCommand(command)) {
    Serial.print(F("[PilotLight] Unknown command: "));
    Serial.println(command);
    return;
  }

  // Handle commands
  if (cmd.equalsIgnoreCase("fireLEDs")) {
    // ... handle fire LEDs
  }
  else if (cmd.equalsIgnoreCase("boilerMonitor")) {
    // ... handle boiler monitor
  }
  else if (cmd.equalsIgnoreCase("newellPower")) {
    // ... handle newell power
  }
  else if (cmd.equalsIgnoreCase("flangeLEDs")) {
    // ... handle flange LEDs
  }
}
```

---

## Benefits

### ✅ Single Source of Truth
Define each device ONCE at the top of your file. Everything else auto-generates.

### ✅ No Duplication
Impossible for manifest and code to get out of sync.

### ✅ Clear Structure
All devices defined in one place, easy to see what controller does.

### ✅ Validation
`isValidCommand()` lets you validate commands before processing.

### ✅ Debug Info
`printSummary()` shows complete device registry on serial monitor.

---

## Migration Guide

### Step 1: Add Include
```cpp
#include <SentientDeviceRegistry.h>
```

### Step 2: Define Devices at Top
```cpp
// Command arrays
const char* myDevice_commands[] = {"command1", "command2"};

// Device definitions
SentientDeviceDef dev_my_device(
  "my_device_id",
  "My Device Name",
  "relay",
  myDevice_commands,
  2  // number of commands
);

// Create registry
SentientDeviceRegistry deviceRegistry;
```

### Step 3: Register in setup()
```cpp
void setup() {
  deviceRegistry.addDevice(&dev_my_device);
  deviceRegistry.printSummary();  // Optional: print debug info
}
```

### Step 4: Replace Manual Manifest Building
```cpp
void build_capability_manifest() {
  manifest.set_controller_info(...);

  // REPLACE ALL THIS:
  // manifest.add_device("my_device_id", ...);
  // manifest.add_device_topic("my_device_id", "commands/command1", "command");

  // WITH THIS ONE LINE:
  deviceRegistry.buildManifest(manifest);
}
```

### Step 5: (Optional) Add Validation
```cpp
void handle_mqtt_command(const char *command, ...) {
  if (!deviceRegistry.isValidCommand(command)) {
    Serial.println("Unknown command");
    return;
  }
  // ... handle commands
}
```

---

## Advanced: Bidirectional Devices

Some devices both send sensors AND receive commands:

```cpp
// Define both commands and sensors
const char* motor_commands[] = {"stepperUp", "stepperDown", "stepperStop"};
const char* motor_sensors[] = {"ProximitySensors", "Position"};

// Bidirectional device
SentientDeviceDef dev_motor(
  "newell_post_motor",
  "Newell Post Motor",
  "stepper",
  motor_commands, 3,   // commands
  motor_sensors, 2     // sensors
);
```

---

**Created:** 2025-10-17
**Purpose:** Eliminate duplication in device/command definitions
**Status:** Ready to use
