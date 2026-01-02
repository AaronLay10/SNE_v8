// Pilaster v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════
// STATELESS ARCHITECTURE:
// - 4 button sensors (Green/Silver/Red/Black) publish events on state change
// - No game logic - Sentient makes all decisions
// - Commands execute immediately, publish confirmation
// - Heartbeat every 5 seconds for connection monitoring
// ══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientCapabilityManifest.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>
#include "controller_naming.h"
#include "FirmwareMetadata.h"

using namespace naming;

SentientMQTTConfig build_mqtt_config()
{
  SentientMQTTConfig cfg;
  cfg.brokerHost = "mqtt.sentientengine.ai";
  cfg.brokerIp = IPAddress(192, 168, 2, 3);
  cfg.brokerPort = 1883;
  cfg.username = "paragon_devices";
  cfg.password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
  cfg.namespaceId = CLIENT_ID;
  cfg.roomId = ROOM_ID;
  cfg.controllerId = CONTROLLER_ID;
  cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
  cfg.displayName = CONTROLLER_FRIENDLY_NAME;
  cfg.useDhcp = true;
  cfg.autoHeartbeat = true;
  cfg.heartbeatIntervalMs = 5000;
  return cfg;
}

SentientMQTT sentient(build_mqtt_config());
SentientCapabilityManifest manifest;
SentientDeviceRegistry deviceRegistry;

void build_capability_manifest()
{
  manifest.set_controller_info(
      CONTROLLER_ID,
      CONTROLLER_FRIENDLY_NAME,
      firmware::VERSION,
      ROOM_ID,
      CONTROLLER_ID);
}

const int powerLED = 13;

// Pin Assignments for buttons
const int button1 = 12; // Green button
const int button2 = 10; // Silver button
const int button3 = 9;  // Red button
const int button4 = 11; // Black button

// Button state variables
bool buttonState1 = false, buttonState2 = false, buttonState3 = false, buttonState4 = false;
bool lastButtonState1 = false, lastButtonState2 = false, lastButtonState3 = false, lastButtonState4 = false;

// Debounce timing
unsigned long lastDebounceTime1 = 0, lastDebounceTime2 = 0, lastDebounceTime3 = 0, lastDebounceTime4 = 0;
const unsigned long debounceDelay = 50; // 50ms debounce delay

// Heartbeat timing
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 5000; // 5 seconds

void setup()
{
  // On-board LED setup
  pinMode(powerLED, OUTPUT);
  digitalWrite(powerLED, HIGH);

  Serial.begin(115200);
  while (!Serial && millis() < 3000)
  {
    delay(10);
  }

  Serial.println("\n═══════════════════════════════════════════════════");
  Serial.println("  Pilaster Controller v2.3.0");
  Serial.println("  STATELESS EXECUTOR Architecture");
  Serial.println("═══════════════════════════════════════════════════");
  Serial.print("  Version: ");
  Serial.println(firmware::VERSION);
  Serial.print("  Build Date: ");
  Serial.println(firmware::BUILD_DATE);
  Serial.print("  Controller ID: ");
  Serial.println(CONTROLLER_ID);
  Serial.println("═══════════════════════════════════════════════════\n");

  // Button setup with internal pull-down resistors for active HIGH
  Serial.println("Setting up Button Sensors...");
  pinMode(button1, INPUT_PULLDOWN);
  pinMode(button2, INPUT_PULLDOWN);
  pinMode(button3, INPUT_PULLDOWN);
  pinMode(button4, INPUT_PULLDOWN);

  // Build capability manifest
  Serial.println("[Pilaster] Building capability manifest...");
  build_capability_manifest();
  Serial.println("[Pilaster] Manifest built successfully");

  // Initialize Sentient MQTT
  Serial.println("[Pilaster] Initializing MQTT...");
  if (!sentient.begin())
  {
    Serial.println("[Pilaster] MQTT initialization failed - continuing without network");
  }
  else
  {
    Serial.println("[Pilaster] MQTT initialization successful");
  }

  Serial.println("✓ Pilaster Controller Ready!");
  Serial.println("  Waiting for commands from Sentient...\n");
}

void loop()
{
  // Handle MQTT loop and registration
  static bool registered = false;
  sentient.loop();

  // Register after MQTT connection is established
  if (!registered && sentient.isConnected())
  {
    Serial.println("[Pilaster] Registering with Sentient system...");
    if (manifest.publish_registration(sentient.get_client(), ROOM_ID, CONTROLLER_ID))
    {
      Serial.println("[Pilaster] Registration successful!");
      registered = true;
    }
    else
    {
      Serial.println("[Pilaster] Registration failed - will retry on next loop");
    }
  }

  // Read and debounce all buttons
  checkButton(button1, buttonState1, lastButtonState1, lastDebounceTime1, "green_button");  // Green
  checkButton(button2, buttonState2, lastButtonState2, lastDebounceTime2, "silver_button"); // Silver
  checkButton(button3, buttonState3, lastButtonState3, lastDebounceTime3, "red_button");    // Red
  checkButton(button4, buttonState4, lastButtonState4, lastDebounceTime4, "black_button");  // Black

  // Send heartbeat every 5 seconds
  unsigned long currentTime = millis();
  if (currentTime - lastHeartbeat >= heartbeatInterval)
  {
    sentient.publishHeartbeat();
    lastHeartbeat = currentTime;
  }
}

void checkButton(int pin, bool &currentState, bool &lastState, unsigned long &lastDebounceTime, const char *device_id)
{
  bool reading = digitalRead(pin); // Direct reading for active HIGH with pull-down

  if (reading != lastState)
  {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    if (reading != currentState)
    {
      currentState = reading;

      // Publish on state change (both press and release)
      sentient.publishText(
          "sensors",
          device_id,
          currentState ? "pressed" : "released");

      Serial.print("[SENSOR] ");
      Serial.print(device_id);
      Serial.print(": ");
      Serial.println(currentState ? "pressed" : "released");
    }
  }

  lastState = reading;
}
