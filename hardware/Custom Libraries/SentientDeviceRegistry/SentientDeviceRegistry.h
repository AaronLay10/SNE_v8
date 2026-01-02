/*
 * SentientDeviceRegistry.h
 *
 * Single Source of Truth for device and command definitions.
 *
 * PHILOSOPHY:
 * - Define each device ONCE in your controller code
 * - Manifest is auto-generated from these definitions
 * - Topics are auto-generated from these definitions
 * - Impossible for things to get out of sync
 *
 * USAGE:
 * 1. Define devices in your .ino file
 * 2. Call buildManifestFromRegistry() to auto-generate manifest
 * 3. That's it!
 */

#ifndef SENTIENT_DEVICE_REGISTRY_H
#define SENTIENT_DEVICE_REGISTRY_H

#include <Arduino.h>
#include <SentientCapabilityManifest.h>

// Maximum number of commands or sensors per device
#define MAX_TOPICS_PER_DEVICE 10

// ============================================================================
// DEVICE DEFINITION STRUCTURE
// ============================================================================

struct SentientDeviceDef
{
  const char *device_id;     // Unique ID (e.g., "boiler_fire_leds")
  const char *friendly_name; // Human-readable name
  const char *device_type;   // Type: "relay", "sensor", "led_strip", etc.
  const char *category;      // "input", "output", "bidirectional"

  // Commands this device responds to (output devices)
  const char *commands[MAX_TOPICS_PER_DEVICE];
  int command_count;

  // Sensor topics this device publishes (input devices)
  const char *sensors[MAX_TOPICS_PER_DEVICE];
  int sensor_count;

  // Constructor for output device with commands
  SentientDeviceDef(const char *id, const char *name, const char *type,
                    const char **cmds, int cmd_count)
      : device_id(id), friendly_name(name), device_type(type),
        category("output"), command_count(cmd_count), sensor_count(0)
  {
    for (int i = 0; i < cmd_count && i < MAX_TOPICS_PER_DEVICE; i++)
    {
      commands[i] = cmds[i];
    }
    for (int i = cmd_count; i < MAX_TOPICS_PER_DEVICE; i++)
    {
      commands[i] = nullptr;
    }
    for (int i = 0; i < MAX_TOPICS_PER_DEVICE; i++)
    {
      sensors[i] = nullptr;
    }
  }

  // Constructor for input device with sensors
  SentientDeviceDef(const char *id, const char *name, const char *type,
                    const char **snsr, int snsr_count, bool is_input)
      : device_id(id), friendly_name(name), device_type(type),
        category("input"), command_count(0), sensor_count(snsr_count)
  {
    for (int i = 0; i < snsr_count && i < MAX_TOPICS_PER_DEVICE; i++)
    {
      sensors[i] = snsr[i];
    }
    for (int i = snsr_count; i < MAX_TOPICS_PER_DEVICE; i++)
    {
      sensors[i] = nullptr;
    }
    for (int i = 0; i < MAX_TOPICS_PER_DEVICE; i++)
    {
      commands[i] = nullptr;
    }
  }

  // Constructor for bidirectional device
  SentientDeviceDef(const char *id, const char *name, const char *type,
                    const char **cmds, int cmd_count,
                    const char **snsr, int snsr_count)
      : device_id(id), friendly_name(name), device_type(type),
        category("bidirectional"), command_count(cmd_count), sensor_count(snsr_count)
  {
    for (int i = 0; i < cmd_count && i < MAX_TOPICS_PER_DEVICE; i++)
    {
      commands[i] = cmds[i];
    }
    for (int i = cmd_count; i < MAX_TOPICS_PER_DEVICE; i++)
    {
      commands[i] = nullptr;
    }
    for (int i = 0; i < snsr_count && i < MAX_TOPICS_PER_DEVICE; i++)
    {
      sensors[i] = snsr[i];
    }
    for (int i = snsr_count; i < MAX_TOPICS_PER_DEVICE; i++)
    {
      sensors[i] = nullptr;
    }
  }
};

// ============================================================================
// DEVICE REGISTRY
// ============================================================================

class SentientDeviceRegistry
{
private:
  SentientDeviceDef **devices;
  int device_count;
  int max_devices;

public:
  SentientDeviceRegistry(int max_devs = 40)
      : device_count(0), max_devices(max_devs)
  {
    devices = new SentientDeviceDef *[max_devices];
    for (int i = 0; i < max_devices; i++)
    {
      devices[i] = nullptr;
    }
  }

  ~SentientDeviceRegistry()
  {
    delete[] devices;
  }

  // Add a device to the registry
  bool addDevice(SentientDeviceDef *device)
  {
    if (device_count >= max_devices)
    {
      Serial.println(F("[Registry] ERROR: Max devices reached"));
      return false;
    }
    devices[device_count++] = device;
    return true;
  }

  // Build manifest from all registered devices
  void buildManifest(SentientCapabilityManifest &manifest)
  {
    Serial.print(F("[Registry] Building manifest for "));
    Serial.print(device_count);
    Serial.println(F(" devices"));

    for (int i = 0; i < device_count; i++)
    {
      SentientDeviceDef *dev = devices[i];
      if (!dev)
        continue;

      // Add device with primary command name (first command in the list)
      const char *primary_command = (dev->command_count > 0) ? dev->commands[0] : nullptr;
      manifest.add_device(dev->device_id, dev->friendly_name,
                          dev->device_type, dev->category, primary_command);

      // Add command topics
      for (int j = 0; j < dev->command_count && j < MAX_TOPICS_PER_DEVICE; j++)
      {
        if (dev->commands[j])
        {
          String topic = String("commands/") + dev->commands[j];
          manifest.add_device_topic(dev->device_id, topic.c_str(), "command");

          Serial.print(F("  [Registry] Added command: "));
          Serial.print(dev->device_id);
          Serial.print(F(" -> "));
          Serial.println(topic);
        }
      }

      // Add sensor topics
      for (int j = 0; j < dev->sensor_count && j < MAX_TOPICS_PER_DEVICE; j++)
      {
        if (dev->sensors[j])
        {
          String topic = String("sensors/") + dev->sensors[j];
          manifest.add_device_topic(dev->device_id, topic.c_str(), "sensor");

          Serial.print(F("  [Registry] Added sensor: "));
          Serial.print(dev->device_id);
          Serial.print(F(" -> "));
          Serial.println(topic);
        }
      }
    }

    Serial.println(F("[Registry] Manifest build complete"));
  }

  // Get device count
  int getDeviceCount() const { return device_count; }

  // Get device by index
  SentientDeviceDef *getDevice(int index)
  {
    if (index >= 0 && index < device_count)
    {
      return devices[index];
    }
    return nullptr;
  }

  // Find device by ID
  SentientDeviceDef *findDevice(const char *device_id)
  {
    for (int i = 0; i < device_count; i++)
    {
      if (devices[i] && strcmp(devices[i]->device_id, device_id) == 0)
      {
        return devices[i];
      }
    }
    return nullptr;
  }

  // Check if command exists for any device
  bool isValidCommand(const char *command)
  {
    for (int i = 0; i < device_count; i++)
    {
      SentientDeviceDef *dev = devices[i];
      if (!dev)
        continue;

      for (int j = 0; j < dev->command_count; j++)
      {
        if (dev->commands[j] && strcmp(dev->commands[j], command) == 0)
        {
          return true;
        }
      }
    }
    return false;
  }

  // Print registry summary
  void printSummary()
  {
    Serial.println(F("\n========================================"));
    Serial.println(F("DEVICE REGISTRY SUMMARY"));
    Serial.println(F("========================================"));
    Serial.print(F("Total Devices: "));
    Serial.println(device_count);

    for (int i = 0; i < device_count; i++)
    {
      SentientDeviceDef *dev = devices[i];
      if (!dev)
        continue;

      Serial.println();
      Serial.print(F("Device: "));
      Serial.println(dev->friendly_name);
      Serial.print(F("  ID: "));
      Serial.println(dev->device_id);
      Serial.print(F("  Type: "));
      Serial.println(dev->device_type);
      Serial.print(F("  Category: "));
      Serial.println(dev->category);

      if (dev->command_count > 0)
      {
        Serial.println(F("  Commands:"));
        for (int j = 0; j < dev->command_count; j++)
        {
          if (dev->commands[j])
          {
            Serial.print(F("    - "));
            Serial.println(dev->commands[j]);
          }
        }
      }

      if (dev->sensor_count > 0)
      {
        Serial.println(F("  Sensors:"));
        for (int j = 0; j < dev->sensor_count; j++)
        {
          if (dev->sensors[j])
          {
            Serial.print(F("    - "));
            Serial.println(dev->sensors[j]);
          }
        }
      }
    }
    Serial.println(F("========================================\n"));
  }
};

#endif // SENTIENT_DEVICE_REGISTRY_H
