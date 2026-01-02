# SentientSentientCapabilityManifest Library

A helper library for Teensy 4.1 controllers to generate self-documenting capability manifests for the Sentient Engine.

## Overview

The SentientCapabilityManifest library makes it easy to create detailed capability descriptions for your controllers. Instead of manually writing JSON, you use a fluent API to build the manifest programmatically.

## Features

- **Fluent API**: Chain method calls for readable manifest building
- **Type safety**: Compile-time validation of manifest structure
- **Self-documenting**: Manifests describe devices, topics, and actions
- **Auto-registration**: Include manifest in registration message
- **Small footprint**: Uses ArduinoJson for efficient JSON handling

## Installation

1. Copy the `SentientCapabilityManifest` folder to your Arduino libraries directory:
   - **Teensy**: `/home/techadmin/Arduino/libraries/`
   - **Windows**: `Documents/Arduino/libraries/`
   - **Mac**: `Documents/Arduino/libraries/`

2. Restart Arduino IDE

3. Include in your sketch:
   ```cpp
   #include <SentientCapabilityManifest.h>
   ```

## Dependencies

- **ArduinoJson** (v6.x or later) - Install via Library Manager

## Quick Start

```cpp
#include <SentientCapabilityManifest.h>

SentientCapabilityManifest manifest;

void setup() {
  // Define a device
  manifest.addDevice("led_strip", "neopixel", "Main LED Strip", 6)
    .setPinType("digital_output")
    .addProperty("led_count", 60);

  // Define a published topic
  manifest.addPublishTopic(
    "sentient/paragon/room/controller/sensors/temp",
    "sensor_reading",
    1000  // Publish every 1000ms
  );

  // Define a command topic
  manifest.beginSubscribeTopic(
      "sentient/paragon/room/controller/commands/set_brightness",
      "Set brightness"
    )
    .addParameter("brightness", "number", true)
    .setRange(0, 255)
    .endSubscribeTopic();

  // Define an action
  manifest.beginAction("activate", "Activate Puzzle",
                      "sentient/paragon/room/controller/actions/activate")
    .setDuration(2000)
    .setCanInterrupt(false)
    .endAction();

  // Get JSON string
  String json = manifest.toJson();
  Serial.println(json);
}
```

## API Reference

### Devices

#### `addDevice(deviceId, deviceType, friendlyName, pin)`
Add a device to the manifest.

**Parameters:**
- `deviceId` (const char*): Unique device identifier
- `deviceType` (const char*): Type of device (e.g., "neopixel", "analog_sensor", "relay")
- `friendlyName` (const char*): Human-readable name
- `pin` (int or const char*): Pin number or designation (e.g., 6, "A0")

**Returns:** Reference to manifest for chaining

**Example:**
```cpp
manifest.addDevice("temp_sensor", "analog_sensor", "Temperature Sensor", "A0");
```

#### `setPinType(pinType)`
Set the pin type for the last added device.

**Parameters:**
- `pinType` (const char*): Pin type (e.g., "digital_output", "analog_input", "pwm")

**Example:**
```cpp
manifest.addDevice("led", "led", "Status LED", 13)
  .setPinType("digital_output");
```

#### `addProperty(key, value)`
Add a property to the last added device.

**Parameters:**
- `key` (const char*): Property name
- `value` (int, const char*, or bool): Property value

**Example:**
```cpp
manifest.addDevice("strip", "neopixel", "LED Strip", 6)
  .addProperty("led_count", 60)
  .addProperty("color_order", "GRB")
  .addProperty("supports_rgb", true);
```

### Published Topics

#### `addPublishTopic(topic, messageType, intervalMs)`
Add a topic that this controller publishes to.

**Parameters:**
- `topic` (const char*): Full MQTT topic path
- `messageType` (const char*): Type of message (e.g., "sensor_reading", "heartbeat")
- `intervalMs` (int, optional): Publish interval in milliseconds

**Example:**
```cpp
manifest.addPublishTopic(
  "sentient/paragon/clockwork/pilot/sensors/flame",
  "sensor_reading",
  100
);
```

### Subscribe Topics (Commands)

#### `beginSubscribeTopic(topic, description)`
Start defining a topic this controller subscribes to.

**Parameters:**
- `topic` (const char*): Full MQTT topic path
- `description` (const char*, optional): Description of what this command does

**Returns:** Reference to manifest for chaining

#### `addParameter(name, type, required)`
Add a parameter to the current subscribe topic.

**Parameters:**
- `name` (const char*): Parameter name
- `type` (const char*): Parameter type ("number", "string", "boolean")
- `required` (bool): Whether parameter is required

**Returns:** Reference to manifest for chaining

#### `setRange(min, max)`
Set min/max range for the last added number parameter.

**Parameters:**
- `min` (int): Minimum value
- `max` (int): Maximum value

**Returns:** Reference to manifest for chaining

#### `setDefault(value)`
Set default value for the last added parameter.

**Parameters:**
- `value` (int or const char*): Default value

**Returns:** Reference to manifest for chaining

#### `setParamDescription(description)`
Set description for the last added parameter.

**Parameters:**
- `description` (const char*): Parameter description

**Returns:** Reference to manifest for chaining

#### `setSafetyCritical(critical)`
Mark the current topic or action as safety critical.

**Parameters:**
- `critical` (bool): Whether this is safety critical (default: true)

**Returns:** Reference to manifest for chaining

#### `endSubscribeTopic()`
Finish defining the current subscribe topic.

**Returns:** Reference to manifest for chaining

**Example:**
```cpp
manifest.beginSubscribeTopic(
    "sentient/paragon/room/controller/commands/set_brightness",
    "Set LED brightness"
  )
  .addParameter("brightness", "number", true)
  .setRange(0, 255)
  .setDefault(128)
  .setParamDescription("Brightness level (0-255)")
  .endSubscribeTopic();
```

### Actions

#### `beginAction(actionId, friendlyName, mqttTopic)`
Start defining an action.

**Parameters:**
- `actionId` (const char*): Unique action identifier
- `friendlyName` (const char*): Human-readable action name
- `mqttTopic` (const char*, optional): MQTT topic to trigger this action

**Returns:** Reference to manifest for chaining

#### `setActionDescription(description)`
Set description for the current action.

**Parameters:**
- `description` (const char*): Action description

**Returns:** Reference to manifest for chaining

#### `setDuration(durationMs)`
Set expected duration of the action.

**Parameters:**
- `durationMs` (int): Duration in milliseconds

**Returns:** Reference to manifest for chaining

#### `setCanInterrupt(can)`
Set whether the action can be interrupted.

**Parameters:**
- `can` (bool): Whether action can be interrupted (default: true)

**Returns:** Reference to manifest for chaining

#### `addActionParameter(name, type, required)`
Add a parameter to the current action.

**Parameters:**
- `name` (const char*): Parameter name
- `type` (const char*): Parameter type
- `required` (bool): Whether parameter is required

**Note:** After calling this, you can use `setRange()`, `setDefault()`, and `setParamDescription()` for the action parameter.

**Returns:** Reference to manifest for chaining

#### `endAction()`
Finish defining the current action.

**Returns:** Reference to manifest for chaining

**Example:**
```cpp
manifest.beginAction("ignite", "Ignite Pilot Light",
                    "sentient/paragon/room/pilot/actions/ignite")
  .setActionDescription("Activates gas valve and ignition sequence")
  .setDuration(2000)
  .setCanInterrupt(false)
  .addActionParameter("intensity", "number", false)
  .setRange(0, 100)
  .setDefault(100)
  .endAction();
```

### Output

#### `toJson()`
Get the manifest as a JSON string.

**Returns:** String containing JSON manifest

**Example:**
```cpp
String json = manifest.toJson();
Serial.println(json);
```

#### `getManifest()`
Get the manifest as a JsonObject for embedding in registration messages.

**Returns:** JsonObject reference

**Example:**
```cpp
JsonObject manifestObj = doc.createNestedObject("capability_manifest");
JsonObject manifestSrc = manifest.getManifest();
for (JsonPair kv : manifestSrc) {
  manifestObj[kv.key()] = kv.value();
}
```

#### `printToSerial()`
Print formatted manifest to Serial for debugging.

**Example:**
```cpp
manifest.printToSerial();
```

## Complete Example

See `examples/PilotLightExample/PilotLightExample.ino` for a complete working example.

## Best Practices

1. **Build manifest in setup()** - Create the manifest once during setup
2. **Include in registration** - Send manifest with initial registration message
3. **Use descriptive names** - Make device and action names clear and meaningful
4. **Document parameters** - Always include descriptions for parameters
5. **Set safety flags** - Mark emergency stop and dangerous actions as safety critical
6. **Define realistic ranges** - Set min/max values that match your hardware limits
7. **Keep it updated** - Update manifest when you change hardware or capabilities

## Troubleshooting

### "Manifest too large" error

If your manifest exceeds the 4096-byte StaticJsonDocument:

1. Reduce description lengths
2. Remove unnecessary properties
3. Increase document size in SentientCapabilityManifest.h:
   ```cpp
   StaticJsonDocument<8192> doc;  // Increase from 4096
   ```

### Registration message too large

If the complete registration message exceeds buffer size:

1. Increase buffer in your sketch:
   ```cpp
   char payload[6144];  // Increase from 4096
   ```

2. Consider simplifying manifest or splitting into multiple messages

### Manifest not appearing in database

1. Check device-monitor logs: `pm2 logs sentient-device-monitor`
2. Verify JSON is valid: Use manifest.printToSerial() to check
3. Ensure capability_manifest column exists in database

## Version History

- **1.0.0** - Initial release with device, topic, and action support

## License

MIT License - See LICENSE file for details

## Support

For questions or issues:
- GitHub Issues: [sentient/issues](https://github.com/anthropics/sentient/issues)
- Documentation: `/opt/sentient/docs/CONTROLLER_SELF_REGISTRATION.md`
