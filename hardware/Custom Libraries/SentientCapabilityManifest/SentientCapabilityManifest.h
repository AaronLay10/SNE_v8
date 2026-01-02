/**
 * Mythra Sentient Engine - Capability Manifest Library
 * Helps Teensy controllers generate self-documenting capability manifests
 *
 * Author: Sentient Development Team
 * Version: 1.0.0
 * License: MIT
 */

#ifndef SENTIENT_CAPABILITY_MANIFEST_H
#define SENTIENT_CAPABILITY_MANIFEST_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

class SentientCapabilityManifest
{
private:
  StaticJsonDocument<4096> doc;
  JsonObject controller_info;
  JsonArray devices;
  JsonArray mqtt_topics_publish;
  JsonArray mqtt_topics_subscribe;
  JsonArray actions;
  JsonObject current_topic;
  JsonArray current_parameters;
  JsonObject current_action;
  JsonArray current_action_parameters;

public:
  SentientCapabilityManifest()
  {
    controller_info = doc.createNestedObject("controller");
    devices = doc.createNestedArray("devices");
    mqtt_topics_publish = doc.createNestedArray("mqtt_topics_publish");
    mqtt_topics_subscribe = doc.createNestedArray("mqtt_topics_subscribe");
    actions = doc.createNestedArray("actions");
  }

  /**
   * Set controller metadata
   */
  void set_controller_info(const char *unique_id, const char *friendly_name,
                           const char *firmware_version,
                           const char *room_id, const char *controller_id)
  {
    controller_info["unique_id"] = unique_id;
    controller_info["friendly_name"] = friendly_name;
    controller_info["firmware_version"] = firmware_version;
    controller_info["room_id"] = room_id;
    controller_info["controller_id"] = controller_id;
  }

  /**
   * Add a device to the manifest (new simplified API)
   */
  void add_device(const char *device_id, const char *friendly_name,
                  const char *device_type, const char *device_category,
                  const char *primary_command = nullptr)
  {
    JsonObject device = devices.add<JsonObject>();
    device["device_id"] = device_id;
    device["friendly_name"] = friendly_name;
    device["device_type"] = device_type;
    device["device_category"] = device_category;
    if (primary_command && primary_command[0] != '\0')
    {
      device["device_command_name"] = primary_command;
    }
  }

  /**
   * Add MQTT topic for a device
   */
  void add_device_topic(const char *device_id, const char *topic, const char *topic_type)
  {
    JsonObject topic_obj = mqtt_topics_publish.add<JsonObject>();
    topic_obj["device_id"] = device_id;
    topic_obj["topic"] = topic;
    topic_obj["topic_type"] = topic_type;
  }

  /**
   * Add action for a device
   */
  void add_device_action(const char *device_id, const char *action_name,
                         const char *param_type, const char *description)
  {
    JsonObject action = actions.add<JsonObject>();
    action["device_id"] = device_id;
    action["action_name"] = action_name;
    action["param_type"] = param_type;
    action["description"] = description;
  }

  /**
   * Publish registration to Sentient system in SMALL CHUNKS
   * to avoid W5500 Ethernet TX buffer overflow (2KB hardware limit)
   *
   * Sends controller info + each device separately
   */
  bool publish_registration(PubSubClient &mqtt_client, const char *room_id_uuid, const char *mqtt_device_id = "Teensy 4.1")
  {
    // Extract controller info
    const char *controller_id = controller_info["unique_id"] | "UNKNOWN";
    const char *friendly_name = controller_info["friendly_name"] | "";
    const char *firmware_version = controller_info["firmware_version"] | "";

    Serial.println(F("[CapabilityManifest] Starting registration..."));
    Serial.print(F("[CapabilityManifest] Controller: "));
    Serial.println(controller_id);
    Serial.print(F("[CapabilityManifest] Devices to register: "));
    Serial.println(devices.size());

    // STEP 1: Publish controller metadata (small, ~800 bytes)
    {
      StaticJsonDocument<2048> controller_doc;
      controller_doc["controller_id"] = controller_id;
      controller_doc["room_id"] = room_id_uuid;
      controller_doc["friendly_name"] = friendly_name;
      controller_doc["hardware_type"] = "Teensy 4.1";
      controller_doc["mcu_model"] = "ARM Cortex-M7";
      controller_doc["clock_speed_mhz"] = 600;
      controller_doc["firmware_version"] = firmware_version;
      controller_doc["digital_pins_total"] = 55;
      controller_doc["analog_pins_total"] = 18;
      controller_doc["heartbeat_interval_ms"] = 5000;
      controller_doc["controller_type"] = "microcontroller";
      controller_doc["device_count"] = devices.size(); // Tell backend how many devices to expect

      // MQTT topic structure (CRITICAL for command routing)
      const char *mqtt_namespace = "paragon"; // Always paragon for now
      const char *mqtt_room_id = controller_info["room_id"] | "";
      const char *mqtt_controller_id = controller_info["controller_id"] | "";
      controller_doc["mqtt_namespace"] = mqtt_namespace;
      controller_doc["mqtt_room_id"] = mqtt_room_id;
      controller_doc["mqtt_controller_id"] = mqtt_controller_id;

      // Add capability manifest for device sync
      JsonObject manifest = controller_doc.createNestedObject("capability_manifest");
      manifest["controller_id"] = controller_id;
      manifest["firmware_version"] = firmware_version;
      JsonArray manifest_devices = manifest.createNestedArray("devices");
      for (JsonVariant device_variant : devices)
      {
        JsonObject device = device_variant.as<JsonObject>();
        JsonObject manifest_device = manifest_devices.add<JsonObject>();
        manifest_device["device_id"] = device["device_id"];
        manifest_device["device_type"] = device["device_type"];
        manifest_device["friendly_name"] = device["friendly_name"];
        manifest_device["device_category"] = device["device_category"];
      }

      String payload;
      serializeJson(controller_doc, payload);

      Serial.print(F("[CapabilityManifest] Controller payload: "));
      Serial.print(payload.length());
      Serial.println(F(" bytes"));

      if (!mqtt_client.publish("sentient/system/register/controller", payload.c_str()))
      {
        Serial.println(F("[CapabilityManifest] Controller registration failed!"));
        return false;
      }
      Serial.println(F("[CapabilityManifest] Controller registered"));
      delay(100); // Give broker time to process
    }

    // STEP 2: Publish each device individually (small, ~200-400 bytes each)
    int device_index = 0;
    for (JsonVariant device_variant : devices)
    {
      JsonObject device = device_variant.as<JsonObject>();
      const char *device_id = device["device_id"];

      StaticJsonDocument<512> device_doc;
      device_doc["controller_id"] = controller_id;
      device_doc["device_index"] = device_index;

      // Copy all device fields
      for (JsonPair kv : device)
      {
        device_doc[kv.key()] = kv.value();
      }

      // Attach mqtt_topics for this device (enables multi-command support)
      JsonArray device_topics = device_doc.createNestedArray("mqtt_topics");
      for (JsonVariant topic_variant : mqtt_topics_publish)
      {
        JsonObject topic = topic_variant.as<JsonObject>();
        const char *topic_device_id = topic["device_id"];

        // Only include topics that belong to THIS device
        if (topic_device_id && device_id && strcmp(topic_device_id, device_id) == 0)
        {
          JsonObject topic_entry = device_topics.add<JsonObject>();
          topic_entry["topic"] = topic["topic"];
          topic_entry["topic_type"] = topic["topic_type"];
        }
      }

      String device_payload;
      serializeJson(device_doc, device_payload);

      Serial.print(F("[CapabilityManifest] Device "));
      Serial.print(device_index);
      Serial.print(F(": "));
      Serial.print(device_payload.length());
      Serial.println(F(" bytes"));

      if (!mqtt_client.publish("sentient/system/register/device", device_payload.c_str()))
      {
        Serial.print(F("[CapabilityManifest] Device "));
        Serial.print(device_index);
        Serial.println(F(" registration failed!"));
        return false;
      }

      device_index++;
      delay(50); // Small delay between device registrations
    }

    Serial.print(F("[CapabilityManifest] Registration complete! "));
    Serial.print(device_index);
    Serial.println(F(" devices registered"));

    return true;
  }

  /**
   * Add a device to the manifest (legacy API)
   */
  SentientCapabilityManifest &addDevice(const char *deviceId, const char *deviceType,
                                        const char *friendlyName, int pin)
  {
    JsonObject device = devices.add<JsonObject>();
    device["device_id"] = deviceId;
    device["device_type"] = deviceType;
    device["friendly_name"] = friendlyName;
    device["pin"] = pin;
    return *this;
  }

  /**
   * Add a device with string pin designation (e.g., "A0")
   */
  SentientCapabilityManifest &addDevice(const char *deviceId, const char *deviceType,
                                        const char *friendlyName, const char *pin)
  {
    JsonObject device = devices.add<JsonObject>();
    device["device_id"] = deviceId;
    device["device_type"] = deviceType;
    device["friendly_name"] = friendlyName;
    device["pin"] = pin;
    return *this;
  }

  /**
   * Set pin type for the last added device
   */
  SentientCapabilityManifest &setPinType(const char *pinType)
  {
    if (devices.size() > 0)
    {
      devices[devices.size() - 1]["pin_type"] = pinType;
    }
    return *this;
  }

  /**
   * Add properties to the last added device
   */
  SentientCapabilityManifest &addProperty(const char *key, int value)
  {
    if (devices.size() > 0)
    {
      JsonObject props = devices[devices.size() - 1]["properties"].as<JsonObject>();
      if (!props)
      {
        props = devices[devices.size() - 1]["properties"].to<JsonObject>();
      }
      props[key] = value;
    }
    return *this;
  }

  SentientCapabilityManifest &addProperty(const char *key, const char *value)
  {
    if (devices.size() > 0)
    {
      JsonObject props = devices[devices.size() - 1]["properties"].as<JsonObject>();
      if (!props)
      {
        props = devices[devices.size() - 1]["properties"].to<JsonObject>();
      }
      props[key] = value;
    }
    return *this;
  }

  SentientCapabilityManifest &addProperty(const char *key, bool value)
  {
    if (devices.size() > 0)
    {
      JsonObject props = devices[devices.size() - 1]["properties"].as<JsonObject>();
      if (!props)
      {
        props = devices[devices.size() - 1]["properties"].to<JsonObject>();
      }
      props[key] = value;
    }
    return *this;
  }

  /**
   * Add a published MQTT topic
   */
  SentientCapabilityManifest &addPublishTopic(const char *topic, const char *messageType, int intervalMs = 0)
  {
    JsonObject pub = mqtt_topics_publish.add<JsonObject>();
    pub["topic"] = topic;
    pub["message_type"] = messageType;
    if (intervalMs > 0)
    {
      pub["publish_interval_ms"] = intervalMs;
    }
    return *this;
  }

  /**
   * Start defining a subscribe topic (command topic)
   */
  SentientCapabilityManifest &beginSubscribeTopic(const char *topic, const char *description = nullptr)
  {
    current_topic = mqtt_topics_subscribe.add<JsonObject>();
    current_topic["topic"] = topic;
    if (description)
    {
      current_topic["description"] = description;
    }
    current_parameters = current_topic["parameters"].to<JsonArray>();
    return *this;
  }

  /**
   * Add a parameter to the current subscribe topic
   */
  SentientCapabilityManifest &addParameter(const char *name, const char *type, bool required = false)
  {
    if (!current_parameters)
      return *this;
    JsonObject param = current_parameters.add<JsonObject>();
    param["name"] = name;
    param["type"] = type;
    param["required"] = required;
    return *this;
  }

  /**
   * Set min/max range for the last parameter
   */
  SentientCapabilityManifest &setRange(int min, int max)
  {
    if (!current_parameters || current_parameters.size() == 0)
      return *this;
    JsonObject param = current_parameters[current_parameters.size() - 1];
    param["min"] = min;
    param["max"] = max;
    return *this;
  }

  /**
   * Set default value for the last parameter
   */
  SentientCapabilityManifest &setDefault(int value)
  {
    if (!current_parameters || current_parameters.size() == 0)
      return *this;
    current_parameters[current_parameters.size() - 1]["default"] = value;
    return *this;
  }

  SentientCapabilityManifest &setDefault(const char *value)
  {
    if (!current_parameters || current_parameters.size() == 0)
      return *this;
    current_parameters[current_parameters.size() - 1]["default"] = value;
    return *this;
  }

  /**
   * Set parameter description
   */
  SentientCapabilityManifest &setParamDescription(const char *desc)
  {
    if (!current_parameters || current_parameters.size() == 0)
      return *this;
    current_parameters[current_parameters.size() - 1]["description"] = desc;
    return *this;
  }

  /**
   * Mark current topic as safety critical
   */
  SentientCapabilityManifest &setSafetyCritical(bool critical = true)
  {
    if (current_topic)
    {
      current_topic["safety_critical"] = critical;
    }
    else if (current_action)
    {
      current_action["safety_critical"] = critical;
    }
    return *this;
  }

  /**
   * Finish defining the current subscribe topic
   */
  SentientCapabilityManifest &endSubscribeTopic()
  {
    current_topic = JsonObject();
    current_parameters = JsonArray();
    return *this;
  }

  /**
   * Start defining an action
   */
  SentientCapabilityManifest &beginAction(const char *actionId, const char *friendlyName,
                                          const char *mqttTopic = nullptr)
  {
    current_action = actions.add<JsonObject>();
    current_action["action_id"] = actionId;
    current_action["friendly_name"] = friendlyName;
    if (mqttTopic)
    {
      current_action["mqtt_topic"] = mqttTopic;
    }
    current_action_parameters = current_action["parameters"].to<JsonArray>();
    return *this;
  }

  /**
   * Set action description
   */
  SentientCapabilityManifest &setActionDescription(const char *desc)
  {
    if (current_action)
    {
      current_action["description"] = desc;
    }
    return *this;
  }

  /**
   * Set action duration
   */
  SentientCapabilityManifest &setDuration(int durationMs)
  {
    if (current_action)
    {
      current_action["duration_ms"] = durationMs;
    }
    return *this;
  }

  /**
   * Set whether action can be interrupted
   */
  SentientCapabilityManifest &setCanInterrupt(bool can = true)
  {
    if (current_action)
    {
      current_action["can_interrupt"] = can;
    }
    return *this;
  }

  /**
   * Add parameter to current action (using action parameters array)
   */
  SentientCapabilityManifest &addActionParameter(const char *name, const char *type, bool required = false)
  {
    if (!current_action_parameters)
      return *this;
    JsonObject param = current_action_parameters.add<JsonObject>();
    param["name"] = name;
    param["type"] = type;
    param["required"] = required;
    // Switch context to action parameters for subsequent calls
    current_parameters = current_action_parameters;
    return *this;
  }

  /**
   * Finish defining the current action
   */
  SentientCapabilityManifest &endAction()
  {
    current_action = JsonObject();
    current_action_parameters = JsonArray();
    current_parameters = JsonArray();
    return *this;
  }

  /**
   * Get the JSON manifest as a string
   */
  String toJson()
  {
    String output;
    serializeJson(doc, output);
    return output;
  }

  /**
   * Get the manifest as a JsonObject for embedding in registration message
   */
  JsonObject getManifest()
  {
    return doc.as<JsonObject>();
  }

  /**
   * Print manifest to Serial for debugging
   */
  void printToSerial()
  {
    serializeJsonPretty(doc, Serial);
    Serial.println();
  }
};

#endif // SENTIENT_CAPABILITY_MANIFEST_H

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
