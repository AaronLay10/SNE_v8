/**
 * Pilot Light Controller Example
 * Demonstrates usage of CapabilityManifest library for self-documenting controllers
 */

#include <NativeEthernet.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SentientCapabilityManifest.h>

// Controller identification
#define CONTROLLER_ID "pilot_light_001"
#define ROOM_ID "clockwork-corridor"
#define FIRMWARE_VERSION "2.0.0"
#define SKETCH_NAME "PilotLight"

// MQTT Configuration
#define MQTT_SERVER "192.168.20.3"
#define MQTT_PORT 1883
#define REGISTRATION_TOPIC "sentient/system/register"

// Hardware pins
#define LED_STRIP_PIN 6
#define FLAME_SENSOR_PIN A0
#define GAS_VALVE_RELAY_PIN 8

// Network
byte mac[] = { 0x04, 0xE9, 0xE5, 0x12, 0x34, 0x56 };

EthernetClient ethClient;
PubSubClient mqttClient(ethClient);
CapabilityManifest manifest;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Pilot Light Controller Starting...");

  // Initialize hardware pins
  pinMode(LED_STRIP_PIN, OUTPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT);
  pinMode(GAS_VALVE_RELAY_PIN, OUTPUT);
  digitalWrite(GAS_VALVE_RELAY_PIN, LOW);

  // Initialize Ethernet
  Serial.println("Configuring Ethernet...");
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    return;
  }

  Serial.print("IP Address: ");
  Serial.println(Ethernet.localIP());

  // Build capability manifest
  buildManifest();

  // Initialize MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // Connect to MQTT and register
  connectMQTT();
}

void buildManifest() {
  Serial.println("Building capability manifest...");

  // Define connected devices
  manifest.addDevice("led_strip_main", "neopixel", "Main LED Strip", LED_STRIP_PIN)
    .setPinType("digital_output")
    .addProperty("led_count", 60)
    .addProperty("color_order", "GRB")
    .addProperty("supports_rgb", true)
    .addProperty("supports_brightness", true);

  manifest.addDevice("flame_sensor", "analog_sensor", "Flame Sensor", "A0")
    .setPinType("analog_input")
    .addProperty("read_interval_ms", 100)
    .addProperty("threshold_low", 300)
    .addProperty("threshold_high", 700);

  manifest.addDevice("relay_valve", "relay", "Gas Valve Relay", GAS_VALVE_RELAY_PIN)
    .setPinType("digital_output")
    .addProperty("active_high", true)
    .addProperty("default_state", false);

  // Define published topics
  manifest.addPublishTopic(
    "sentient/paragon/clockwork/pilot_light/sensors/flame",
    "sensor_reading",
    100  // Publish every 100ms
  );

  manifest.addPublishTopic(
    "sentient/paragon/clockwork/pilot_light/heartbeat",
    "heartbeat",
    5000  // Publish every 5 seconds
  );

  // Define subscribe topics (commands)
  manifest.beginSubscribeTopic(
      "sentient/paragon/clockwork/pilot_light/commands/set_brightness",
      "Set LED strip brightness"
    )
    .addParameter("brightness", "number", true)
    .setRange(0, 255)
    .setParamDescription("Brightness level (0-255)")
    .endSubscribeTopic();

  manifest.beginSubscribeTopic(
      "sentient/paragon/clockwork/pilot_light/commands/set_color",
      "Set LED strip RGB color"
    )
    .addParameter("r", "number", true).setRange(0, 255)
    .addParameter("g", "number", true).setRange(0, 255)
    .addParameter("b", "number", true).setRange(0, 255)
    .endSubscribeTopic();

  manifest.beginSubscribeTopic(
      "sentient/paragon/clockwork/pilot_light/commands/emergency_stop",
      "Emergency stop - turn off all outputs immediately"
    )
    .setSafetyCritical(true)
    .endSubscribeTopic();

  // Define actions
  manifest.beginAction("ignite", "Ignite Pilot Light",
                      "sentient/paragon/clockwork/pilot_light/actions/ignite")
    .setActionDescription("Activates gas valve and plays flame ignition animation")
    .setDuration(2000)
    .setCanInterrupt(false)
    .addActionParameter("intensity", "number", false)
    .setRange(0, 100)
    .setDefault(100)
    .setParamDescription("Flame intensity percentage")
    .endAction();

  manifest.beginAction("extinguish", "Extinguish Pilot Light",
                      "sentient/paragon/clockwork/pilot_light/actions/extinguish")
    .setActionDescription("Closes gas valve and fades out flame animation")
    .setDuration(1500)
    .setCanInterrupt(true)
    .endAction();

  manifest.beginAction("emergency_stop", "Emergency Stop",
                      "sentient/paragon/clockwork/pilot_light/commands/emergency_stop")
    .setActionDescription("Immediately shuts off all outputs")
    .setDuration(0)
    .setCanInterrupt(true)
    .setSafetyCritical(true)
    .endAction();

  Serial.println("Capability manifest built successfully");
}

void publishRegistration() {
  Serial.println("Publishing registration message...");

  StaticJsonDocument<4096> doc;

  // Basic registration fields
  doc["controller_id"] = CONTROLLER_ID;
  doc["room_id"] = ROOM_ID;
  doc["hardware_type"] = "Teensy 4.1";
  doc["hardware_version"] = "1.0";
  doc["mcu_model"] = "TEENSY41";
  doc["clock_speed_mhz"] = F_CPU_ACTUAL / 1000000;

  // Network info
  IPAddress ip = Ethernet.localIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  doc["ip_address"] = ipStr;

  byte macAddr[6];
  Ethernet.MACAddress(macAddr);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           macAddr[0], macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
  doc["mac_address"] = macStr;

  // MQTT info
  doc["mqtt_client_id"] = CONTROLLER_ID;
  doc["mqtt_base_topic"] = "sentient/paragon/clockwork/pilot_light";
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["sketch_name"] = SKETCH_NAME;

  // Hardware capabilities
  doc["digital_pins_total"] = 55;
  doc["analog_pins_total"] = 18;
  doc["pwm_pins_total"] = 35;
  doc["i2c_buses"] = 3;
  doc["spi_buses"] = 3;
  doc["serial_ports"] = 8;
  doc["flash_size_kb"] = 8192;
  doc["ram_size_kb"] = 1024;
  doc["heartbeat_interval_ms"] = 5000;

  // Descriptive info
  doc["friendly_name"] = "Pilot Light Controller";
  doc["description"] = "Controls pilot light puzzle elements";
  doc["physical_location"] = "Wall Panel B";

  // Add capability manifest
  JsonObject manifestObj = doc.createNestedObject("capability_manifest");
  JsonObject manifestSrc = manifest.getManifest();

  // Copy manifest into registration message
  for (JsonPair kv : manifestSrc) {
    manifestObj[kv.key()] = kv.value();
  }

  // Serialize and publish
  char payload[4096];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  if (len >= sizeof(payload)) {
    Serial.println("ERROR: Registration message too large!");
    return;
  }

  if (mqttClient.publish(REGISTRATION_TOPIC, payload, false)) {
    Serial.println("Registration published successfully");
    Serial.print("Message size: ");
    Serial.print(len);
    Serial.println(" bytes");
  } else {
    Serial.println("Failed to publish registration");
  }
}

void connectMQTT() {
  while (!mqttClient.connected()) {
    Serial.println("Connecting to MQTT...");

    if (mqttClient.connect(CONTROLLER_ID)) {
      Serial.println("Connected to MQTT");

      // Publish registration message
      publishRegistration();

      // Subscribe to command topics
      mqttClient.subscribe("sentient/paragon/clockwork/pilot_light/commands/#");
      mqttClient.subscribe("sentient/paragon/clockwork/pilot_light/actions/#");

      Serial.println("Subscribed to command topics");
    } else {
      Serial.print("Failed to connect, rc=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received on topic: ");
  Serial.println(topic);

  // Parse JSON payload
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Handle commands based on topic
  String topicStr = String(topic);

  if (topicStr.endsWith("/commands/set_brightness")) {
    int brightness = doc["brightness"] | 128;
    Serial.print("Setting brightness to: ");
    Serial.println(brightness);
    // TODO: Implement LED brightness control
  }
  else if (topicStr.endsWith("/commands/set_color")) {
    int r = doc["r"] | 255;
    int g = doc["g"] | 0;
    int b = doc["b"] | 0;
    Serial.print("Setting color to RGB(");
    Serial.print(r); Serial.print(", ");
    Serial.print(g); Serial.print(", ");
    Serial.print(b); Serial.println(")");
    // TODO: Implement LED color control
  }
  else if (topicStr.endsWith("/commands/emergency_stop")) {
    Serial.println("EMERGENCY STOP!");
    digitalWrite(GAS_VALVE_RELAY_PIN, LOW);
    // TODO: Turn off all outputs
  }
  else if (topicStr.endsWith("/actions/ignite")) {
    int intensity = doc["intensity"] | 100;
    Serial.print("Igniting with intensity: ");
    Serial.println(intensity);
    // TODO: Implement ignition sequence
  }
  else if (topicStr.endsWith("/actions/extinguish")) {
    Serial.println("Extinguishing...");
    // TODO: Implement extinguish sequence
  }
}

void loop() {
  // Maintain MQTT connection
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  // TODO: Add your normal controller logic here
  // - Read sensors
  // - Publish sensor data
  // - Control outputs
  // - Publish heartbeat

  delay(100);
}
