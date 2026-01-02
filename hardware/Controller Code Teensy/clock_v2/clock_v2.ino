// Clock v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ═══════════════════════════════════════════════════════════════════════════════
// ⚠️  WARNING: This controller contains embedded game logic that violates
// ⚠️  STATELESS architecture. State machine (IDLE/PILASTER/LEVER/CRANK/OPERATOR/
// ⚠️  FINALE) and puzzle validation logic must be moved to Sentient backend.
// ⚠️  Current implementation is v2.3.0 library upgrade only - game logic
// ⚠️  extraction required.
// ═══════════════════════════════════════════════════════════════════════════════
// HARDWARE:
// - 3 DM542 stepper motors (Hour/Minute/Gears hands with differential signaling)
// - 8 resistor readers for operator puzzle (analog pins 16-23)
// - 3 rotary encoders for crank puzzle (Bottom/TopA/TopB)
// - 2 maglocks (operator/wood door), metal door proximity sensor
// - 2 linear actuators (metal door FWD/RWD, exit door FWD/RWD)
// - 4 FastLED WS2812B strips (100 LEDs each, left/right upper/lower)
// - Clock fog power, blacklight, laser light
// ═══════════════════════════════════════════════════════════════════════════════

#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <SentientCapabilityManifest.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include "controller_naming.h"
#include "FirmwareMetadata.h"

using namespace naming;

// Pin Definitions
#define POWER_LED 13

// Clock Stepper Motors (Pins 0-11) - DM542 Differential Signaling
#define CLOCK_HOUR_STEP_POS 0   // STEP+ for Hour Hand
#define CLOCK_HOUR_STEP_NEG 1   // STEP- for Hour Hand
#define CLOCK_HOUR_DIR_POS 2    // DIR+ for Hour Hand
#define CLOCK_HOUR_DIR_NEG 3    // DIR- for Hour Hand
#define CLOCK_GEARS_STEP_POS 4  // STEP+ for Gears
#define CLOCK_GEARS_STEP_NEG 5  // STEP- for Gears
#define CLOCK_GEARS_DIR_POS 6   // DIR+ for Gears
#define CLOCK_GEARS_DIR_NEG 7   // DIR- for Gears
#define CLOCK_MINUTE_STEP_POS 8 // STEP+ for Minute Hand
#define CLOCK_MINUTE_STEP_NEG 9 // STEP- for Minute Hand
#define CLOCK_MINUTE_DIR_POS 10 // DIR+ for Minute Hand
#define CLOCK_MINUTE_DIR_NEG 11 // DIR- for Minute Hand
#define STEPPER_ENABLE 12

// Operator Puzzle Analog Inputs for Resistor Reading (Pins 16-23)
#define RESISTOR_1 16
#define RESISTOR_2 17
#define RESISTOR_3 18
#define RESISTOR_4 19
#define RESISTOR_5 20
#define RESISTOR_6 21
#define RESISTOR_7 22
#define RESISTOR_8 23

// Crank Puzzle Rotary Encoders
#define ENCODER_BOTTOM_CLK 24
#define ENCODER_BOTTOM_DT 25
#define ENCODER_TOPA_CLK 38
#define ENCODER_TOPA_DT 39
#define ENCODER_TOPB_CLK 40
#define ENCODER_TOPB_DT 41

// Maglocks
#define OPERATOR_MAGLOCK 26
#define WOOD_DOOR_MAGLOCK 27
#define METAL_DOOR_PROX_SENSOR 28

// Actuators
#define EXIT_DOOR_FWD 31
#define METAL_DOOR_FWD 29
#define METAL_DOOR_RWD 30
#define EXIT_DOOR_RWD 32

// Other Outputs
#define CLOCKFOGPOWER 15
#define BLACKLIGHT 14
#define LASER_LIGHT 33

// LED Strips
#define LED_LEFT_UPPER 34
#define LED_LEFT_LOWER 35
#define LED_RIGHT_UPPER 36
#define LED_RIGHT_LOWER 37

// Sentient MQTT Configuration
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

// Forward declaration for acknowledgement publishing
void publish_command_acknowledgement(const char *device_id, const char *command);

void build_capability_manifest()
{
    manifest.set_controller_info(
        CONTROLLER_ID,
        CONTROLLER_FRIENDLY_NAME,
        firmware::VERSION,
        ROOM_ID,
        CONTROLLER_ID);
    // Clock controller is too complex for full device registry
    // Minimal manifest will still allow registration
}

// State Machine
enum PuzzleState
{
    IDLE,
    PILASTER,
    LEVER,
    CRANK,
    OPERATOR,
    FINALE
};

// FastLED Configuration
#define NUM_LEDS_PER_STRIP 100
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

CRGB ledsLeftUpper[NUM_LEDS_PER_STRIP];
CRGB ledsLeftLower[NUM_LEDS_PER_STRIP];
CRGB ledsRightUpper[NUM_LEDS_PER_STRIP];
CRGB ledsRightLower[NUM_LEDS_PER_STRIP];

// Global Variables
PuzzleState currentState = IDLE;
bool systemActive = false;
bool ledStripsActive = false;

// Stepper Control Variables
long minutePosition = 0;
long hourPosition = 0;
long gearPosition = 0;
long minuteTarget = 0;
long hourTarget = 0;
long gearTarget = 0;
unsigned long lastStepTime = 0;
const unsigned long minStepInterval = 500;           // Minimum interval (max speed): 500μs (2000 steps/sec)
const unsigned long maxStepInterval = 1000;          // Maximum interval (start speed): 1000μs (1000 steps/sec)
const unsigned long accelSteps = 500;                // Steps to accelerate/decelerate over
unsigned long currentStepInterval = maxStepInterval; // Current step timing (starts slow)
long minuteStepsRemaining = 0;
long hourStepsRemaining = 0;
long gearStepsRemaining = 0;
bool isAccelerating = false;
bool isDecelerating = false;

// Pilaster Stage Variables
float currentTime = 0.0;                 // Current time in hours (0.0 = midnight, 6.5 = 6:30)
const float targetTime = 6.5;            // Target time: 6:30
const int minuteStepsPerRotation = 4520; // Steps for 3200 microstepping
const int hourStepsPerRotation = 5240;   // Steps for 3200 microstepping
const int gearStepsPerRotation = 3200;   // Steps for 3200 microstepping
const int maxPresses = 5;                // Maximum number of button presses allowed
int currentPressCount = 0;               // Current number of button presses
char pilasterData[50] = "";
bool newPilasterData = false;

// Crank Stage Variables
volatile int encoderBottomCount = 0;
volatile int encoderTopCount = 0;
volatile int encoderTopBCount = 0;
int lastEncoderBottomCount = 0;
int lastEncoderTopCount = 0;
int lastEncoderTopBCount = 0;

// Telemetry change detection
char lastTelemetry[200] = "";
bool telemetryInitialized = false;

// ═══════════════════════════════════════════════════════════════
//                          SETUP & LOOP
// ═══════════════════════════════════════════════════════════════

void setup()
{
    Serial.begin(115200);

    Serial.print("USB Serial Number: ");
    Serial.println(teensyUsbSN());

    Serial.print("Device ID: ");
    Serial.println(CONTROLLER_ID);

    Serial.print("MAC Address: ");
    Serial.println(teensyMAC());

    Serial.print("Board Model: ");
    Serial.println(teensyBoardVersion());

    pinMode(POWER_LED, OUTPUT);
    digitalWrite(POWER_LED, HIGH);

    pinMode(CLOCKFOGPOWER, OUTPUT);
    digitalWrite(CLOCKFOGPOWER, HIGH); // Active LOW

    pinMode(BLACKLIGHT, OUTPUT);
    digitalWrite(BLACKLIGHT, LOW);

    pinMode(LASER_LIGHT, OUTPUT);
    // Laser lights controlled manually via MQTT only - no automatic state

    initializePins();
    initializeSteppers();
    initializeFastLED();
    initializeEncoders();
    initializeOutputs();

    // Build capability manifest
    Serial.println("[Clock] Building capability manifest...");
    build_capability_manifest();
    Serial.println("[Clock] Manifest built successfully");

    // Initialize Sentient MQTT
    Serial.println("[Clock] Initializing MQTT...");
    if (!sentient.begin())
    {
        Serial.println("[Clock] MQTT initialization failed - continuing without network");
    }
    else
    {
        Serial.println("[Clock] MQTT initialization successful");
    }

    // Register Action Handlers
    sentient.setCommandCallback([](const char *command, const JsonDocument &payload, void *context)
                                {
        if (payload.is<const char*>()) {
            const char* value = payload.as<const char*>();
            if (strcmp(command, "activate") == 0) {
                activateHandler(value);
            } else if (strcmp(command, "state") == 0) {
                stateHandler(value);
            } else if (strcmp(command, "pilaster") == 0) {
                pilasterHandler(value);
            } else if (strcmp(command, "fogMachine") == 0) {
                fogMachineHandler(value);
            } else if (strcmp(command, "blacklights") == 0) {
                blacklightHandler(value);
            } else if (strcmp(command, "OperatorMaglock") == 0) {
                operatorMaglockHandler(value);
            } else if (strcmp(command, "WoodDoorMaglock") == 0) {
                woodDoorMaglockHandler(value);
            } else if (strcmp(command, "ExitDoorFWD") == 0) {
                exitDoorFWDHandler(value);
            } else if (strcmp(command, "ExitDoorRWD") == 0) {
                exitDoorRWDHandler(value);
            } else if (strcmp(command, "MetalDoorFWD") == 0) {
                metalDoorFWDHandler(value);
            } else if (strcmp(command, "MetalDoorRWD") == 0) {
                metalDoorRWDHandler(value);
            } else if (strcmp(command, "laserLights") == 0) {
                laserLightHandler(value);
            }
        } });

    Serial.println("Clock Puzzle Controller Ready");
}

void loop()
{
    // Handle MQTT loop and registration
    static bool registered = false;
    sentient.loop();

    // Register after MQTT connection is established
    if (!registered && sentient.isConnected())
    {
        Serial.println("[Clock] Registering with Sentient system...");
        if (manifest.publish_registration(sentient.get_client(), ROOM_ID, CONTROLLER_ID))
        {
            Serial.println("[Clock] Registration successful!");
            registered = true;
        }
        else
        {
            Serial.println("[Clock] Registration failed - will retry on next loop");
        }
    }

    handleStateMachine();
    updateClockState();
    runSteppers();
}

// ═══════════════════════════════════════════════════════════════
//                      HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════

void activateHandler(const char *value)
{
    bool shouldActivate = (strcmp(value, "1") == 0 ||
                           strcmp(value, "true") == 0 ||
                           strcmp(value, "on") == 0);

    systemActive = shouldActivate;
    Serial.println("System " + String(shouldActivate ? "ACTIVATED" : "DEACTIVATED"));
}

void setStepperTarget(int motor, long target)
{
    switch (motor)
    {
    case 0: // Minute motor
        minuteTarget = target;
        minuteStepsRemaining = abs(target - minutePosition);
        break;
    case 1: // Hour motor
        hourTarget = target;
        hourStepsRemaining = abs(target - hourPosition);
        break;
    case 2: // Gear motor
        gearTarget = target;
        gearStepsRemaining = abs(target - gearPosition);
        break;
    }

    // Reset acceleration when new target is set
    currentStepInterval = maxStepInterval; // Start slow
    isAccelerating = false;
    isDecelerating = false;
}

// Get current position of a stepper motor
long getStepperPosition(int motor)
{
    switch (motor)
    {
    case 0:
        return minutePosition;
    case 1:
        return hourPosition;
    case 2:
        return gearPosition;
    default:
        return 0;
    }
}

// Check if stepper has reached target
bool stepperAtTarget(int motor)
{
    switch (motor)
    {
    case 0:
        return minutePosition == minuteTarget;
    case 1:
        return hourPosition == hourTarget;
    case 2:
        return gearPosition == gearTarget;
    default:
        return true;
    }
}

// Step a single motor in differential mode
void stepMotor(int motor, bool direction)
{
    int stepPos, stepNeg, dirPos, dirNeg;

    // Get pin assignments for the motor
    switch (motor)
    {
    case 0: // Minute motor
        stepPos = CLOCK_MINUTE_STEP_POS;
        stepNeg = CLOCK_MINUTE_STEP_NEG;
        dirPos = CLOCK_MINUTE_DIR_POS;
        dirNeg = CLOCK_MINUTE_DIR_NEG;
        break;
    case 1: // Hour motor
        stepPos = CLOCK_HOUR_STEP_POS;
        stepNeg = CLOCK_HOUR_STEP_NEG;
        dirPos = CLOCK_HOUR_DIR_POS;
        dirNeg = CLOCK_HOUR_DIR_NEG;
        break;
    case 2: // Gear motor
        stepPos = CLOCK_GEARS_STEP_POS;
        stepNeg = CLOCK_GEARS_STEP_NEG;
        dirPos = CLOCK_GEARS_DIR_POS;
        dirNeg = CLOCK_GEARS_DIR_NEG;
        break;
    default:
        return; // Invalid motor
    }

    // Debug: Print step pulse generation for first few steps
    static int stepCount[3] = {0, 0, 0};
    if (stepCount[motor] < 10) // Only print first 10 steps per motor to avoid spam
    {
        Serial.println("Motor " + String(motor) + " step " + String(stepCount[motor]) +
                       " - DIR:" + String(direction) + " on pins " + String(stepPos) + "/" + String(stepNeg));
    }
    stepCount[motor]++;

    // Set direction (invert direction for minute motor if it goes backwards)
    if (motor == 0) // Minute motor - invert direction
    {
        digitalWrite(dirPos, direction ? LOW : HIGH);
        digitalWrite(dirNeg, direction ? HIGH : LOW);
    }
    else // Hour and gear motors - normal direction
    {
        digitalWrite(dirPos, direction ? HIGH : LOW);
        digitalWrite(dirNeg, direction ? LOW : HIGH);
    }

    // Generate step pulse - DM542 differential signaling
    // For DM542: STEP+ goes HIGH, STEP- stays LOW for step pulse
    digitalWrite(stepPos, HIGH);
    digitalWrite(stepNeg, LOW);
    delayMicroseconds(10); // Pulse width for DM542
    digitalWrite(stepPos, LOW);
    digitalWrite(stepNeg, LOW);
    delayMicroseconds(10); // Settling time

    // Update position
    switch (motor)
    {
    case 0:
        minutePosition += direction ? 1 : -1;
        break;
    case 1:
        hourPosition += direction ? 1 : -1;
        break;
    case 2:
        gearPosition += direction ? 1 : -1;
        break;
    }
}

// Animate gear motor without position tracking (for visual effect)
void animateGearMotor(bool direction)
{
    // Set direction
    digitalWrite(CLOCK_GEARS_DIR_POS, direction ? HIGH : LOW);
    digitalWrite(CLOCK_GEARS_DIR_NEG, direction ? LOW : HIGH);

    // Generate step pulse - DM542 differential signaling
    digitalWrite(CLOCK_GEARS_STEP_POS, HIGH);
    digitalWrite(CLOCK_GEARS_STEP_NEG, LOW);
    delayMicroseconds(10); // Pulse width for DM542
    digitalWrite(CLOCK_GEARS_STEP_POS, LOW);
    digitalWrite(CLOCK_GEARS_STEP_NEG, LOW);
    delayMicroseconds(10); // Settling time

    // Note: Don't update gearPosition for animation - this is just visual effect
}

// Run all steppers toward their targets with acceleration
void runSteppers()
{
    unsigned long currentTime = micros();

    // Calculate dynamic step interval based on movement progress
    currentStepInterval = calculateStepInterval();

    // Check if it's time for the next step
    if (currentTime - lastStepTime >= currentStepInterval)
    {
        lastStepTime = currentTime;

        // Track if any hand is moving for gear animation
        bool handIsMoving = false;

        // Move minute motor toward target
        if (minutePosition != minuteTarget)
        {
            bool direction = minuteTarget > minutePosition;
            stepMotor(0, direction);
            minuteStepsRemaining = abs(minuteTarget - minutePosition);
            handIsMoving = true;
        }

        // Move hour motor toward target
        if (hourPosition != hourTarget)
        {
            bool direction = hourTarget > hourPosition;
            stepMotor(1, direction);
            hourStepsRemaining = abs(hourTarget - hourPosition);
            handIsMoving = true;
        }

        // Move gear motor toward target OR animate gears when hands are moving
        if (gearPosition != gearTarget)
        {
            bool direction = gearTarget > gearPosition;
            stepMotor(2, direction);
            gearStepsRemaining = abs(gearTarget - gearPosition);
        }
        else if (handIsMoving)
        {
            // Animate gears when hands are moving to look realistic
            // Use dedicated animation function that doesn't affect position tracking
            animateGearMotor(true); // Constant clockwise rotation for animation
        }
    }
}

// Calculate dynamic step interval with acceleration/deceleration
unsigned long calculateStepInterval()
{
    // Find the maximum steps remaining from any active motor
    long maxStepsRemaining = max(minuteStepsRemaining, max(hourStepsRemaining, gearStepsRemaining));

    if (maxStepsRemaining == 0)
    {
        return maxStepInterval; // No movement, return to slow speed
    }

    // Calculate total steps for the current movement
    long totalSteps = max(abs(minuteTarget - minutePosition),
                          max(abs(hourTarget - hourPosition),
                              abs(gearTarget - gearPosition)));

    if (totalSteps <= accelSteps * 2)
    {
        // Short movement - use linear ramp up/down
        float progress = (float)(totalSteps - maxStepsRemaining) / (float)totalSteps;
        if (progress < 0.5)
        {
            // Accelerating (first half)
            return maxStepInterval - (unsigned long)((maxStepInterval - minStepInterval) * progress * 2);
        }
        else
        {
            // Decelerating (second half)
            return minStepInterval + (unsigned long)((maxStepInterval - minStepInterval) * (progress - 0.5) * 2);
        }
    }
    else
    {
        // Long movement - accelerate for accelSteps, cruise, then decelerate
        long stepsTaken = totalSteps - maxStepsRemaining;

        if (stepsTaken < accelSteps)
        {
            // Accelerating phase
            float progress = (float)stepsTaken / (float)accelSteps;
            return maxStepInterval - (unsigned long)((maxStepInterval - minStepInterval) * progress);
        }
        else if (maxStepsRemaining < accelSteps)
        {
            // Decelerating phase
            float progress = (float)(accelSteps - maxStepsRemaining) / (float)accelSteps;
            return minStepInterval + (unsigned long)((maxStepInterval - minStepInterval) * progress);
        }
        else
        {
            // Cruising phase - full speed
            return minStepInterval;
        }
    }
}

// Check if all motors have reached their targets
bool allSteppersAtTarget()
{
    return stepperAtTarget(0) && stepperAtTarget(1) && stepperAtTarget(2);
}

// ═══════════════════════════════════════════════════════════════
//                    STATE MACHINE CORE
// ═══════════════════════════════════════════════════════════════
void handleStateMachine()
{
    switch (currentState)
    {
    case IDLE:
        handleIdleStage();
        break;
    case PILASTER:
        handlePilasterStage();
        break;
    case LEVER:
        handleLeverStage();
        break;
    case CRANK:
        handleCrankStage();
        break;
    case OPERATOR:
        handleOperatorStage();
        break;
    case FINALE:
        handleFinaleStage();
        break;
    default:
        Serial.println("Unknown state, resetting to IDLE");
        currentState = IDLE;
        break;
    }
}

// ═══════════════════════════════════════════════════════════════
//                      STAGE HANDLER FUNCTIONS
// ═══════════════════════════════════════════════════════════════

// ================= IDLE STAGE =================
void handleIdleStage()
{
    // Maintain LED strips on and Blue
    if (!ledStripsActive)
    {
        fill_solid(ledsLeftUpper, NUM_LEDS_PER_STRIP, CRGB::Blue);
        fill_solid(ledsLeftLower, NUM_LEDS_PER_STRIP, CRGB::Blue);
        fill_solid(ledsRightUpper, NUM_LEDS_PER_STRIP, CRGB::Blue);
        fill_solid(ledsRightLower, NUM_LEDS_PER_STRIP, CRGB::Blue);
        FastLED.show();
        ledStripsActive = true;
    }

    // Maglocks Active by default in IDLE (but don't override MQTT commands)
    // Note: Maglocks are controlled via MQTT commands, not forced here

    // Actuators in FWD position by default
    digitalWrite(EXIT_DOOR_FWD, HIGH);
    digitalWrite(METAL_DOOR_FWD, HIGH);
    digitalWrite(EXIT_DOOR_RWD, LOW);
    digitalWrite(METAL_DOOR_RWD, LOW);
}

// ================= PILASTER STAGE =================
void handlePilasterStage()
{

    // Pilaster button processing is now handled directly in pilasterHandler()
    // This function just maintains the stage state

    // Debug: Print current time status periodically
    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 5000) // Print every 5 seconds
    {
        Serial.println("=== PILASTER STAGE STATUS ===");
        Serial.println("Current Time: " + String(currentTime) + " hours");
        Serial.println("Target Time: " + String(targetTime) + " hours (6:30)");
        Serial.println("Button Presses: " + String(currentPressCount) + " of " + String(maxPresses));
        Serial.println("Presses Remaining: " + String(maxPresses - currentPressCount));
        Serial.println("=============================");
        lastStatusPrint = millis();
    }
}

// ================= LEVER STAGE =================
void handleLeverStage()
{
}

// ================= CRANK STAGE =================
void handleCrankStage()
{

    // Debug: Print encoder status periodically
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 1000) // Print every second
    {
        Serial.println("=== CRANK STAGE DEBUG ===");
        Serial.println("Encoder Bottom Count: " + String(encoderBottomCount));
        Serial.println("Encoder TopA Count: " + String(encoderTopCount));
        Serial.println("Encoder TopB Count: " + String(encoderTopBCount));
        Serial.println("Last Encoder Bottom: " + String(lastEncoderBottomCount));
        Serial.println("Last Encoder TopA: " + String(lastEncoderTopCount));
        Serial.println("Last Encoder TopB: " + String(lastEncoderTopBCount));
        Serial.println("Encoder Bottom Pin: " + String(digitalRead(ENCODER_BOTTOM_CLK)));
        Serial.println("Encoder TopA Pin: " + String(digitalRead(ENCODER_TOPA_CLK)));
        Serial.println("Encoder TopB Pin: " + String(digitalRead(ENCODER_TOPB_CLK)));
        Serial.println("========================");
        lastPrint = millis();
    }

    // Monitor rotary encoders and send MQTT when target reached
    if (encoderBottomCount != lastEncoderBottomCount ||
        encoderTopCount != lastEncoderTopCount ||
        encoderTopBCount != lastEncoderTopBCount)
    {
        Serial.println("ENCODER CHANGE - Bottom: " + String(encoderBottomCount) +
                       ", TopA: " + String(encoderTopCount) +
                       ", TopB: " + String(encoderTopBCount));
        lastEncoderBottomCount = encoderBottomCount;
        lastEncoderTopCount = encoderTopCount;
        lastEncoderTopBCount = encoderTopBCount;

        // Check if target positions reached with correct ratios
        // Documentation: "The encoders must turn with the correct ratios between the encoders"
        // Example ratio validation (adjust ratios based on actual gear system)
        // For demonstration, assuming Bottom:TopA:TopB should be in ratio 1:2:3
        bool ratioCorrect = false;
        if (encoderBottomCount > 0 && encoderTopCount > 0 && encoderTopBCount > 0)
        {
            float ratioTopA = (float)encoderTopCount / (float)encoderBottomCount;
            float ratioTopB = (float)encoderTopBCount / (float)encoderBottomCount;

            // Check if ratios are approximately 2:1 and 3:1 (adjust tolerance as needed)
            if (abs(ratioTopA - 2.0) < 0.2 && abs(ratioTopB - 3.0) < 0.2 &&
                encoderBottomCount >= 50) // Minimum rotation threshold
            {
                ratioCorrect = true;
            }
        }

        if (ratioCorrect)
        {
            Serial.println("SUCCESS! Correct gear ratios achieved. Publishing completion...");
            sentient.publishText("Events", "event", "crank_complete");
        }
    }
}

// ================= OPERATOR STAGE =================
void handleOperatorStage()
{

    // Read all 8 resistor values and send via MQTT
    // This is handled in updateMQTTData() which is called from updateClockState()
}

// ================= FINALE STAGE =================
void handleFinaleStage()
{
    // Laser lights controlled manually via MQTT only
    // Use 'laserLights on' command to turn on when needed

    // Handle lots of triggers via MQTT
    // This stage responds to external MQTT commands for various effects
}

// ═══════════════════════════════════════════════════════════════
//                        UTILITY FUNCTIONS
// ═══════════════════════════════════════════════════════════════

// ================= INITIALIZATION FUNCTIONS =================
void initializePins()
{
    // DM542 Differential Stepper pins
    pinMode(CLOCK_MINUTE_STEP_POS, OUTPUT);
    pinMode(CLOCK_MINUTE_STEP_NEG, OUTPUT);
    pinMode(CLOCK_MINUTE_DIR_POS, OUTPUT);
    pinMode(CLOCK_MINUTE_DIR_NEG, OUTPUT);
    pinMode(CLOCK_HOUR_STEP_POS, OUTPUT);
    pinMode(CLOCK_HOUR_STEP_NEG, OUTPUT);
    pinMode(CLOCK_HOUR_DIR_POS, OUTPUT);
    pinMode(CLOCK_HOUR_DIR_NEG, OUTPUT);
    pinMode(CLOCK_GEARS_STEP_POS, OUTPUT);
    pinMode(CLOCK_GEARS_STEP_NEG, OUTPUT);
    pinMode(CLOCK_GEARS_DIR_POS, OUTPUT);
    pinMode(CLOCK_GEARS_DIR_NEG, OUTPUT);
    pinMode(STEPPER_ENABLE, OUTPUT);

    // Maglock pins
    pinMode(OPERATOR_MAGLOCK, OUTPUT);
    pinMode(WOOD_DOOR_MAGLOCK, OUTPUT);

    // Proximity sensor input
    pinMode(METAL_DOOR_PROX_SENSOR, INPUT);

    // Actuator pins
    pinMode(EXIT_DOOR_FWD, OUTPUT);
    pinMode(METAL_DOOR_FWD, OUTPUT);
    pinMode(METAL_DOOR_RWD, OUTPUT);
    pinMode(EXIT_DOOR_RWD, OUTPUT);

    // Laser light
    pinMode(LASER_LIGHT, OUTPUT);
}

void initializeFastLED()
{
    // Initialize FastLED strips
    FastLED.addLeds<LED_TYPE, LED_LEFT_UPPER, COLOR_ORDER>(ledsLeftUpper, NUM_LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_LEFT_LOWER, COLOR_ORDER>(ledsLeftLower, NUM_LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_RIGHT_UPPER, COLOR_ORDER>(ledsRightUpper, NUM_LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_RIGHT_LOWER, COLOR_ORDER>(ledsRightLower, NUM_LEDS_PER_STRIP);

    FastLED.setBrightness(128); // Set brightness (0-255)
    FastLED.clear();            // Clear all LEDs initially

    // Set IDLE state: LED Strips filled with solid Blue
    fill_solid(ledsLeftUpper, NUM_LEDS_PER_STRIP, CRGB::Blue);
    fill_solid(ledsLeftLower, NUM_LEDS_PER_STRIP, CRGB::Blue);
    fill_solid(ledsRightUpper, NUM_LEDS_PER_STRIP, CRGB::Blue);
    fill_solid(ledsRightLower, NUM_LEDS_PER_STRIP, CRGB::Blue);
    FastLED.show(); // Display the colors
    ledStripsActive = true;
}

void initializeSteppers()
{
    // Initialize custom stepper control system
    Serial.println("Initializing custom stepper control system...");

    // Reset all positions and targets
    minutePosition = 0;
    hourPosition = 0;
    gearPosition = 0;
    minuteTarget = 0;
    hourTarget = 0;
    gearTarget = 0;

    lastStepTime = micros();

    Serial.println("Custom stepper system ready - 800 steps/sec, DM542 differential signaling");
}

void initializeEncoders()
{
    // Encoder pins
    pinMode(ENCODER_BOTTOM_CLK, INPUT_PULLUP);
    pinMode(ENCODER_BOTTOM_DT, INPUT_PULLUP);
    pinMode(ENCODER_TOPA_CLK, INPUT_PULLUP);
    pinMode(ENCODER_TOPA_DT, INPUT_PULLUP);
    pinMode(ENCODER_TOPB_CLK, INPUT_PULLUP);
    pinMode(ENCODER_TOPB_DT, INPUT_PULLUP);

    // Attach interrupts for encoders - using RISING like GearOne
    attachInterrupt(digitalPinToInterrupt(ENCODER_BOTTOM_CLK), encoderBottomCLKISR, RISING);
    attachInterrupt(digitalPinToInterrupt(ENCODER_BOTTOM_DT), encoderBottomDTISR, RISING);
    attachInterrupt(digitalPinToInterrupt(ENCODER_TOPA_CLK), encoderTopACLKISR, RISING);
    attachInterrupt(digitalPinToInterrupt(ENCODER_TOPA_DT), encoderTopADTISR, RISING);
    attachInterrupt(digitalPinToInterrupt(ENCODER_TOPB_CLK), encoderTopBCLKISR, RISING);
    attachInterrupt(digitalPinToInterrupt(ENCODER_TOPB_DT), encoderTopBDTISR, RISING);

    Serial.println("Encoder setup complete:");
    Serial.println("  Encoder Bottom CLK Pin: " + String(ENCODER_BOTTOM_CLK));
    Serial.println("  Encoder Bottom DT Pin: " + String(ENCODER_BOTTOM_DT));
    Serial.println("  Encoder TopA CLK Pin: " + String(ENCODER_TOPA_CLK));
    Serial.println("  Encoder TopA DT Pin: " + String(ENCODER_TOPA_DT));
    Serial.println("  Encoder TopB CLK Pin: " + String(ENCODER_TOPB_CLK));
    Serial.println("  Encoder TopB DT Pin: " + String(ENCODER_TOPB_DT));
    Serial.println("  Interrupts attached to pins " + String(ENCODER_BOTTOM_CLK) + ", " + String(ENCODER_TOPA_CLK) + ", and " + String(ENCODER_TOPB_CLK));
}

void initializeOutputs()
{
    // Initialize DM542 differential signals to proper idle state
    // For DM542 drivers: idle state should be both step signals LOW
    digitalWrite(CLOCK_MINUTE_STEP_POS, LOW);
    digitalWrite(CLOCK_MINUTE_STEP_NEG, LOW); // Changed from HIGH to LOW
    digitalWrite(CLOCK_MINUTE_DIR_POS, LOW);
    digitalWrite(CLOCK_MINUTE_DIR_NEG, HIGH);

    digitalWrite(CLOCK_HOUR_STEP_POS, LOW);
    digitalWrite(CLOCK_HOUR_STEP_NEG, LOW); // Changed from HIGH to LOW
    digitalWrite(CLOCK_HOUR_DIR_POS, LOW);
    digitalWrite(CLOCK_HOUR_DIR_NEG, HIGH);

    digitalWrite(CLOCK_GEARS_STEP_POS, LOW);
    digitalWrite(CLOCK_GEARS_STEP_NEG, LOW); // Changed from HIGH to LOW
    digitalWrite(CLOCK_GEARS_DIR_POS, LOW);
    digitalWrite(CLOCK_GEARS_DIR_NEG, HIGH);

    // Initialize all other outputs to OFF
    digitalWrite(STEPPER_ENABLE, HIGH); // HIGH = disabled for most stepper drivers

    digitalWrite(OPERATOR_MAGLOCK, HIGH);
    digitalWrite(WOOD_DOOR_MAGLOCK, HIGH);
    digitalWrite(EXIT_DOOR_FWD, HIGH);
    digitalWrite(METAL_DOOR_FWD, HIGH);
    digitalWrite(METAL_DOOR_RWD, LOW);
    digitalWrite(EXIT_DOOR_RWD, LOW);
}

// ═══════════════════════════════════════════════════════════════
//                    CALIBRATION FUNCTIONS
// ═══════════════════════════════════════════════════════════════
void calibrateMinuteMotor(const char *data)
{
    int steps = atoi(data);
    if (steps == 0)
        steps = 200; // Default full rotation guess

    Serial.println("=== MINUTE MOTOR CALIBRATION ===");
    Serial.println("Starting " + String(steps) + " step rotation test");
    Serial.println("Watch the minute hand and count full rotations");
    Serial.println("Position before: " + String(minutePosition));

    digitalWrite(STEPPER_ENABLE, LOW);

    long startPosition = minutePosition;
    setStepperTarget(0, minutePosition + steps);

    unsigned long startTime = millis();
    while (!stepperAtTarget(0) && (millis() - startTime < 30000)) // 30 second timeout
    {
        runSteppers();
    }

    long endPosition = minutePosition;
    digitalWrite(STEPPER_ENABLE, HIGH);

    Serial.println("Position after: " + String(endPosition));
    Serial.println("Steps moved: " + String(endPosition - startPosition));
    Serial.println("=== CALIBRATION COMPLETE ===");
}

void calibrateHourMotor(const char *data)
{
    int steps = atoi(data);
    if (steps == 0)
        steps = 200; // Default full rotation guess

    Serial.println("=== HOUR MOTOR CALIBRATION ===");
    Serial.println("Starting " + String(steps) + " step rotation test");
    Serial.println("Watch the hour hand and count full rotations");
    Serial.println("Position before: " + String(hourPosition));

    digitalWrite(STEPPER_ENABLE, LOW);

    long startPosition = hourPosition;
    setStepperTarget(1, hourPosition + steps);

    unsigned long startTime = millis();
    while (!stepperAtTarget(1) && (millis() - startTime < 30000)) // 30 second timeout
    {
        runSteppers();
    }

    long endPosition = hourPosition;
    digitalWrite(STEPPER_ENABLE, HIGH);

    Serial.println("Position after: " + String(endPosition));
    Serial.println("Steps moved: " + String(endPosition - startPosition));
    Serial.println("=== CALIBRATION COMPLETE ===");
}

void calibrateGearMotor(const char *data)
{
    int steps = atoi(data);
    if (steps == 0)
        steps = 200; // Default full rotation guess

    Serial.println("=== GEAR MOTOR CALIBRATION ===");
    Serial.println("Starting " + String(steps) + " step rotation test");
    Serial.println("Watch the gears and count full rotations");
    Serial.println("Position before: " + String(gearPosition));

    digitalWrite(STEPPER_ENABLE, LOW);

    long startPosition = gearPosition;
    setStepperTarget(2, gearPosition + steps);

    unsigned long startTime = millis();
    while (!stepperAtTarget(2) && (millis() - startTime < 30000)) // 30 second timeout
    {
        runSteppers();
    }

    long endPosition = gearPosition;
    digitalWrite(STEPPER_ENABLE, HIGH);

    Serial.println("Position after: " + String(endPosition));
    Serial.println("Steps moved: " + String(endPosition - startPosition));
    Serial.println("=== CALIBRATION COMPLETE ===");
}

// ================= RESISTOR READING FUNCTIONS =================
float readResistance(int pin)
{
    // Take multiple readings for averaging to improve stability
    const int numReadings = 5;
    int totalRaw = 0;

    for (int i = 0; i < numReadings; i++)
    {
        totalRaw += analogRead(pin);
        // No delay needed - ADC is fast enough
    }

    int rawValue = totalRaw / numReadings;

    // Convert to voltage (assuming 5V reference and 10-bit ADC)
    float voltage = (rawValue / 1023.0) * 5.0;

    // Calculate resistance using voltage divider formula
    // Assuming 100 ohm reference resistor in series (per documentation)
    float resistance;
    if (voltage > 4.99) // Avoid division by zero
    {
        resistance = 999999.0; // Very high resistance (open circuit)
    }
    else if (voltage < 0.01) // Very low voltage
    {
        resistance = 0.0; // Short circuit
    }
    else
    {
        resistance = (5.0 - voltage) / voltage * 100.0; // 100 ohm reference resistor
    }

    return resistance;
}

// ================= CLOCK MOVEMENT FUNCTIONS =================
void moveClockToTime(float timeInHours)
{
    Serial.println("Moving clock to time: " + String(timeInHours) + " hours");

    // Convert time to hour and minute components
    int hours = (int)timeInHours;
    float minutesFraction = timeInHours - hours;
    int minutes = (int)(minutesFraction * 60);

    // Convert to 12-hour format for display
    int displayHours = hours % 12;

    Serial.println("Display time: " + String(displayHours) + ":" + String(minutes));

    // Calculate stepper positions using individual motor step counts
    // Hour hand: 12 hours = hourStepsPerRotation steps
    // Minute hand: 60 minutes = minuteStepsPerRotation steps
    int hourSteps = (int)((timeInHours / 12.0) * hourStepsPerRotation);
    int minuteSteps = (int)((minutesFraction * 60.0 / 60.0) * minuteStepsPerRotation);

    // For the gears, let's make them follow the minute hand movement
    int gearSteps = (int)((minutesFraction * 60.0 / 60.0) * gearStepsPerRotation);

    Serial.println("Stepper positions - Hour: " + String(hourSteps) +
                   ", Minute: " + String(minuteSteps) +
                   ", Gear: " + String(gearSteps));

    moveClockHands(minuteSteps, hourSteps, gearSteps);
}

void moveClockHands(int minutePos, int hourPos, int gearPos)
{
    Serial.println("Moving clock hands with custom stepper control - Minute: " + String(minutePos) +
                   ", Hour: " + String(hourPos) + ", Gear: " + String(gearPos));

    // Test STEPPER_ENABLE pin before and after
    Serial.println("STEPPER_ENABLE pin state before: " + String(digitalRead(STEPPER_ENABLE)));

    // Enable stepper motors (LOW = enabled for most stepper drivers)
    digitalWrite(STEPPER_ENABLE, LOW);
    Serial.println("Stepper motors ENABLED - pin state: " + String(digitalRead(STEPPER_ENABLE)));

    // Set all target positions first
    minuteTarget = minutePos;
    hourTarget = hourPos;
    gearTarget = gearPos;

    // Calculate steps remaining for all motors
    minuteStepsRemaining = abs(minutePos - minutePosition);
    hourStepsRemaining = abs(hourPos - hourPosition);
    gearStepsRemaining = abs(gearPos - gearPosition);

    // Reset acceleration state once for the entire movement
    currentStepInterval = maxStepInterval; // Start slow
    isAccelerating = false;
    isDecelerating = false;

    Serial.println("Custom stepper targets set - waiting for movement completion...");
    Serial.println("Current positions - Minute: " + String(minutePosition) +
                   ", Hour: " + String(hourPosition) + ", Gear: " + String(gearPosition));
    Serial.println("Target positions - Minute: " + String(minuteTarget) +
                   ", Hour: " + String(hourTarget) + ", Gear: " + String(gearTarget));

    unsigned long moveStartTime = millis();
    unsigned long lastDebugTime = 0;

    // Wait for all steppers to reach their target positions
    while (!allSteppersAtTarget())
    {
        runSteppers(); // Explicitly call runSteppers() since this may be called from MQTT handlers

        // Print debug info every 500ms during movement
        if (millis() - lastDebugTime > 500)
        {
            Serial.println("Movement progress - Current: M:" + String(minutePosition) +
                           " H:" + String(hourPosition) + " G:" + String(gearPosition) +
                           " | Target: M:" + String(minuteTarget) +
                           " H:" + String(hourTarget) + " G:" + String(gearTarget));
            lastDebugTime = millis();
        }

        // Timeout after 30 seconds to prevent infinite loop
        if (millis() - moveStartTime > 30000)
        {
            Serial.println("WARNING: Movement timeout after 30 seconds!");
            Serial.println("Final positions - Minute: " + String(minutePosition) +
                           ", Hour: " + String(hourPosition) + ", Gear: " + String(gearPosition));
            break;
        }
    }

    Serial.println("All steppers reached target positions");

    // Disable stepper motors to reduce power consumption and heat (HIGH = disabled)
    digitalWrite(STEPPER_ENABLE, HIGH);
    Serial.println("Stepper motors DISABLED - pin state: " + String(digitalRead(STEPPER_ENABLE)));
}

// ================= STATE MONITORING FUNCTIONS =================
void updateClockState()
{
    // Update any periodic clock state monitoring
    // This can include encoder readings, sensor checks, etc.

    // Update MQTT data based on current state
    updateMQTTData();
}

void updateMQTTData()
{
    // Format telemetry based on current state
    char telemetry[200];

    switch (currentState)
    {
    case IDLE:
        sprintf(telemetry, "State:%01X,Status:IDLE", currentState);
        break;

    case PILASTER:
        sprintf(telemetry, "State:%01X,Time:%.2f,Target:%.2f,Presses:%d/%d",
                currentState, currentTime, targetTime, currentPressCount, maxPresses);
        break;

    case LEVER:
        sprintf(telemetry, "State:%01X,Status:LEVER",
                currentState);
        break;

    case CRANK:
        sprintf(telemetry, "State:%01X,Encoders:%d:%d:%d",
                currentState, encoderBottomCount, encoderTopCount, encoderTopBCount);
        break;

    case OPERATOR:
    {
        // Read all 8 resistor values for MQTT transmission (divided by 100 for easier reading)
        float r1 = readResistance(RESISTOR_1) / 100.0;
        float r2 = readResistance(RESISTOR_2) / 100.0;
        float r3 = readResistance(RESISTOR_3) / 100.0;
        float r4 = readResistance(RESISTOR_4) / 100.0;
        float r5 = readResistance(RESISTOR_5) / 100.0;
        float r6 = readResistance(RESISTOR_6) / 100.0;
        float r7 = readResistance(RESISTOR_7) / 100.0;
        float r8 = readResistance(RESISTOR_8) / 100.0;

        sprintf(telemetry, "State:%01X,R1:%.0f,R2:%.0f,R3:%.0f,R4:%.0f,R5:%.0f,R6:%.0f,R7:%.0f,R8:%.0f",
                currentState, r1, r2, r3, r4, r5, r6, r7, r8);
    }
    break;

    case FINALE:
        sprintf(telemetry, "State:%01X,Status:FINALE", currentState);
        break;

    default:
        sprintf(telemetry, "State:%01X,Status:UNKNOWN", currentState);
        break;
    }

    // CHANGE DETECTION: Only publish if data has changed and MQTT is connected
    bool dataChanged = (!telemetryInitialized) || (strcmp(telemetry, lastTelemetry) != 0);

    if (dataChanged && sentient.isConnected())
    {
        Serial.print("[PUBLISHING] ");
        Serial.println(telemetry);
        sentient.publishText("Telemetry", "data", telemetry);
        strcpy(lastTelemetry, telemetry);
        telemetryInitialized = true;
    }
}

// ═══════════════════════════════════════════════════════════════
//                    DEBUG TEST MQTT HANDLERS
// ═══════════════════════════════════════════════════════════════
void testMinuteMotor(const char *data)
{
    int steps = atoi(data);
    if (steps == 0)
        steps = 50; // Default to 50 steps

    Serial.println("Testing minute motor: " + String(steps) + " steps");
    digitalWrite(STEPPER_ENABLE, LOW);

    setStepperTarget(0, minutePosition + steps);

    unsigned long startTime = millis();
    while (!stepperAtTarget(0) && (millis() - startTime < 10000))
    {
        runSteppers();
    }

    digitalWrite(STEPPER_ENABLE, HIGH);
    Serial.println("Minute motor test complete. Final position: " + String(minutePosition));
}

void testHourMotor(const char *data)
{
    int steps = atoi(data);
    if (steps == 0)
        steps = 50; // Default to 50 steps

    Serial.println("Testing hour motor: " + String(steps) + " steps");
    digitalWrite(STEPPER_ENABLE, LOW);

    setStepperTarget(1, hourPosition + steps);

    unsigned long startTime = millis();
    while (!stepperAtTarget(1) && (millis() - startTime < 10000))
    {
        runSteppers();
    }

    digitalWrite(STEPPER_ENABLE, HIGH);
    Serial.println("Hour motor test complete. Final position: " + String(hourPosition));
}

void testGearMotor(const char *data)
{
    int steps = atoi(data);
    if (steps == 0)
        steps = 50; // Default to 50 steps

    Serial.println("Testing gear motor: " + String(steps) + " steps");
    digitalWrite(STEPPER_ENABLE, LOW);

    setStepperTarget(2, gearPosition + steps);

    unsigned long startTime = millis();
    while (!stepperAtTarget(2) && (millis() - startTime < 10000))
    {
        runSteppers();
    }

    digitalWrite(STEPPER_ENABLE, HIGH);
    Serial.println("Gear motor test complete. Final position: " + String(gearPosition));
}

void testStepperEnable(const char *data)
{
    Serial.println("Testing stepper enable pin (pin 12)");
    Serial.println("Current STEPPER_ENABLE state: " + String(digitalRead(STEPPER_ENABLE)));

    Serial.println("Setting STEPPER_ENABLE LOW (enabled)");
    digitalWrite(STEPPER_ENABLE, LOW);
    Serial.println("STEPPER_ENABLE state: " + String(digitalRead(STEPPER_ENABLE)));

    // Note: Test completed immediately - no blocking delays
    Serial.println("Setting STEPPER_ENABLE HIGH (disabled)");
    digitalWrite(STEPPER_ENABLE, HIGH);
    Serial.println("STEPPER_ENABLE state: " + String(digitalRead(STEPPER_ENABLE)));
}

void testPinMapping(const char *data)
{
    Serial.println("=== PIN MAPPING TEST ===");
    Serial.println("Motor Pin Assignments:");
    Serial.println("HOUR Motor (pins 0-3): STEP+=" + String(CLOCK_HOUR_STEP_POS) +
                   " STEP-=" + String(CLOCK_HOUR_STEP_NEG) +
                   " DIR+=" + String(CLOCK_HOUR_DIR_POS) +
                   " DIR-=" + String(CLOCK_HOUR_DIR_NEG));
    Serial.println("GEAR Motor (pins 4-7): STEP+=" + String(CLOCK_GEARS_STEP_POS) +
                   " STEP-=" + String(CLOCK_GEARS_STEP_NEG) +
                   " DIR+=" + String(CLOCK_GEARS_DIR_POS) +
                   " DIR-=" + String(CLOCK_GEARS_DIR_NEG));
    Serial.println("MINUTE Motor (pins 8-11): STEP+=" + String(CLOCK_MINUTE_STEP_POS) +
                   " STEP-=" + String(CLOCK_MINUTE_STEP_NEG) +
                   " DIR+=" + String(CLOCK_MINUTE_DIR_POS) +
                   " DIR-=" + String(CLOCK_MINUTE_DIR_NEG));
    Serial.println("ENABLE Pin: " + String(STEPPER_ENABLE));
    Serial.println("=== PIN MAPPING COMPLETE ===");
}

// ═══════════════════════════════════════════════════════════════
//                       MQTT ACTION HANDLERS
// ═══════════════════════════════════════════════════════════════
void pilasterHandler(const char *data)
{
    Serial.print("Pilaster button received: ");
    Serial.println(data);
    Serial.print("Current state: ");
    Serial.println(currentState);

    // Only process pilaster data when in PILASTER state
    if (currentState != PILASTER)
    {
        Serial.println("Ignoring pilaster data - not in PILASTER state");
        return;
    }

    // Process single button press directly
    char buttonPressed = data[0];
    bool timeChanged = false;

    // Process button presses with mathematical operations
    if (buttonPressed == '1') // Green button
    {
        Serial.println("Green button pressed: Add 7");
        currentTime += 7.0;
        timeChanged = true;
    }
    else if (buttonPressed == '2') // Silver button
    {
        Serial.println("Silver button pressed: Multiply by 4");
        currentTime *= 4.0;
        timeChanged = true;
    }
    else if (buttonPressed == '3') // Red button
    {
        Serial.println("Red button pressed: Subtract 3");
        currentTime -= 3.0;
        timeChanged = true;
    }
    else if (buttonPressed == '4') // Black button
    {
        Serial.println("Black button pressed: Divide by 2");
        currentTime /= 2.0;
        timeChanged = true;
    }
    else if (strcmp(data, "heartbeat") == 0)
    {
        Serial.println("Pilaster heartbeat received");
        return; // Don't process heartbeat as a button press
    }
    else
    {
        Serial.println("Unknown pilaster data: " + String(data));
        return;
    }

    if (timeChanged)
    {
        currentPressCount++; // Increment press counter
        Serial.println("Press " + String(currentPressCount) + " of " + String(maxPresses));
        Serial.println("Time calculation: " + String(currentTime) + " hours");

        // Check reset conditions: negative, over 24 hours, or max presses reached without success
        if (currentTime < 0.0 || currentTime >= 24.0)
        {
            Serial.println("Reset condition met! Time out of bounds. Returning to midnight (0:00)");
            currentTime = 0.0;
            currentPressCount = 0;
            moveClockToTime(currentTime);
        }
        else if (currentPressCount >= maxPresses)
        {
            // Check if target reached on the 5th press
            if (abs(currentTime - targetTime) < 0.1) // Close enough to 6.5 hours
            {
                Serial.println("SUCCESS! Target reached on press " + String(currentPressCount) + "! Clock shows 6:30");
                moveClockToTime(currentTime);
                sentient.publishText("Events", "event", "pilaster_complete");
            }
            else
            {
                Serial.println("FAILED! Maximum presses (" + String(maxPresses) + ") reached without hitting target.");
                Serial.println("Current time: " + String(currentTime) + " hours. Target: " + String(targetTime) + " hours");
                Serial.println("Resetting to midnight (0:00)");
                sentient.publishText("Events", "event", "pilaster_failed");
                currentTime = 0.0;
                currentPressCount = 0;
                moveClockToTime(currentTime);
            }
        }
        else if (abs(currentTime - targetTime) < 0.1) // Target reached before 5th press
        {
            Serial.println("SUCCESS! Target reached on press " + String(currentPressCount) + "! Clock shows 6:30");
            moveClockToTime(currentTime);
            sentient.publishText("Events", "event", "pilaster_complete");
        }
        else
        {
            // Continue playing - move clock hands to new time
            moveClockToTime(currentTime);
            Serial.println("Presses remaining: " + String(maxPresses - currentPressCount));
        }
    }
}
void stateHandler(const char *data)
{
    Serial.print("Clock action received: ");
    Serial.println(data);

    // Handle both numeric and string state values
    int numericState = atoi(data);

    // Check if it's a numeric value (0-5) or string value
    if (strcmp(data, "idle") == 0 || numericState == 0)
    {
        currentState = IDLE;
        Serial.println("State changed to IDLE");
    }
    else if (strcmp(data, "pilaster") == 0 || numericState == 1)
    {
        currentState = PILASTER;
        Serial.println("Subscribed to pilaster events via MQTT actions");

        // Reset clock to midnight when entering pilaster stage
        currentTime = 0.0;
        currentPressCount = 0;
        moveClockToTime(currentTime);
        Serial.println("State changed to PILASTER - Clock reset to midnight");
    }
    else if (strcmp(data, "lever") == 0 || numericState == 2)
    {
        currentState = LEVER;
        Serial.println("State changed to LEVER");
    }
    else if (strcmp(data, "crank") == 0 || numericState == 3)
    {
        currentState = CRANK;
        // Reset encoder counts when entering crank stage
        encoderBottomCount = 0;
        encoderTopCount = 0;
        encoderTopBCount = 0;
        Serial.println("State changed to CRANK - Encoders reset");
    }
    else if (strcmp(data, "operator") == 0 || numericState == 4)
    {
        currentState = OPERATOR;
        Serial.println("State changed to OPERATOR");
    }
    else if (strcmp(data, "finale") == 0 || numericState == 5)
    {
        currentState = FINALE;
        Serial.println("State changed to FINALE");
    }
    else
    {
        Serial.println("Unknown state command: " + String(data));
    }
}

// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
//                    DOOR & ACTUATOR MQTT HANDLERS
// ═══════════════════════════════════════════════════════════════

void operatorMaglockHandler(const char *data)
{
    bool shouldOpen = false;

    // Check for string commands first
    if (strcmp(data, "open") == 0)
    {
        shouldOpen = true;
    }
    else if (strcmp(data, "close") == 0)
    {
        shouldOpen = false;
    }
    else
    {
        // Fall back to numeric interpretation (1=open, 0=close)
        int state = atoi(data);
        shouldOpen = (state != 0);
    }

    digitalWrite(OPERATOR_MAGLOCK, shouldOpen ? LOW : HIGH); // open=LOW, close=HIGH - Active High is Locked
    Serial.println("Operator Maglock " + String(shouldOpen ? "OPEN" : "CLOSED"));
    publish_command_acknowledgement("operator_maglock", shouldOpen ? "open" : "close");
}

void woodDoorMaglockHandler(const char *data)
{
    bool shouldOpen = false;

    // Check for string commands first
    if (strcmp(data, "open") == 0)
    {
        shouldOpen = true;
    }
    else if (strcmp(data, "close") == 0)
    {
        shouldOpen = false;
    }
    else
    {
        // Fall back to numeric interpretation (1=open, 0=close)
        int state = atoi(data);
        shouldOpen = (state != 0);
    }

    digitalWrite(WOOD_DOOR_MAGLOCK, shouldOpen ? LOW : HIGH); // open=LOW, close=HIGH - Active High is Locked
    Serial.println("Wood Door Maglock " + String(shouldOpen ? "OPEN" : "CLOSED"));
    publish_command_acknowledgement("wood_door_maglock", shouldOpen ? "open" : "close");
}

void exitDoorFWDHandler(const char *data)
{
    int state = atoi(data);
    if (state)
    {
        digitalWrite(EXIT_DOOR_FWD, HIGH);
        digitalWrite(EXIT_DOOR_RWD, LOW);
        Serial.println("Exit Door LOCKED (FWD)");
    }
    else
    {
        digitalWrite(EXIT_DOOR_FWD, LOW);
        digitalWrite(EXIT_DOOR_RWD, HIGH);
        Serial.println("Exit Door UNLOCKED (RWD)");
    }
}

void exitDoorRWDHandler(const char *data)
{
    int state = atoi(data);
    if (state)
    {
        digitalWrite(EXIT_DOOR_RWD, HIGH);
        digitalWrite(EXIT_DOOR_FWD, LOW);
        Serial.println("Exit Door UNLOCKED (RWD)");
    }
    else
    {
        digitalWrite(EXIT_DOOR_RWD, LOW);
        digitalWrite(EXIT_DOOR_FWD, HIGH);
        Serial.println("Exit Door LOCKED (FWD)");
    }
}

void metalDoorFWDHandler(const char *data)
{
    int state = atoi(data);
    if (state)
    {
        digitalWrite(METAL_DOOR_FWD, HIGH);
        digitalWrite(METAL_DOOR_RWD, LOW);
        Serial.println("Metal Door LOCKED (FWD)");
    }
    else
    {
        digitalWrite(METAL_DOOR_FWD, LOW);
        digitalWrite(METAL_DOOR_RWD, HIGH);
        Serial.println("Metal Door UNLOCKED (RWD)");
    }
}

void metalDoorRWDHandler(const char *data)
{
    int state = atoi(data);
    if (state)
    {
        digitalWrite(METAL_DOOR_RWD, HIGH);
        digitalWrite(METAL_DOOR_FWD, LOW);
        Serial.println("Metal Door UNLOCKED (RWD)");
    }
    else
    {
        digitalWrite(METAL_DOOR_RWD, LOW);
        digitalWrite(METAL_DOOR_FWD, HIGH);
        Serial.println("Metal Door LOCKED (FWD)");
    }
}

// ═══════════════════════════════════════════════════════════════
//                    INTERRUPT SERVICE ROUTINES
// ═══════════════════════════════════════════════════════════════

// ================= BOTTOM ENCODER ISR FUNCTIONS =================
void encoderBottomCLKISR()
{
    if (digitalRead(ENCODER_BOTTOM_DT) == LOW)
    {
        encoderBottomCount++;
    }
    else
    {
        encoderBottomCount--;
    }
}

void encoderBottomDTISR()
{
    if (digitalRead(ENCODER_BOTTOM_CLK) == LOW)
    {
        encoderBottomCount--;
    }
    else
    {
        encoderBottomCount++;
    }
}

// ================= TOP-A ENCODER ISR FUNCTIONS =================
void encoderTopACLKISR()
{
    if (digitalRead(ENCODER_TOPA_DT) == LOW)
    {
        encoderTopCount++;
    }
    else
    {
        encoderTopCount--;
    }
}

void encoderTopADTISR()
{
    if (digitalRead(ENCODER_TOPA_CLK) == LOW)
    {
        encoderTopCount--;
    }
    else
    {
        encoderTopCount++;
    }
}

// ================= TOP-B ENCODER ISR FUNCTIONS =================
void encoderTopBCLKISR()
{
    if (digitalRead(ENCODER_TOPB_DT) == LOW)
    {
        encoderTopBCount++;
    }
    else
    {
        encoderTopBCount--;
    }
}

void encoderTopBDTISR()
{
    if (digitalRead(ENCODER_TOPB_CLK) == LOW)
    {
        encoderTopBCount--;
    }
    else
    {
        encoderTopBCount++;
    }
}

void fogMachineHandler(const char *data)
{
    bool shouldTurnOn = false;

    // Check for string commands first
    if (strcmp(data, "on") == 0)
    {
        shouldTurnOn = true;
    }
    else if (strcmp(data, "off") == 0)
    {
        shouldTurnOn = false;
    }
    else
    {
        // Fall back to numeric interpretation (1=on, 0=off)
        int state = atoi(data);
        shouldTurnOn = (state != 0);
    }

    digitalWrite(CLOCKFOGPOWER, shouldTurnOn ? LOW : HIGH); // Active LOW: LOW = ON, HIGH = OFF
    Serial.println("Fog Machine " + String(shouldTurnOn ? "ON" : "OFF"));
    publish_command_acknowledgement("fog_machine", shouldTurnOn ? "on" : "off");
}

void blacklightHandler(const char *data)
{
    bool shouldTurnOn = false;

    // Check for string commands first
    if (strcmp(data, "on") == 0)
    {
        shouldTurnOn = true;
    }
    else if (strcmp(data, "off") == 0)
    {
        shouldTurnOn = false;
    }
    else
    {
        // Fall back to numeric interpretation (1=on, 0=off)
        int state = atoi(data);
        shouldTurnOn = (state != 0);
    }

    digitalWrite(BLACKLIGHT, shouldTurnOn ? HIGH : LOW); // Active HIGH: HIGH = ON, LOW = OFF
    Serial.println("Blacklight " + String(shouldTurnOn ? "ON" : "OFF"));
    publish_command_acknowledgement("blacklights", shouldTurnOn ? "on" : "off");
}

void laserLightHandler(const char *data)
{
    bool shouldTurnOn = false;

    // Check for string commands first
    if (strcmp(data, "on") == 0)
    {
        shouldTurnOn = true;
    }
    else if (strcmp(data, "off") == 0)
    {
    }
    else
    {
        // Fall back to numeric interpretation (1=on, 0=off)
        int state = atoi(data);
        shouldTurnOn = (state != 0);
    }

    digitalWrite(LASER_LIGHT, shouldTurnOn ? HIGH : LOW); // Active HIGH: HIGH = ON, LOW = OFF
    Serial.println("Laser Lights " + String(shouldTurnOn ? "ON" : "OFF"));
    publish_command_acknowledgement("laser_lights", shouldTurnOn ? "on" : "off");
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

    Serial.print("[ACK] -> ");
    Serial.println(ackTopic);
}
