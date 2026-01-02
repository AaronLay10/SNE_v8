// Floor v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ═══════════════════════════════════════════════════════════════════════════════
// ⚠️  WARNING: This controller contains embedded game logic that violates
// ⚠️  STATELESS architecture. Sequence validation, beat tracking, and state
// ⚠️  machine logic must be moved to Sentient backend. Current implementation
// ⚠️  is v2.3.0 library upgrade only - game logic extraction required.
// ═══════════════════════════════════════════════════════════════════════════════
// HARDWARE:
// - 9 floor button sensors with debouncing
// - 9 FastLED WS2812B strips (60 LEDs each = 540 total LEDs)
// - DM542 stepper motor for drawer mechanism (differential signaling)
// - 4 proximity sensors (drawer open/close main/sub)
// - Drawer maglock, cuckoo solenoid, IR sensor, photocell
// - COB lights, crystal light, lever RGB LED
// ═══════════════════════════════════════════════════════════════════════════════

#include <Wire.h>
#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <SentientCapabilityManifest.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN // Suppress IRremote begin() error
#include <IRremote.hpp>
#include "controller_naming.h"
#include "FirmwareMetadata.h"

#define POWERLED 13

// Mechanical
#define DRAWERMAGLOCK 14
#define CUCKCOOSOLENOID 18
#define PHOTOCELL A16 // Pin 40
#define IRSENSOR 12

// Lights
#define DRAWERCOBLIGHTS 15
#define CRYSTALLIGHT 37

// Proximity Sensor Pins
#define DRAWEROPENED_MAIN 35
#define DRAWEROPENED_SUB 36
#define DRAWERCLOSED_MAIN 33
#define DRAWERCLOSED_SUB 34

// Motor Pins
#define LEVERRGBLED 10
#define MOTOR_PULPOS 22
#define MOTOR_DIRPOS 21
#define MOTOR_PULNEG 23
#define MOTOR_DIRNEG 20

// LED Pin Definitions
#define LED1 3
#define LED2 0
#define LED3 6
#define LED4 4
#define LED5 1
#define LED6 7
#define LED7 5
#define LED8 2
#define LED9 8

// Floor Button Pin Definitions
#define BUTTON1 27
#define BUTTON2 24
#define BUTTON3 30
#define BUTTON4 28
#define BUTTON5 25
#define BUTTON6 32
#define BUTTON7 29
#define BUTTON8 26
#define BUTTON9 31

using namespace naming;

// MQTT Configuration helper
SentientMQTTConfig build_mqtt_config()
{
  SentientMQTTConfig cfg;
  cfg.brokerHost = "mqtt.sentientengine.ai";
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

  // Floor controller is too complex for full device registry
  // Minimal manifest will still allow registration
}

// LED pin mapping
const int ledPins[] = {LED1, LED2, LED3, LED4, LED5, LED6, LED7, LED8, LED9};

// FastLED configuration
#define NUM_STRIPS 9
#define LEDS_PER_STRIP 60
#define TOTAL_LEDS (NUM_STRIPS * LEDS_PER_STRIP)
CRGB leds[TOTAL_LEDS];

// LED strip data pins (one for each button/strip)
const int stripDataPins[] = {LED1, LED2, LED3, LED4, LED5, LED6, LED7, LED8, LED9};

// Sequence definitions - using 1-based strip numbers (not pin numbers)
const int sequence1[][2] = {
    {2, 0}, {6, 0}, {3, 0}, {9, 0}, {2, 0}, {4, 0}, {5, 0}, {2, 0}};

const int sequence2[][2] = {
    {9, 3}, {2, 1}, {8, 0}, {5, 7}, {2, 0}, {6, 0}, {6, 9}, {1, 0}};

const int sequence3[][3] = {
    {3, 1, 7}, {6, 2, 8}, {9, 1, 0}, {4, 6, 1}, {8, 7, 6}, {4, 2, 0}, {5, 3, 0}, {1, 5, 9}};

// Floor Button Pins
const int floorButtons[] = {BUTTON1, BUTTON2, BUTTON3, BUTTON4, BUTTON5, BUTTON6, BUTTON7, BUTTON8, BUTTON9};
int state = 0; // Current state of the puzzle

// Sequence timing variables
unsigned long lastSequenceUpdate = 0;
int currentSequenceStep = 0;
const unsigned long SEQUENCE_STEP_DELAY = 1600; // 1.6 seconds between steps
bool sequenceStarted = false;

// Sequence 2 timing variables
unsigned long lastSequence2Update = 0;
int currentSequence2Step = 0;
bool sequence2Started = false;

// Sequence 3 timing variables
unsigned long lastSequence3Update = 0;
int currentSequence3Step = 0;
bool sequence3Started = false;

// Button testing variables
unsigned long lastButtonCheck = 0;
const unsigned long BUTTON_CHECK_DELAY = 500; // Check buttons every 500ms

// Button press tracking for sequences
bool buttonPressed[9] = {false};   // Track button states
bool lastButtonState[9] = {false}; // Track previous button states
bool correctPress[9] = {false};    // Track correct presses in current step
bool wrongPress[9] = {false};      // Track wrong presses in current step
int totalCorrectPresses = 0;       // Track total correct presses in sequence
int totalExpectedPresses = 0;      // Track total expected presses in sequence
int totalWrongPresses = 0;         // Track total wrong presses in sequence
unsigned long replayStartTime = 0; // Time when replay countdown starts
bool waitingToReplay = false;      // Flag for replay waiting state

// Sequence 2 replay variables
unsigned long replay2StartTime = 0;
bool waitingToReplay2 = false;
int totalCorrectPresses2 = 0;
int totalExpectedPresses2 = 0;
int totalWrongPresses2 = 0;

// Sequence 3 replay variables
unsigned long replay3StartTime = 0;
bool waitingToReplay3 = false;
int totalCorrectPresses3 = 0;
int totalExpectedPresses3 = 0;
int totalWrongPresses3 = 0;

// Beat tracking for MQTT reporting
bool beatResults[8] = {false};  // Track if each beat was completed correctly
bool beatResults2[8] = {false}; // Track for sequence 2
bool beatResults3[8] = {false}; // Track for sequence 3

// IR Sensor configuration
const int IR_RECEIVE_PIN = 12; // Using IRSENSOR pin
bool irReceiverActive = false;

// Stepper motor configuration - Use correct pins for DM542 driver
// Custom motor control variables (replacing AccelStepper)
bool stepperActive = false;
bool drawerOpen = true;

// Custom motor movement variables
bool isMoving = false;
bool moveDirection = true; // true = positive (open), false = negative (close)
unsigned long lastStepTime = 0;
unsigned long stepInterval = 1000; // microseconds between steps (1ms = 1000 steps/sec)
long stepsToGo = 0;
long currentPosition = 0;

// Lever state variables
bool leverActivated = false;
unsigned long leverStartTime = 0;
const unsigned long LEVER_TIMEOUT = 30000; // 30 seconds timeout
unsigned long lastPhotocellReport = 0;
const unsigned long PHOTOCELL_REPORT_INTERVAL = 1000; // Report every 1 second

// Drawer state variables
bool drawerMoving = false;
bool movingToOpen = false;  // Direction flag
bool movingToClose = false; // Direction flag

// Motor diagnostic variables
unsigned long motorStartTime = 0;
long lastMotorPosition = 0;
int motorStallCount = 0;
const unsigned long MOTOR_STALL_TIMEOUT = 5000; // 5 seconds
const long MIN_POSITION_CHANGE = 200;           // Minimum steps to consider movement

// Drawer monitoring variables
unsigned long lastDrawerReport = 0;
const unsigned long DRAWER_REPORT_INTERVAL = 1000; // Report every 1 second

// Solenoid control variables
unsigned long solenoidStartTime = 0;
bool solenoidActive = false;
const unsigned long SOLENOID_DURATION = 500; // 500ms activation time

// RGB LED Pin for Photocell
#define PHOTOCELL_LED 11

// FastLED configuration for photocell RGB LED
#define PHOTOCELL_LED_COUNT 1
CRGB photocellLED[PHOTOCELL_LED_COUNT];

// FastLED configuration for lever RGB LED (shines on photocell)
#define LEVERRGBLED_COUNT 1
CRGB leverLED[LEVERRGBLED_COUNT];

void setup()
{
  Serial.begin(115200);
  Serial4.begin(115200);

  pinMode(POWERLED, OUTPUT);
  digitalWrite(POWERLED, HIGH);

  // Initialize solenoid pin
  pinMode(CUCKCOOSOLENOID, OUTPUT);
  pinMode(DRAWERCOBLIGHTS, OUTPUT);
  pinMode(CRYSTALLIGHT, OUTPUT);
  pinMode(DRAWERMAGLOCK, OUTPUT);
  digitalWrite(CUCKCOOSOLENOID, LOW);
  digitalWrite(CRYSTALLIGHT, HIGH);   // Turn on crystal light
  digitalWrite(DRAWERCOBLIGHTS, LOW); // Turn off drawer cob lights
  digitalWrite(DRAWERMAGLOCK, HIGH);  // Turn off drawer mag lock

  // Initialize FastLED - Add each strip with its own data pin
  FastLED.addLeds<WS2812, LED1, GRB>(leds, 0 * LEDS_PER_STRIP, LEDS_PER_STRIP); // LED1 = pin 3, strip 0
  FastLED.addLeds<WS2812, LED2, GRB>(leds, 1 * LEDS_PER_STRIP, LEDS_PER_STRIP); // LED2 = pin 0, strip 1
  FastLED.addLeds<WS2812, LED3, GRB>(leds, 2 * LEDS_PER_STRIP, LEDS_PER_STRIP); // LED3 = pin 6, strip 2
  FastLED.addLeds<WS2812, LED4, GRB>(leds, 3 * LEDS_PER_STRIP, LEDS_PER_STRIP); // LED4 = pin 4, strip 3
  FastLED.addLeds<WS2812, LED5, GRB>(leds, 4 * LEDS_PER_STRIP, LEDS_PER_STRIP); // LED5 = pin 1, strip 4
  FastLED.addLeds<WS2812, LED6, GRB>(leds, 5 * LEDS_PER_STRIP, LEDS_PER_STRIP); // LED6 = pin 7, strip 5
  FastLED.addLeds<WS2812, LED7, GRB>(leds, 6 * LEDS_PER_STRIP, LEDS_PER_STRIP); // LED7 = pin 5, strip 6
  FastLED.addLeds<WS2812, LED8, GRB>(leds, 7 * LEDS_PER_STRIP, LEDS_PER_STRIP); // LED8 = pin 2, strip 7
  FastLED.addLeds<WS2812, LED9, GRB>(leds, 8 * LEDS_PER_STRIP, LEDS_PER_STRIP); // LED9 = pin 8, strip 8

  // Add photocell RGB LED
  FastLED.addLeds<WS2812, PHOTOCELL_LED, GRB>(photocellLED, PHOTOCELL_LED_COUNT);

  // Add lever RGB LED (shines on photocell)
  FastLED.addLeds<WS2812, LEVERRGBLED, GRB>(leverLED, LEVERRGBLED_COUNT);

  FastLED.setBrightness(200); // Set brightness (0-255)

  // Set all LEDs to black as default
  for (int i = 0; i < TOTAL_LEDS; i++)
  {
    leds[i] = CRGB::Black;
  }

  // Set photocell LED to black (off) as default
  photocellLED[0] = CRGB::Black;

  // Set lever LED to white (on) to shine on photocell
  leverLED[0] = CRGB::White;

  FastLED.show();

  // Initialize Floor Buttons
  for (size_t i = 0; i < sizeof(floorButtons) / sizeof(floorButtons[0]); i++)
  {
    pinMode(floorButtons[i], INPUT_PULLUP);
  }

  // Initialize proximity sensors
  pinMode(DRAWEROPENED_MAIN, INPUT_PULLUP);
  pinMode(DRAWEROPENED_SUB, INPUT_PULLUP);
  pinMode(DRAWERCLOSED_MAIN, INPUT_PULLUP);
  pinMode(DRAWERCLOSED_SUB, INPUT_PULLUP);

  // Build capability manifest
  Serial.println("[Floor] Building capability manifest...");
  build_capability_manifest();
  Serial.println("[Floor] Manifest built successfully");

  // Initialize Sentient MQTT
  Serial.println("[Floor] Initializing MQTT...");
  if (!sentient.begin())
  {
    Serial.println("[Floor] MQTT initialization failed - continuing without network");
  }
  else
  {
    Serial.println("[Floor] MQTT initialization successful");
  }

  // Initialize stepper motor pins for DM542 driver
  pinMode(MOTOR_PULPOS, OUTPUT); // Pulse pin
  pinMode(MOTOR_DIRPOS, OUTPUT); // Direction pin

  Serial.println("Motor driver pins initialized");

  // Initialize custom motor control - replacing AccelStepper
  currentPosition = 0;
  isMoving = false;
  stepsToGo = 0;

  Serial.println("DM542 driver initialized with custom motor control");
  Serial.print("Motor pins - PULSE: ");
  Serial.print(MOTOR_PULPOS);
  Serial.print(", DIRECTION: ");
  Serial.print(MOTOR_DIRPOS);

  // IR Sensor pin info
  Serial.print("IR sensor pin: ");
  Serial.print(IR_RECEIVE_PIN);
  Serial.println(" (Will auto-activate in state 5)");

  sentient.setCommandCallback([](const char *command, const JsonDocument &payload, void *context)
                              {
    if (strcmp(command, "state") == 0 && payload.is<const char*>()) {
      stateChange(payload.as<const char*>());
    }
    else if (strcmp(command, "drawer") == 0 && payload.is<const char*>()) {
      drawerControl(payload.as<const char*>());
    }
    else if (strcmp(command, "lever") == 0 && payload.is<const char*>()) {
      leverControl(payload.as<const char*>());
    }
    else if (strcmp(command, "solenoid") == 0 && payload.is<const char*>()) {
      solenoidControl(payload.as<const char*>());
    }
    else if (strcmp(command, "activateIR") == 0 && payload.is<const char*>()) {
      activateIR(payload.as<const char*>());
    } });

  Serial.println("Floor Puzzle setup complete.");
  delay(1000);
}

void loop()
{
  // Handle MQTT loop and registration
  static bool registered = false;
  sentient.loop();

  // Register after MQTT connection is established
  if (!registered && sentient.isConnected())
  {
    Serial.println("[Floor] Registering with Sentient system...");
    if (manifest.publish_registration(sentient.get_client(), ROOM_ID, CONTROLLER_ID))
    {
      Serial.println("[Floor] Registration successful!");
      registered = true;
    }
    else
    {
      Serial.println("[Floor] Registration failed - will retry on next loop");
    }
  }

  // Handle solenoid timing (non-blocking)
  if (solenoidActive && (millis() - solenoidStartTime >= SOLENOID_DURATION))
  {
    digitalWrite(CUCKCOOSOLENOID, LOW);
    solenoidActive = false;
    Serial.println("Solenoid deactivated");
  }

  switch (state)
  {
  case 0:
    // Wait for start command
    testLEDs();
    break;
  case 1:
    // Start the beat sequence
    sequenceOne();
    break;
  case 2:
    // Start the second sequence
    sequenceTwo();
    break;
  case 3:
    // Start the third sequence
    sequenceThree();
    break;
  case 4:
    // Test floor buttons
    testFloorButtons();
    break;
  case 5:
    // Lever control with IR sensor
    leverState();
    break;
  case 6:
    // Drawer control with stepper motor
    drawerState();
    break;

  default:
    break;
  }
}

// Simple motor control functions
// Custom motor control function
void customMotorRun()
{
  if (!isMoving)
    return;

  unsigned long currentTime = micros();
  if (currentTime - lastStepTime >= stepInterval)
  {
    // Set direction pin - REVERSED LOGIC
    digitalWrite(MOTOR_DIRPOS, moveDirection ? LOW : HIGH);

    // Generate pulse
    digitalWrite(MOTOR_PULPOS, HIGH);
    delayMicroseconds(10); // Short pulse width
    digitalWrite(MOTOR_PULPOS, LOW);

    // Update position
    if (moveDirection)
    {
      currentPosition++;
    }
    else
    {
      currentPosition--;
    }
    lastStepTime = currentTime;

    // Motor will continue until stopped by sensor check in drawerState()
  }
}

void drawerState()
{
  // Initialize stepper if not already active
  if (!stepperActive)
  {
    stepperActive = true;
    Serial.println("Motor control activated");
    sentient.publishText("Telemetry", "data", "Motor control activated");
  }

  // Run custom motor control
  customMotorRun();

  // Check proximity sensors to stop automatically
  if (isMoving && checkProximitySensors())
  {
    isMoving = false;
    Serial.println("Motor stopped by proximity sensor");
    sentient.publishText("Telemetry", "data", "Motor stopped by proximity sensor");
  }
}

bool checkProximitySensors()
{
  bool openedMain = (digitalRead(DRAWEROPENED_MAIN) == HIGH);
  bool openedSub = (digitalRead(DRAWEROPENED_SUB) == HIGH);
  bool closedMain = (digitalRead(DRAWERCLOSED_MAIN) == HIGH);
  bool closedSub = (digitalRead(DRAWERCLOSED_SUB) == HIGH);

  // Only stop if we hit the sensors in the direction we're moving
  if (moveDirection) // Moving to OPEN
  {
    if (openedMain || openedSub)
    {
      drawerOpen = true;
      Serial.println("Reached OPEN position");
      return true; // Stop motor
    }
  }
  else // Moving to CLOSE
  {
    if (closedMain || closedSub)
    {
      drawerOpen = false;
      Serial.println("Reached CLOSED position");
      return true; // Stop motor
    }
  }

  return false; // Keep moving
}

void moveToOpen()
{
  if (!stepperActive)
  {
    stepperActive = true;
    Serial.println("Motor control activated");
  }

  if (isMoving)
  {
    Serial.println("Motor already moving - ignoring command");
    sentient.publishText("Telemetry", "data", "Motor already moving");
    return;
  }

  Serial.println("Moving to OPEN position - will stop at sensor");
  Serial.print("Motor current position: ");
  Serial.println(currentPosition);

  // Set up custom motor movement - no step limit, move until sensor
  moveDirection = true; // positive direction for open
  isMoving = true;
  lastStepTime = micros();

  sentient.publishText("Telemetry", "data", "Moving to OPEN position");
  drawerMoving = true;

  Serial.println("Custom motor control started - OPEN direction");
}

void moveToClose()
{
  if (!stepperActive)
  {
    stepperActive = true;
    Serial.println("Motor control activated");
  }

  if (isMoving)
  {
    Serial.println("Motor already moving - ignoring command");
    sentient.publishText("Telemetry", "data", "Motor already moving");
    return;
  }

  Serial.println("Moving to CLOSE position - will stop at sensor");
  Serial.print("Motor current position: ");
  Serial.println(currentPosition);

  // Set up custom motor movement - no step limit, move until sensor
  moveDirection = false; // negative direction for close
  isMoving = true;
  lastStepTime = micros();

  sentient.publishText("Telemetry", "data", "Moving to CLOSE position");
  drawerMoving = true;

  Serial.println("Custom motor control started - CLOSE direction");
}

void stopMotor()
{
  isMoving = false;
  drawerMoving = false;
  Serial.println("Motor stopped manually");
  sentient.publishText("Telemetry", "data", "Motor stopped manually");
}

void testMotorPins()
{
  Serial.println("Testing motor pins manually...");

  // Test direction pin
  Serial.println("Setting direction HIGH");
  digitalWrite(MOTOR_DIRPOS, HIGH);
  delay(100);

  // Manual pulse generation
  for (int i = 0; i < 200; i++)
  {
    digitalWrite(MOTOR_PULPOS, HIGH);
    delayMicroseconds(1000); // 1ms high
    digitalWrite(MOTOR_PULPOS, LOW);
    delayMicroseconds(1000); // 1ms low
  }

  delay(1000);

  // Test opposite direction
  Serial.println("Setting direction LOW");
  digitalWrite(MOTOR_DIRPOS, LOW);
  delay(100);

  // Manual pulse generation
  for (int i = 0; i < 200; i++)
  {
    digitalWrite(MOTOR_PULPOS, HIGH);
    delayMicroseconds(1000); // 1ms high
    digitalWrite(MOTOR_PULPOS, LOW);
    delayMicroseconds(1000); // 1ms low
  }

  Serial.println("Pin test complete");
  sentient.publishText("Telemetry", "data", "Motor pin test completed");
}

void drawerControl(const char *value)
{
  // Always release drawer maglock at start of drawer control
  digitalWrite(DRAWERMAGLOCK, LOW); // Release (unlock) drawer maglock

  String cmd = String(value);
  cmd.toLowerCase();
  cmd.trim();

  Serial.print("Motor command received: '");
  Serial.print(cmd);
  Serial.println("'");

  // Switch to motor state if not already active
  if (state != 6)
  {
    state = 6;
    Serial.println("Switching to motor control state");
  }

  if (cmd == "open")
  {
    // Turn on COB lights for open command
    digitalWrite(DRAWERCOBLIGHTS, HIGH); // Turn on drawer COB lights (HIGH = on)
    Serial.println("Drawer maglock released and COB lights activated");
    moveToOpen();
  }
  else if (cmd == "close")
  {
    // Turn off COB lights for close command
    digitalWrite(DRAWERCOBLIGHTS, LOW); // Turn off drawer COB lights (LOW = off)
    Serial.println("Drawer maglock released and COB lights deactivated");
    moveToClose();
  }
  else if (cmd == "stop")
  {
    stopMotor();
  }
  else if (cmd == "test")
  {
    testMotorPins();
  }
  else
  {
    Serial.print("Unknown motor command: ");
    Serial.println(cmd);
    Serial.println("Available commands: open, close, stop, test");
    char buffer[100];
    sprintf(buffer, "Unknown motor command: %s. Available: open, close, stop, test", cmd.c_str());
    sentient.publishText("Telemetry", "data", buffer);
    return;
  }

  // Publish acknowledgement
  publish_command_acknowledgement(DEV_LEVER_MOTOR, "drawer");
}

void sequenceOne()
{
  unsigned long currentTime = millis();

  // Handle replay after wrong presses
  if (waitingToReplay)
  {
    if (currentTime - replayStartTime >= 2000) // 2 second delay
    {
      // Restart the sequence
      sequenceStarted = false;
      waitingToReplay = false;
      currentSequenceStep = 0;
      totalCorrectPresses = 0;
      totalExpectedPresses = 0;
      totalWrongPresses = 0;
      return;
    }
    else
    {
      return; // Still waiting
    }
  }

  // Send start message once when sequence begins
  if (!sequenceStarted)
  {
    Serial4.println("start");
    sequenceStarted = true;
    lastSequenceUpdate = currentTime;
    totalCorrectPresses = 0;
    totalExpectedPresses = 0;
    totalWrongPresses = 0;

    // Initialize beat results array
    for (int i = 0; i < 8; i++)
    {
      beatResults[i] = false;
    }

    return; // Exit early to allow for initial delay
  }

  // Check for button presses
  checkButtonPresses();

  // Add 500ms delay before first LED step
  unsigned long delayTime = (currentSequenceStep == 0) ? 0 : SEQUENCE_STEP_DELAY;

  // Check if it's time for the next step
  if (currentTime - lastSequenceUpdate >= delayTime)
  {
    // Calculate beat results for the PREVIOUS step before clearing arrays
    if (currentSequenceStep > 0)
    {
      int prevStep = currentSequenceStep - 1;
      int led1 = sequence1[prevStep][0];
      int led2 = sequence1[prevStep][1];

      int expectedForPrevBeat = 0;
      if (led1 != 0)
        expectedForPrevBeat++;
      if (led2 != 0)
        expectedForPrevBeat++;

      int correctForPrevBeat = 0;
      int wrongForPrevBeat = 0;

      for (int i = 0; i < 9; i++)
      {
        if (correctPress[i])
          correctForPrevBeat++;
        if (wrongPress[i])
          wrongForPrevBeat++;
      }

      beatResults[prevStep] = (correctForPrevBeat == expectedForPrevBeat && wrongForPrevBeat == 0);

      Serial.print("Beat ");
      Serial.print(prevStep + 1);
      Serial.print(" - Expected: ");
      Serial.print(expectedForPrevBeat);
      Serial.print(", Correct: ");
      Serial.print(correctForPrevBeat);
      Serial.print(", Wrong: ");
      Serial.print(wrongForPrevBeat);
      Serial.print(" -> Result: ");
      Serial.println(beatResults[prevStep] ? "1" : "0");
    }

    // Check if we've completed all 8 beats
    if (currentSequenceStep >= 8)
    {
      // Sequence complete, check results and send sound effect
      clearAllLEDs();
      FastLED.show();

      // Create MQTT message with beat results
      Serial.println("Debug - Beat Results Array:");
      for (int i = 0; i < 8; i++)
      {
        Serial.print("Beat ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.println(beatResults[i] ? "1" : "0");
      }

      char telemetryBuffer[100];
      sprintf(telemetryBuffer, "%d:%d:%d:%d:%d:%d:%d:%d",
              beatResults[0] ? 1 : 0,
              beatResults[1] ? 1 : 0,
              beatResults[2] ? 1 : 0,
              beatResults[3] ? 1 : 0,
              beatResults[4] ? 1 : 0,
              beatResults[5] ? 1 : 0,
              beatResults[6] ? 1 : 0,
              beatResults[7] ? 1 : 0);

      Serial.print("Seq1 Complete - Beat Results: ");
      Serial.println(telemetryBuffer);
      sentient.publishText("Telemetry", "data", telemetryBuffer);

      // Check if all beats were completed correctly
      bool allBeatsCorrect = true;
      for (int i = 0; i < 8; i++)
      {
        if (!beatResults[i])
        {
          allBeatsCorrect = false;
          break;
        }
      }

      if (allBeatsCorrect)
      {
        Serial4.println("correct");
        Serial.println("Playing CORRECT sound effect");
      }
      else
      {
        Serial4.println("wrong");
        Serial.println("Playing WRONG sound effect - some beats were incorrect");
        // Set up for replay after 2 seconds
        waitingToReplay = true;
        replayStartTime = currentTime;
      }

      currentSequenceStep = 0;
      sequenceStarted = false;
      totalCorrectPresses = 0;
      totalExpectedPresses = 0;
      totalWrongPresses = 0;
      state = 0; // Return to waiting state
      return;
    }

    // Clear all LEDs first
    clearAllLEDs();

    // Reset correct and wrong press tracking for new step
    for (int i = 0; i < 9; i++)
    {
      correctPress[i] = false;
      wrongPress[i] = false;
    }

    // Light up LEDs for current step
    int led1 = sequence1[currentSequenceStep][0];
    int led2 = sequence1[currentSequenceStep][1];

    // Count expected presses for this step
    if (led1 != 0)
      totalExpectedPresses++;
    if (led2 != 0)
      totalExpectedPresses++;

    // Light up the LEDs for this step
    if (led1 != 0)
      lightUpStrip(led1 - 1, CRGB::Yellow);
    if (led2 != 0)
      lightUpStrip(led2 - 1, CRGB::Yellow);

    FastLED.show();

    currentSequenceStep++;
    lastSequenceUpdate = currentTime;
  }
  else
  {
    // Update LEDs for correct presses and red for wrong presses during current step
    updateCorrectPresses();
  }
}

void sequenceTwo()
{
  unsigned long currentTime = millis();

  // Handle replay after wrong presses
  if (waitingToReplay2)
  {
    if (currentTime - replay2StartTime >= 2000) // 2 second delay
    {
      // Restart the sequence
      sequence2Started = false;
      waitingToReplay2 = false;
      currentSequence2Step = 0;
      totalCorrectPresses2 = 0;
      totalExpectedPresses2 = 0;
      totalWrongPresses2 = 0;
      return;
    }
    else
    {
      return; // Still waiting
    }
  }

  // Send start message once when sequence begins
  if (!sequence2Started)
  {
    Serial4.println("start");
    sequence2Started = true;
    lastSequence2Update = currentTime;
    totalCorrectPresses2 = 0;
    totalExpectedPresses2 = 0;
    totalWrongPresses2 = 0;

    // Initialize beat results array
    for (int i = 0; i < 8; i++)
    {
      beatResults2[i] = false;
    }

    return; // Exit early to allow for initial delay
  }

  // Check for button presses
  checkButtonPresses2();

  // No delay for first LED step
  unsigned long delayTime = (currentSequence2Step == 0) ? 0 : SEQUENCE_STEP_DELAY;

  // Check if it's time for the next step
  if (currentTime - lastSequence2Update >= delayTime)
  {
    // Calculate beat results for the PREVIOUS step before clearing arrays
    if (currentSequence2Step > 0)
    {
      int prevStep = currentSequence2Step - 1;
      int led1 = sequence2[prevStep][0];
      int led2 = sequence2[prevStep][1];

      int expectedForPrevBeat = 0;
      if (led1 != 0)
        expectedForPrevBeat++;
      if (led2 != 0)
        expectedForPrevBeat++;

      int correctForPrevBeat = 0;
      int wrongForPrevBeat = 0;

      for (int i = 0; i < 9; i++)
      {
        if (correctPress[i])
          correctForPrevBeat++;
        if (wrongPress[i])
          wrongForPrevBeat++;
      }

      beatResults2[prevStep] = (correctForPrevBeat == expectedForPrevBeat && wrongForPrevBeat == 0);

      Serial.print("Seq2 - Beat ");
      Serial.print(prevStep + 1);
      Serial.print(" - Expected: ");
      Serial.print(expectedForPrevBeat);
      Serial.print(", Correct: ");
      Serial.print(correctForPrevBeat);
      Serial.print(", Wrong: ");
      Serial.print(wrongForPrevBeat);
      Serial.print(" -> Result: ");
      Serial.println(beatResults2[prevStep] ? "1" : "0");
    }

    // Check if we've completed all 8 beats
    if (currentSequence2Step >= 8)
    {
      // Sequence complete, check results and send sound effect
      clearAllLEDs();
      FastLED.show();

      // Create MQTT message with beat results
      Serial.println("Debug - Beat Results Array:");
      for (int i = 0; i < 8; i++)
      {
        Serial.print("Beat ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.println(beatResults2[i] ? "1" : "0");
      }

      char telemetryBuffer[100];
      sprintf(telemetryBuffer, "%d:%d:%d:%d:%d:%d:%d:%d",
              beatResults2[0] ? 1 : 0,
              beatResults2[1] ? 1 : 0,
              beatResults2[2] ? 1 : 0,
              beatResults2[3] ? 1 : 0,
              beatResults2[4] ? 1 : 0,
              beatResults2[5] ? 1 : 0,
              beatResults2[6] ? 1 : 0,
              beatResults2[7] ? 1 : 0);

      Serial.print("Seq2 Complete - Beat Results: ");
      Serial.println(telemetryBuffer);
      sentient.publishText("Telemetry", "data", telemetryBuffer);

      // Check if all beats were completed correctly
      bool allBeatsCorrect = true;
      for (int i = 0; i < 8; i++)
      {
        if (!beatResults2[i])
        {
          allBeatsCorrect = false;
          break;
        }
      }

      if (allBeatsCorrect)
      {
        Serial4.println("correct");
        Serial.println("Playing CORRECT sound effect");
      }
      else
      {
        Serial4.println("wrong");
        Serial.println("Playing WRONG sound effect - some beats were incorrect");
        // Set up for replay after 2 seconds
        waitingToReplay2 = true;
        replay2StartTime = currentTime;
      }

      currentSequence2Step = 0;
      sequence2Started = false;
      totalCorrectPresses2 = 0;
      totalExpectedPresses2 = 0;
      totalWrongPresses2 = 0;
      state = 0; // Return to waiting state
      return;
    }
    else
    {
      // Clear all LEDs first
      clearAllLEDs();

      // Reset correct and wrong press tracking for new step
      for (int i = 0; i < 9; i++)
      {
        correctPress[i] = false;
        wrongPress[i] = false;
      }

      // Light up LEDs for current step
      int led1 = sequence2[currentSequence2Step][0];
      int led2 = sequence2[currentSequence2Step][1];

      // Count expected presses for this step
      if (led1 != 0)
        totalExpectedPresses2++;
      if (led2 != 0)
        totalExpectedPresses2++;

      // Light up the LEDs for this step
      if (led1 != 0)
        lightUpStrip(led1 - 1, CRGB::Blue);
      if (led2 != 0)
        lightUpStrip(led2 - 1, CRGB::Blue);

      FastLED.show();

      currentSequence2Step++;
      lastSequence2Update = currentTime;
    }
  }
  else
  {
    // Update LEDs for correct presses and red for wrong presses during current step
    updateCorrectPresses();
  }
}

void sequenceThree()
{
  unsigned long currentTime = millis();

  // Handle replay after wrong presses
  if (waitingToReplay3)
  {
    if (currentTime - replay3StartTime >= 2000) // 2 second delay
    {
      // Restart the sequence
      sequence3Started = false;
      waitingToReplay3 = false;
      currentSequence3Step = 0;
      totalCorrectPresses3 = 0;
      totalExpectedPresses3 = 0;
      totalWrongPresses3 = 0;
      return;
    }
    else
    {
      return; // Still waiting
    }
  }

  // Send start message once when sequence begins
  if (!sequence3Started)
  {
    Serial4.println("start");
    sequence3Started = true;
    lastSequence3Update = currentTime;
    totalCorrectPresses3 = 0;
    totalExpectedPresses3 = 0;
    totalWrongPresses3 = 0;

    // Initialize beat results array
    for (int i = 0; i < 8; i++)
    {
      beatResults3[i] = false;
    }

    return; // Exit early to allow for initial delay
  }

  // Check for button presses
  checkButtonPresses3();

  // No delay for first LED step
  unsigned long delayTime = (currentSequence3Step == 0) ? 0 : SEQUENCE_STEP_DELAY;

  // Check if it's time for the next step
  if (currentTime - lastSequence3Update >= delayTime)
  {
    // Calculate beat results for the PREVIOUS step before clearing arrays
    if (currentSequence3Step > 0)
    {
      int prevStep = currentSequence3Step - 1;
      int led1 = sequence3[prevStep][0];
      int led2 = sequence3[prevStep][1];
      int led3 = sequence3[prevStep][2];

      int expectedForPrevBeat = 0;
      if (led1 != 0)
        expectedForPrevBeat++;
      if (led2 != 0)
        expectedForPrevBeat++;
      if (led3 != 0)
        expectedForPrevBeat++;

      int correctForPrevBeat = 0;
      int wrongForPrevBeat = 0;

      for (int i = 0; i < 9; i++)
      {
        if (correctPress[i])
          correctForPrevBeat++;
        if (wrongPress[i])
          wrongForPrevBeat++;
      }

      beatResults3[prevStep] = (correctForPrevBeat == expectedForPrevBeat && wrongForPrevBeat == 0);

      Serial.print("Seq3 - Beat ");
      Serial.print(prevStep + 1);
      Serial.print(" - Expected: ");
      Serial.print(expectedForPrevBeat);
      Serial.print(", Correct: ");
      Serial.print(correctForPrevBeat);
      Serial.print(", Wrong: ");
      Serial.print(wrongForPrevBeat);
      Serial.print(" -> Result: ");
      Serial.println(beatResults3[prevStep] ? "1" : "0");
    }

    // Check if we've completed all 8 beats
    if (currentSequence3Step >= 8)
    {
      // Sequence complete, check results and send sound effect
      clearAllLEDs();
      FastLED.show();

      // Create MQTT message with beat results
      Serial.println("Debug - Beat Results Array:");
      for (int i = 0; i < 8; i++)
      {
        Serial.print("Beat ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.println(beatResults3[i] ? "1" : "0");
      }

      char telemetryBuffer[100];
      sprintf(telemetryBuffer, "%d:%d:%d:%d:%d:%d:%d:%d",
              beatResults3[0] ? 1 : 0,
              beatResults3[1] ? 1 : 0,
              beatResults3[2] ? 1 : 0,
              beatResults3[3] ? 1 : 0,
              beatResults3[4] ? 1 : 0,
              beatResults3[5] ? 1 : 0,
              beatResults3[6] ? 1 : 0,
              beatResults3[7] ? 1 : 0);

      Serial.print("Seq3 Complete - Beat Results: ");
      Serial.println(telemetryBuffer);
      sentient.publishText("Telemetry", "data", telemetryBuffer);

      // Check if all beats were completed correctly
      bool allBeatsCorrect = true;
      for (int i = 0; i < 8; i++)
      {
        if (!beatResults3[i])
        {
          allBeatsCorrect = false;
          break;
        }
      }

      if (allBeatsCorrect)
      {
        Serial4.println("correct");
        Serial.println("Playing CORRECT sound effect");
      }
      else
      {
        Serial4.println("wrong");
        Serial.println("Playing WRONG sound effect - some beats were incorrect");
        // Set up for replay after 2 seconds
        waitingToReplay3 = true;
        replay3StartTime = currentTime;
      }

      currentSequence3Step = 0;
      sequence3Started = false;
      totalCorrectPresses3 = 0;
      totalExpectedPresses3 = 0;
      totalWrongPresses3 = 0;
      state = 0; // Return to waiting state
      return;
    }
    else
    {
      // Clear all LEDs first
      clearAllLEDs();

      // Reset correct and wrong press tracking for new step
      for (int i = 0; i < 9; i++)
      {
        correctPress[i] = false;
        wrongPress[i] = false;
      }

      // Light up LEDs for current step
      int led1 = sequence3[currentSequence3Step][0];
      int led2 = sequence3[currentSequence3Step][1];
      int led3 = sequence3[currentSequence3Step][2];

      // Count expected presses for this step
      if (led1 != 0)
        totalExpectedPresses3++;
      if (led2 != 0)
        totalExpectedPresses3++;
      if (led3 != 0)
        totalExpectedPresses3++;

      // Light up the LEDs for this step
      if (led1 != 0)
        lightUpStrip(led1 - 1, CRGB::Purple);
      if (led2 != 0)
        lightUpStrip(led2 - 1, CRGB::Purple);
      if (led3 != 0)
        lightUpStrip(led3 - 1, CRGB::Purple);

      FastLED.show();

      currentSequence3Step++;
      lastSequence3Update = currentTime;
    }
  }
  else
  {
    // Update LEDs for correct presses and red for wrong presses during current step
    updateCorrectPresses();
  }
}

void clearAllLEDs()
{
  for (int i = 0; i < TOTAL_LEDS; i++)
  {
    leds[i] = CRGB::Black;
  }
}

void lightUpStrip(int stripIndex, CRGB color)
{
  if (stripIndex >= 0 && stripIndex < NUM_STRIPS)
  {
    int startLED = stripIndex * LEDS_PER_STRIP;
    for (int i = 0; i < LEDS_PER_STRIP; i++)
    {
      leds[startLED + i] = color;
    }
  }
}

void leverState()
{
  // Automatically initialize IR receiver when in state 5
  if (!irReceiverActive)
  {
    Serial.println("*** AUTO-ACTIVATING IR SENSOR FOR STATE 5 ***");
    IrReceiver.begin(IR_RECEIVE_PIN, false); // Explicitly disable LED feedback
    irReceiverActive = true;
    leverStartTime = millis();
    lastPhotocellReport = millis();

    // Turn on bright white LED for photocell using FastLED
    photocellLED[0] = CRGB::White;
    FastLED.show();

    Serial.println("State 5: IR sensor auto-activated, Photocell LED ON");
    sentient.publishText("Telemetry", "data", "State5:IR_Auto_Active,Photocell_ON");
  }

  // Removed timeout check - lever will stay active indefinitely

  // Periodic photocell status reporting
  unsigned long currentTime = millis();
  if (currentTime - lastPhotocellReport >= PHOTOCELL_REPORT_INTERVAL)
  {
    int photocellValue = analogRead(PHOTOCELL);

    Serial.print("=== LEVER STATE STATUS ===");
    Serial.print(" Photocell: ");
    Serial.print(photocellValue);
    Serial.print(" (");

    // Provide interpretation of photocell value
    if (photocellValue < 100)
    {
      Serial.print("VERY DARK");
    }
    else if (photocellValue < 300)
    {
      Serial.print("DARK");
    }
    else if (photocellValue < 600)
    {
      Serial.print("DIM");
    }
    else if (photocellValue < 800)
    {
      Serial.print("BRIGHT");
    }
    else
    {
      Serial.print("VERY BRIGHT");
    }

    Serial.println(")"); // Removed time remaining display

    // MQTT status update
    char telemetryBuffer[100];
    sprintf(telemetryBuffer, "Lever:Photocell=%d,IR_Active=%s",
            photocellValue,
            irReceiverActive ? "true" : "false");
    sentient.publishText("Telemetry", "data", telemetryBuffer);

    lastPhotocellReport = currentTime;
  }

  // Check for IR signal
  if (IrReceiver.decode())
  {
    Serial.println("IR signal detected - processing...");
    handleLeverIR();
    IrReceiver.resume();
  }
}

void handleLeverIR()
{
  // Always log what we received for debugging
  Serial.print("Raw IR Data - Protocol: ");
  Serial.print(IrReceiver.decodedIRData.protocol);
  Serial.print(", Command: 0x");
  Serial.print(IrReceiver.decodedIRData.command, HEX);
  Serial.print(", Raw: 0x");
  Serial.print(IrReceiver.decodedIRData.decodedRawData, HEX);

  // Filter out weak signals
  bool isStrongSignal = (IrReceiver.decodedIRData.protocol != UNKNOWN &&
                         IrReceiver.decodedIRData.protocol != 2 &&
                         IrReceiver.decodedIRData.decodedRawData != 0);

  Serial.print(" -> ");
  Serial.println(isStrongSignal ? "STRONG SIGNAL" : "WEAK/FILTERED");

  if (isStrongSignal)
  {
    Serial.print("VALID IR Signal - Protocol: ");
    Serial.print(getProtocolString(IrReceiver.decodedIRData.protocol));
    Serial.print(", Command: 0x");
    Serial.print(IrReceiver.decodedIRData.command, HEX);
    Serial.print(", Raw: 0x");
    Serial.println(IrReceiver.decodedIRData.decodedRawData, HEX);

    // Activate solenoid
    activateSolenoid();

    // Send MQTT message with IR data
    char telemetryBuffer[100];
    sprintf(telemetryBuffer, "IR_Received:0x%llX,Command:0x%X",
            IrReceiver.decodedIRData.decodedRawData,
            IrReceiver.decodedIRData.command);
    sentient.publishText("Telemetry", "data", telemetryBuffer);

    leverActivated = true;
  }
  else
  {
    Serial.println("IR signal filtered out (weak or invalid)");
  }
}

void activateSolenoid()
{
  Serial.println("Activating solenoid");
  digitalWrite(CUCKCOOSOLENOID, HIGH);
  solenoidStartTime = millis();
  solenoidActive = true;
  sentient.publishText("Telemetry", "data", "Solenoid activated");
}

// MQTT command handler for solenoid
void solenoidControl(const char *value)
{
  String cmd = String(value);
  cmd.toLowerCase();

  if (cmd == "activate" || cmd == "on" || cmd == "")
  {
    activateSolenoid();
  }
  else if (cmd == "off")
  {
    digitalWrite(CUCKCOOSOLENOID, LOW);
    solenoidActive = false;
    sentient.publishText("Telemetry", "data", "Solenoid deactivated");
  }
  else
  {
    return; // Unknown command, no ACK
  }

  // Publish acknowledgement
  publish_command_acknowledgement(DEV_CUCKOO_SOLENOID, "solenoid");
}

void deactivateLever()
{
  if (irReceiverActive)
  {
    IrReceiver.stop();
    irReceiverActive = false;
    leverActivated = false;

    // Turn off photocell LED when leaving lever state using FastLED
    photocellLED[0] = CRGB::Black;
    FastLED.show();
    Serial.println("Lever deactivated - Photocell LED OFF");
  }
}

void activateIR(const char *value)
{
  // NOTE: State 5 automatically activates IR - this is for manual override only
  if (strcmp(value, "1") == 0 || strcmp(value, "on") == 0)
  {
    if (state != 5)
    {
      state = 5; // Switch to lever state if not already there
      Serial.println("Manual IR activation: Switching to state 5");
    }

    if (!irReceiverActive)
    {
      IrReceiver.begin(IR_RECEIVE_PIN, false);
      irReceiverActive = true;
      photocellLED[0] = CRGB::White;
      FastLED.show();
      Serial.println("Manual IR override: Activated");
    }
    else
    {
      Serial.println("IR already active (state 5 auto-activation)");
    }
    sentient.publishText("Telemetry", "data", "IR_Manual_Override:ON");
  }
  else if (strcmp(value, "0") == 0 || strcmp(value, "off") == 0)
  {
    if (irReceiverActive)
    {
      IrReceiver.stop();
      irReceiverActive = false;
      photocellLED[0] = CRGB::Black;
      FastLED.show();
      Serial.println("Manual IR override: Deactivated");
    }
    sentient.publishText("Telemetry", "data", "IR_Manual_Override:OFF");
  }
  else
  {
    return; // Unknown command, no ACK
  }

  // Publish acknowledgement
  publish_command_acknowledgement(DEV_IR_SENSOR, "activateIR");
}

void deactivateDrawer()
{
  stepperActive = false;
  drawerMoving = false;
  isMoving = false;
  stepsToGo = 0;
  Serial.println("Motor control deactivated");
}

// Action functions for MQTT commands
void stateChange(const char *value)
{
  int newState = atoi(value);

  Serial.print("State change requested: ");
  Serial.print(state);
  Serial.print(" -> ");
  Serial.println(newState);

  // Clean up previous state before changing
  if (state == 5)
  {
    deactivateLever();
  }
  else if (state == 6)
  {
    deactivateDrawer();
  }

  // Turn off photocell LED when leaving state 5
  if (state == 5 && newState != 5)
  {
    photocellLED[0] = CRGB::Black;
    FastLED.show();
    Serial.println("Leaving lever state - Photocell LED OFF");
  }

  // Set the new state
  state = newState;

  // Auto-initialize state 5 (lever/IR state)
  if (newState == 5)
  {
    Serial.println("*** ENTERING STATE 5 - AUTO-INITIALIZING IR ***");
    // The leverState() function will handle IR initialization automatically
    // Just ensure photocell LED is ready
    photocellLED[0] = CRGB::White;
    FastLED.show();
    Serial.println("State 5 ready - IR will auto-activate in leverState()");
  }

  // Log state change completion
  Serial.print("State changed to: ");
  Serial.println(state);
  char telemetryBuffer[50];
  sprintf(telemetryBuffer, "State changed to: %d", state);
  sentient.publishText("Telemetry", "data", telemetryBuffer);

  // Publish acknowledgement
  publish_command_acknowledgement(DEV_FLOOR_LEDS, "state");
}

void leverControl(const char *value)
{
  String cmd = String(value);
  cmd.toLowerCase();

  if (cmd == "activate")
  {
    state = 5; // Switch to lever state
  }
  else if (cmd == "deactivate")
  {
    deactivateLever();
    state = 0;
  }
  else if (cmd == "photocell")
  {
    // Manual photocell reading
    int photocellValue = analogRead(PHOTOCELL);
    Serial.print("Photocell reading: ");
    Serial.print(photocellValue);
    Serial.print(" (Raw ADC value 0-1023, Pin ");
    Serial.print(PHOTOCELL);
    Serial.println(")");

    char telemetryBuffer[50];
    sprintf(telemetryBuffer, "Photocell reading: %d", photocellValue);
    sentient.publishText("Telemetry", "data", telemetryBuffer);
  }
  else
  {
    return; // Unknown command, no ACK
  }

  // Publish acknowledgement
  publish_command_acknowledgement(DEV_LEVER_LED, "lever");
}

// Add missing button press checking functions
void checkButtonPresses()
{
  for (int i = 0; i < 9; i++)
  {
    bool currentButtonState = (digitalRead(floorButtons[i]) == LOW);

    // Detect button press (transition from not pressed to pressed)
    if (currentButtonState && !lastButtonState[i])
    {
      // Check if this button corresponds to an active LED
      if (isLEDActive(i + 1))
      { // Convert to 1-based LED number
        correctPress[i] = true;
        totalCorrectPresses++;
      }
      else
      {
        // Wrong button pressed
        wrongPress[i] = true;
        totalWrongPresses++;
      }
    }

    lastButtonState[i] = currentButtonState;
  }
}

void checkButtonPresses2()
{
  for (int i = 0; i < 9; i++)
  {
    bool currentButtonState = (digitalRead(floorButtons[i]) == LOW);

    // Detect button press (transition from not pressed to pressed)
    if (currentButtonState && !lastButtonState[i])
    {
      // Check if this button corresponds to an active LED
      if (isLEDActive2(i + 1))
      { // Convert to 1-based LED number
        correctPress[i] = true;
        totalCorrectPresses2++;
      }
      else
      {
        // Wrong button pressed
        wrongPress[i] = true;
        totalWrongPresses2++;
      }
    }

    lastButtonState[i] = currentButtonState;
  }
}

void checkButtonPresses3()
{
  for (int i = 0; i < 9; i++)
  {
    bool currentButtonState = (digitalRead(floorButtons[i]) == LOW);

    // Detect button press (transition from not pressed to pressed)
    if (currentButtonState && !lastButtonState[i])
    {
      // Check if this button corresponds to an active LED
      if (isLEDActive3(i + 1))
      { // Convert to 1-based LED number
        correctPress[i] = true;
        totalCorrectPresses3++;
      }
      else
      {
        // Wrong button pressed
        wrongPress[i] = true;
        totalWrongPresses3++;
      }
    }

    lastButtonState[i] = currentButtonState;
  }
}

bool isLEDActive(int ledNumber)
{
  if (state == 1 && currentSequenceStep > 0)
  {
    // Check sequence1 - ledNumber is 1-based button number, sequence values are 1-based strip numbers
    int led1 = sequence1[currentSequenceStep - 1][0];
    int led2 = sequence1[currentSequenceStep - 1][1];

    return (ledNumber == led1 || ledNumber == led2);
  }
  return false;
}

bool isLEDActive2(int ledNumber)
{
  if (state == 2 && currentSequence2Step > 0)
  {
    // Check sequence2
    int led1 = sequence2[currentSequence2Step - 1][0];
    int led2 = sequence2[currentSequence2Step - 1][1];
    return (ledNumber == led1 || ledNumber == led2);
  }
  return false;
}

bool isLEDActive3(int ledNumber)
{
  if (state == 3 && currentSequence3Step > 0)
  {
    // Check sequence3
    int led1 = sequence3[currentSequence3Step - 1][0];
    int led2 = sequence3[currentSequence3Step - 1][1];
    int led3 = sequence3[currentSequence3Step - 1][2];

    return (ledNumber == led1 || ledNumber == led2 || ledNumber == led3);
  }
  return false;
}

void updateCorrectPresses()
{
  bool needsUpdate = false;

  for (int i = 0; i < 9; i++)
  {
    if (correctPress[i])
    {
      lightUpStrip(i, CRGB::Green);
      needsUpdate = true;
    }
    else if (wrongPress[i])
    {
      lightUpStrip(i, CRGB::Red);
      needsUpdate = true;
    }
  }

  if (needsUpdate)
  {
    FastLED.show();
  }
}

void testFloorButtons()
{
  unsigned long currentTime = millis();

  if (currentTime - lastButtonCheck >= BUTTON_CHECK_DELAY)
  {
    char buttonStatus[200];
    char tempBuffer[50];

    strcpy(buttonStatus, "Button Status: ");

    // Clear all LEDs first
    clearAllLEDs();

    for (int i = 0; i < 9; i++)
    {
      int buttonState = digitalRead(floorButtons[i]);
      sprintf(tempBuffer, "B%d:%s", i + 1, (buttonState == LOW ? "PRESSED" : "RELEASED"));
      strcat(buttonStatus, tempBuffer);
      if (i < 8)
        strcat(buttonStatus, ", ");

      // Light up corresponding LED if button is pressed
      if (buttonState == LOW)
      {
        lightUpStrip(i, CRGB::Yellow);
      }
    }

    FastLED.show();

    Serial.println(buttonStatus);
    sentient.publishText("Telemetry", "data", buttonStatus);

    lastButtonCheck = currentTime;
  }
}

void testLEDs()
{
  static bool lastTestButtonState[9] = {false}; // Track previous button states for test mode

  // Clear all LEDs first
  clearAllLEDs();

  for (int i = 0; i < 9; i++)
  {
    bool currentButtonState = (digitalRead(floorButtons[i]) == LOW);

    // Light up LED if button is pressed
    if (currentButtonState)
    {
      lightUpStrip(i, CRGB::Cyan);
    }

    // Detect button press (transition from not pressed to pressed) and print debug info
    if (currentButtonState && !lastTestButtonState[i])
    {
      Serial.print("Button ");
      Serial.print(i + 1);
      Serial.print(" pressed -> LED Strip ");
      Serial.println(i + 1);
      char telemetryBuffer[50];
      sprintf(telemetryBuffer, "Button %d pressed -> LED Strip %d", i + 1, i + 1);
      sentient.publishText("Telemetry", "data", telemetryBuffer);
    }

    lastTestButtonState[i] = currentButtonState;
  }

  FastLED.show();
}

// ══════════════════════════════════════════════════════════════════════════════
// COMMAND ACKNOWLEDGEMENT
// ══════════════════════════════════════════════════════════════════════════════

void publish_command_acknowledgement(const char *device_id, const char *command)
{
    if (!sentient.isConnected())
        return;

    StaticJsonDocument<160> ack;
    ack["controller_id"] = CONTROLLER_ID;
    ack["device_id"] = device_id;
    ack["command"] = command;
    ack["success"] = true;
    ack["timestamp_ms"] = millis();

    char buf[196];
    serializeJson(ack, buf, sizeof(buf));

    // Topic: <tenant>/<room>/acknowledgement/<controller>/<device>/<command>
    String ackTopic = String(CLIENT_ID) + "/" + String(ROOM_ID) + "/" +
                      String(CAT_ACKNOWLEDGEMENT) + "/" + String(CONTROLLER_ID) + "/" +
                      String(device_id) + "/" + String(command);

    sentient.get_client().publish(ackTopic.c_str(), buf, false);

    Serial.print(F("[ACK] -> "));
    Serial.println(ackTopic);
}
