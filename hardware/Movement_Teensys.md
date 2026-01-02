# Teensy Controllers with Moving Parts

This document catalogs all Teensy 4.1 controllers in the Sentient Engine system that control mechanical movement devices (stepper motors, servos, linear actuators, etc.).

**Last Updated**: 2025-01-18

---

## Summary Statistics

- **Total Controllers with Moving Parts**: 17
- **Sensor-Required (CRITICAL)**: 6 controllers
- **Sensor-Enhanced**: 5 controllers
- **Sensor-Independent**: 6 controllers

### By Movement Type

- **Stepper Motor Controllers**: 11 (AccelStepper library)
- **Custom Stepper Control**: 2 (floor_v2, lever_boiler_v2)
- **Servo Motor Controllers**: 1 (PWMServo library)
- **Linear Actuator Controllers**: 2 (forward/reverse relay control)
- **Hybrid/Multi-Device Controllers**: 1 (boiler_room_subpanel_v2)

**Total Mechanical Devices**: 50+ individual motors/actuators across all controllers

---

## Sensor Requirement Classification

### **SENSOR-REQUIRED** (CRITICAL)

Movement cannot proceed safely without sensor feedback; motors run until sensors detect position. **Sensor failure causes motors to run indefinitely.**

### **SENSOR-ENHANCED**

Can move without sensors but feedback improves safety/accuracy. Sensors provide position tracking, homing, or validation.

### **SENSOR-INDEPENDENT**

Movement based on step counts, timers, or simple binary states. Relies on mechanical stops or internal limits.

---

## CRITICAL: Sensor-Required Controllers

⚠️ **These controllers MUST have functioning sensors or motors will run indefinitely, causing mechanical damage.**

---

### 1. **floor_v2** - Drawer Mechanism

- **Location**: `hardware/Controller Code Teensy/floor_v2/`
- **Movement Type**: 1x DM542 Stepper Driver (custom pulse/direction control)
- **Device**: Drawer mechanism (open/close)

#### Motor Control

- PULSE output: PIN 22
- DIRECTION output: PIN 21
- Step interval: 1000 microseconds (configurable)

#### **CRITICAL SENSORS** (4x Proximity)

- `DRAWEROPENED_MAIN` (Pin 35)
- `DRAWEROPENED_SUB` (Pin 36)
- `DRAWERCLOSED_MAIN` (Pin 33)
- `DRAWERCLOSED_SUB` (Pin 34)

#### Why Sensors Required

- Motor runs continuously until sensors trigger
- No hardcoded step limits
- Code pattern: `if (isMoving && checkProximitySensors())` stops motor

#### Safety Risk

**Motor would run indefinitely without sensor feedback**, potentially damaging drawer mechanism or motor.

#### Additional Features

- 9x 60-LED WS2812B strips (floor buttons)
- 9x floor button inputs
- 1x solenoid (cuckoo mechanism)
- 1x drawer maglock

---

### 2. **lab_rm_doors_hoist_v2** - Hoist & Sliding Doors

- **Location**: `hardware/Controller Code Teensy/lab_rm_doors_hoist_v2/`
- **Movement Type**: 4x Stepper Motors (AccelStepper::FULL4WIRE)

#### Motor Devices

- Hoist Stepper 1: Pins 0, 1, 2, 3
- Hoist Stepper 2: Pins 5, 6, 7, 8 (synchronized dual-motor lift)
- Door Left Stepper: Pins 24, 25, 26, 27
- Door Right Stepper: Pins 29, 30, 31, 32

#### Motor Specs

- Hoist max speed: 4000 steps/sec
- Door max speed: 6000 steps/sec

#### **CRITICAL SENSORS** (12x Position - Dual-Redundant)

**Hoist Sensors:**

- `HOISTSENSORUPONE` (Pin 15)
- `HOISTSENSORUPTWO` (Pin 17)
- `HOISTSENSORDNONE` (Pin 14)
- `HOISTSENSORDNTWO` (Pin 16)

**Left Door Sensors:**

- `OPEN_ONE` (Pin 40), `OPEN_TWO` (Pin 37)
- `CLOSE_ONE` (Pin 39), `CLOSE_TWO` (Pin 38)

**Right Door Sensors:**

- `OPEN_ONE` (Pin 36), `OPEN_TWO` (Pin 34)
- `CLOSE_ONE` (Pin 35), `CLOSE_TWO` (Pin 33)

#### Why Sensors Required

- Dual-redundant sensors prevent hoist overtravel and door collision
- Code pattern: `if (LeftLabDoorSensorOpen1 != 1 && LeftLabDoorSensorOpen2 != 1)` validates both sensors
- Both sensors must agree before stopping

#### Safety Risk

**Hoist cable damage, door jamming, motor stall, potential injury from moving parts.**

#### Additional Devices

- Rope Drop Solenoid: PIN 23 (timed activation)
- Hall effect endstops for precise positioning

---

### 3. **lab_rm_cage_a_v2** - Lab Cage Doors (2 doors, 3 motors)

- **Location**: `hardware/Controller Code Teensy/lab_rm_cage_a_v2/`
- **Movement Type**: 3x Stepper Motors (AccelStepper::FULL4WIRE)

#### Motor Devices

- Door 1 Stepper 1: Pins 24, 25, 26, 27
- Door 1 Stepper 2: Pins 28, 29, 30, 31 (dual-motor differential motion)
- Door 2 Stepper 1: Pins 4, 5, 6, 7

#### Motor Specs

- Max speed: 500 steps/sec
- Acceleration: 100 steps/sec²

#### **CRITICAL SENSORS** (8x Position - Dual-Redundant)

**Door 1 Sensors:**

- `D1SENSOROPEN_A` (Pin 10), `D1SENSOROPEN_B` (Pin 12)
- `D1SENSORCLOSED_A` (Pin 9), `D1SENSORCLOSED_B` (Pin 11)

**Door 2 Sensors:**

- `D2SENSOROPEN_A` (Pin 3), `D2SENSOROPEN_B` (Pin 2)
- `D2SENSORCLOSED_A` (Pin 1), `D2SENSORCLOSED_B` (Pin 0)

#### Why Sensors Required

- Motors only run when sensors indicate NOT at target position
- Code pattern: `if (D1Direction == 1 && D1OpenA != 0 && D1OpenB != 0)` requires both sensors clear
- Dual-redundant validation for safety

#### Safety Risk

**Door overtravel, mechanism binding, dual-motor synchronization failure.**

#### Additional Devices

- Canister charging solenoid: PIN 41
- Dual proximity sensors per door (open/close detection)

---

### 4. **lab_rm_cage_b_v2** - Lab Cage Doors (3 doors, 5 motors)

- **Location**: `hardware/Controller Code Teensy/lab_rm_cage_b_v2/`
- **Movement Type**: 5x Stepper Motors (AccelStepper::FULL4WIRE)

#### Motor Devices

- Door 3 Stepper 1: Pins 24, 25, 26, 27
- Door 3 Stepper 2: Pins 28, 29, 30, 31
- Door 4 Stepper 1: Pins 4, 5, 6, 7
- Door 5 Stepper 1: Pins 16, 17, 18, 19
- Door 5 Stepper 2: Pins 38, 39, 40, 41

#### Motor Specs

- Max speed: 250 steps/sec
- Acceleration: 200 steps/sec²
- Enable pins: 35 (D3), 36 (D4/D5)

#### **CRITICAL SENSORS** (12x Position - Dual-Redundant)

**Door 3 Sensors:** Open/Close sensors (4x)
**Door 4 Sensors:** Open/Close sensors (4x)
**Door 5 Sensors:** Open/Close sensors (4x)

#### Why Sensors Required

- Same dual-redundant safety model as Cage A
- Multiple doors require coordinated sensor validation
- Motors stop only when both sensors confirm position

#### Safety Risk

**Multiple door synchronization failures, mechanism damage, cage door collision.**

---

### 5. **study_d_v2** - Wall Panel Motors

- **Location**: `hardware/Controller Code Teensy/study_d_v2/`
- **Movement Type**: 2x Stepper Motors (DM542 drivers - differential signaling)

#### Motor Devices (Differential Pin Pairs)

- Left wall motor: STEP+ 15, STEP- 16, DIR+ 17, DIR- 18
- Right wall motor: STEP+ 20, STEP- 21, DIR+ 22, DIR- 23

#### Motor Control

- Shared Power: PIN 1
- Shared ENABLE: PIN 14
- Uses differential signaling for noise immunity

#### **CRITICAL SENSORS** (8x Proximity)

**Left Motor Sensors:**

- Top sensors (2x)
- Bottom sensors (2x)

**Right Motor Sensors:**

- Top sensors (2x)
- Bottom sensors (2x)

#### Why Sensors Required

- Large panels require precise limit detection to prevent collision
- 4 sensors per motor provide redundant safety coverage
- Prevents panel overtravel and wall collision

#### Safety Risk

**Panel collision with walls, motor overload, large moving panel hazard.**

#### Additional Features

- DMX fog machine control (Serial7)

---

### 6. **lever_boiler_v2** - Newell Post Lever

- **Location**: `hardware/Controller Code Teensy/Sentient_Connected/lever_boiler_v2/`
- **Movement Type**: 1x Stepper Motor (4-phase FULL4WIRE - custom control)
- **Device**: Newell post lever mechanism (up/down/stop)

#### Motor Control

- 4-phase pins: 1, 2, 3, 4
- Step sequence timing: 1000 microseconds between steps
- Uses custom 4-phase stepping logic (not AccelStepper)

#### **CRITICAL SENSORS** (2x Proximity)

- Lever up position: Pin 39
- Lever down position: Pin 38

#### Why Sensors Required

- Custom 4-phase stepper needs position feedback for safe travel limits
- Prevents lever overtravel beyond mechanical limits
- No step counting; relies entirely on sensor feedback

#### Safety Risk

**Lever overtravel, mechanical binding, stepper stall.**

#### Associated Devices

- 2x Maglocks: lever_boiler (33), lever_stairs (34)
- 2x Lever LEDs: boiler (20), stairs (19)
- 2x Photocell sensors: boiler (A1), stairs (A0)
- 2x IR receivers (alternating): pins 16 (boiler), 17 (stairs)
- Newell post light: PIN 36

---

## Sensor-Enhanced Controllers

These controllers **can operate without sensors** but use feedback for improved safety, accuracy, or validation.

---

### 7. **gauge_1_3_4_v2** - Pressure Gauges (3 motors)

- **Location**: `hardware/Controller Code Teensy/Sentient_Connected/gauge_1_3_4_v2/`
- **Movement Type**: 3x Stepper Motors (AccelStepper::DRIVER)

#### Motor Devices

- Gauge 1: DIR pin 7, STEP pin 6, ENABLE pin 8
- Gauge 3: DIR pin 11, STEP pin 10, ENABLE pin 12
- Gauge 4: DIR pin 3, STEP pin 2, ENABLE pin 4

#### Motor Specs

- Max speed: 700 steps/sec
- Range: 2300 steps per gauge
- Acceleration: 200 steps/sec²

#### Feedback Sensors (3x Potentiometers)

- Gauge 1 POT: Pin A10
- Gauge 3 POT: Pin A12
- Gauge 4 POT: Pin A11

#### Why Sensors Enhanced

- Gauges autonomously track valve positions via potentiometers
- Can also accept direct step position commands without feedback
- Real-time synchronization with valve positions for accurate PSI display

#### Fallback Mode

Can accept direct `set_position` commands via MQTT (step-count based)

#### Purpose

Autonomously track ball valve potentiometer positions and display PSI readings on mechanical gauge faces

---

### 8. **gauge_2_5_7_v2** - Pressure Gauges (3 motors)

- **Location**: `hardware/Controller Code Teensy/Sentient_Connected/gauge_2_5_7_v2/`
- **Movement Type**: 3x Stepper Motors (AccelStepper::DRIVER)

#### Motor Devices

- Gauge 2: DIR pin 3, STEP pin 2, ENABLE pin 4
- Gauge 5: DIR pin 11, STEP pin 10, ENABLE pin 12
- Gauge 7: DIR pin 7, STEP pin 6, ENABLE pin 8

#### Motor Specs

- Max speed: 700 steps/sec
- Range: 2300 steps per gauge
- Acceleration: 200 steps/sec²

#### Feedback Sensors (3x Potentiometers)

- Gauge 2 POT: Pin A11
- Gauge 5 POT: Pin A12
- Gauge 7 POT: Pin A10

#### Why Sensors Enhanced

Same as gauge_1_3_4_v2 - can operate on step counts or potentiometer feedback

#### Purpose

Track valve positions for pressure gauges in different room areas

---

### 9. **gauge_6_leds_v2** - Pressure Gauge

- **Location**: `hardware/Controller Code Teensy/Sentient_Connected/gauge_6_leds_v2/`
- **Movement Type**: 1x Stepper Motor (AccelStepper::DRIVER)

#### Motor Device

- Gauge 6: STEP pin 39, DIR pin 40, ENABLE pin 41

#### Motor Specs

- Max speed: 800 steps/sec
- Range: 700 steps
- Acceleration: 300 steps/sec²

#### Feedback Sensor (1x Potentiometer)

- Gauge 6 POT: Pin A4

#### Why Sensors Enhanced

- Tracks valve position for synchronized gauge display
- Can accept direct step position commands as fallback

#### Additional Features

- 7x WS2812B indicator LEDs (pins 25-31) with flicker animation
- 219x WS2811 ceiling LEDs in 9 clock-face sections

#### Purpose

Pressure gauge with integrated LED indicators and ceiling lighting control

---

### 10. **riddle_v2** - Puzzle Mechanism

- **Location**: `hardware/Controller Code Teensy/Sentient_Connected/riddle_v2/`
- **Movement Type**: 2x Stepper Motors (AccelStepper::FULL4WIRE)

#### Motor Devices

- Stepper 1: Pins 24, 25, 26, 27
- Stepper 2: Pins 9, 10, 11, 12

#### Feedback Sensors

- Multiple proximity endstop sensors

#### Why Sensors Enhanced

- Endstops provide homing/calibration reference points
- Can run step-based sequences without homing
- Sensors improve positioning accuracy

#### Additional Devices

- Maglock: PIN 42
- 50x LED strip (WS2812B)

#### Purpose

Complex multi-step puzzle with motorized sequences and LED feedback

---

### 11. **lever_fan_safe_v2** - Fan & Safe Door

- **Location**: `hardware/Controller Code Teensy/lever_fan_safe_v2/`
- **Movement Type**: 1x Stepper Motor (AccelStepper::FULL4WIRE)

#### Motor Device

- Fan actuator: Pins 33 (P1), 34 (P2), 35 (P3), 36 (P4)

#### Motor Specs

- Max speed: 3000 steps/sec
- Active speed: 1500 steps/sec (when fan ON)

#### Feedback Sensors (2x Photocells)

- Safe lever: A0
- Fan lever: A1

#### Why Sensors Enhanced

- Photocells track lever positions for puzzle logic
- Fan can run without lever feedback (continuous rotation)
- Sensors provide position tracking for game state

#### Associated Devices

- Solenoid (safe door): PIN 40
- Maglock (fan maglock): PIN 41
- Fan Motor Enable: PIN 37
- IR receivers: PIN 16 (safe), PIN 17 (fan)

#### Purpose

Mechanical fan with solenoid-locked safe door puzzle

---

## Sensor-Independent Controllers

These controllers operate purely on **step counting**, **timers**, or **binary states** without requiring position feedback.

---

### 12. **clock_v2** - Clock Hands & Gears

- **Location**: `hardware/Controller Code Teensy/clock_v2/`
- **Movement Type**: 3x Stepper Motors (DM542 drivers - differential signaling)

#### Motor Devices (Differential Pin Pairs)

- Hour hand: STEP+ 0, STEP- 1, DIR+ 2, DIR- 3
- Gear mechanism: STEP+ 4, STEP- 5, DIR+ 6, DIR- 7
- Minute hand: STEP+ 8, STEP- 9, DIR+ 10, DIR- 11

#### Motor Control

- Shared ENABLE: PIN 12
- Uses differential signaling for noise immunity

#### Movement Control

- Absolute step counting
- Target positioning: `setStepperTarget()` receives step count directly
- Code pattern: `if (minutePosition != minuteTarget)` moves based on step count only

#### Safety Model

Assumes mechanical stops or gear limits prevent overtravel

#### No Position Sensors

Resistor inputs and encoders are for puzzle logic, not motor feedback

#### Purpose

Animated clock puzzle with synchronized multi-motor time display

---

### 13. **study_b_v2** - Fan & Wall Gears (4 motors)

- **Location**: `hardware/Controller Code Teensy/study_b_v2/`
- **Movement Type**: 4x Stepper Motors (DM542 drivers - differential signaling)

#### Motor Devices (Differential Pin Pairs)

- Study fan: PULSE+ 0, PULSE- 1, DIR+ 2, DIR- 3, ENABLE 7
- Wall gear 1: PULSE+ 38, PULSE- 39, DIR+ 40, DIR- 41
- Wall gear 2: PULSE+ 20, PULSE- 21, DIR+ 22, DIR- 23
- Wall gear 3: PULSE+ 16, PULSE- 17, DIR+ 18, DIR- 19

#### Motor Control

- Shared gear ENABLE: PIN 15
- Shared gear POWER: PIN 24

#### Movement Control

- DM542 differential drivers with step counting
- No position feedback
- Open-loop motor control

#### Safety Model

Mechanical limits or continuous rotation (fan)

#### Additional Devices

- 2x TV power relays: PIN 9, 10
- Fog machine: PIN 4, 5
- MAKSERVO power: PIN 8

#### Purpose

Room-wide mechanical animation with synchronized motors and environmental effects

---

### 14. **maks_servo_v2** - Servo Mechanism

- **Location**: `hardware/Controller Code Teensy/maks_servo_v2/`
- **Movement Type**: 1x PWMServo (analog servo motor)
- **Device**: Maks mechanism (opening/closing)

#### Servo Control

- PWMServo on PIN 1
- Position range: 0-180 degrees
- Closed position: 0 degrees
- Open position: 60 degrees

#### Commands

- `open` - Move to 60 degrees
- `close` - Move to 0 degrees
- `set_position` - Precise positioning (0-180)

#### Movement Control

- PWM servo position (0-180 degrees)
- No external feedback
- Standard hobby servos are position-commanded but don't report actual position

#### Safety Model

Servo internal potentiometer provides closed-loop control within servo

#### Purpose

Mechanical puppet/animatronic control (face, mouth, or door mechanism)

---

### 15. **fuse_v2** - Linear Actuator (Gate/Door)

- **Location**: `hardware/Controller Code Teensy/fuse_v2/`
- **Movement Type**: 1x Linear Actuator (forward/reverse relay control)
- **Device**: Metal gate/fuse box door actuator

#### Actuator Control

- Forward relay: PIN 33
- Reverse relay: PIN 34

#### Movement Control

- Forward/Reverse relay pairs
- Simple directional control with timed activation
- No external sensors

#### Safety Model

Actuator internal limit switches or mechanical endstops

#### Associated Devices

- Metal gate maglock: PIN 31
- Maglocks B, C, D: PINs 10, 11, 12
- 5x RFID readers (Serial ports 1,2,3,4,5)
- Fuse resistor sensors
- Knife switch sensor

#### Purpose

Secure puzzle container with motorized door lock/release mechanism

---

### 16. **chemical_v2** - Linear Actuator (Cabinet Door)

- **Location**: `hardware/Controller Code Teensy/Sentient_Connected/chemical_v2/`
- **Movement Type**: 1x Linear Actuator (forward/reverse relay control)
- **Device**: Chemical cabinet/puzzle door actuator

#### Actuator Control

- Forward relay: PIN 22
- Reverse relay: PIN 23

#### Movement Control

- Forward/Reverse relay pairs
- No sensors used for movement control
- RFID/TIR sensors are for puzzle logic, not actuator feedback

#### Safety Model

Actuator internal limits

#### Associated Devices

- Cabinet maglock: PIN 36
- 6x RFID readers with Tag-in-Range (TIR) sensors

#### Purpose

RFID puzzle with motorized door release

#### Note

Uses Tag-in-Range (TIR) for continuous presence detection

---

### 17. **boiler_room_subpanel_v2** - TV Lift & Environmental Controls

- **Location**: `hardware/Controller Code Teensy/Sentient_Connected/boiler_room_subpanel_v2/`
- **Movement Type**: 1x TV Lift Motor + Environmental Actuators

#### Motor Devices

**TV Lift Motor:**

- TV Power: PIN 32
- Lift Up: PIN 2
- Lift Down: PIN 3

**Fog Machine:**

- Fog Power: PIN 38
- Fog Trigger: PIN 33

**Ultrasonic Water Fogger:**

- PIN 34

#### Movement Control

- Up/Down relay pairs (Pins 2, 3)
- No sensors used for movement
- IR sensor is for puzzle input, not lift position

#### Safety Model

TV lift has internal limit switches

#### Additional Devices

- Barrel maglock: PIN 36
- 3x Study door maglocks: PINs 29, 30, 31
- 60x LED gauge progress strip: PIN 27
- IR sensor: PIN 35

#### Purpose

Complex boiler room with animated TV lift and environmental effects

#### Note

Multi-function controller combining motor control with maglocks and LEDs

---

## Summary Table: Sensor Requirements

| Controller                  | Motors | Sensor Type          | Requirement  | Critical Risk if Sensors Fail           |
| --------------------------- | ------ | -------------------- | ------------ | --------------------------------------- |
| **floor_v2**                | 1      | Proximity (4x)       | **REQUIRED** | Motor runs indefinitely                 |
| **lab_rm_doors_hoist_v2**   | 4      | Position (12x dual)  | **REQUIRED** | Cable damage, door collision            |
| **lab_rm_cage_a_v2**        | 3      | Position (8x dual)   | **REQUIRED** | Door overtravel, binding                |
| **lab_rm_cage_b_v2**        | 5      | Position (12x dual)  | **REQUIRED** | Door synchronization failure            |
| **study_d_v2**              | 2      | Proximity (8x)       | **REQUIRED** | Panel collision, motor overload         |
| **lever_boiler_v2**         | 1      | Proximity (2x)       | **REQUIRED** | Lever overtravel, binding               |
| **gauge_1_3_4_v2**          | 3      | Potentiometer (3x)   | ENHANCED     | Loss of valve sync (non-critical)       |
| **gauge_2_5_7_v2**          | 3      | Potentiometer (3x)   | ENHANCED     | Loss of valve sync (non-critical)       |
| **gauge_6_leds_v2**         | 1      | Potentiometer (1x)   | ENHANCED     | Loss of valve sync (non-critical)       |
| **riddle_v2**               | 2      | Proximity (endstops) | ENHANCED     | Loss of homing reference                |
| **lever_fan_safe_v2**       | 1      | Photocell (2x)       | ENHANCED     | Loss of lever position tracking         |
| **clock_v2**                | 3      | None                 | INDEPENDENT  | No feedback (assumes mechanical limits) |
| **study_b_v2**              | 4      | None                 | INDEPENDENT  | No feedback (assumes mechanical limits) |
| **maks_servo_v2**           | 1      | None                 | INDEPENDENT  | Servo has internal feedback             |
| **fuse_v2**                 | 1      | None                 | INDEPENDENT  | Actuator has internal limits            |
| **chemical_v2**             | 1      | None                 | INDEPENDENT  | Actuator has internal limits            |
| **boiler_room_subpanel_v2** | 1      | None                 | INDEPENDENT  | TV lift has internal limits             |

---

## Sensor Failure Modes & Mitigation

### Critical Controllers (Sensor-Required)

**Failure Modes:**

- Sensor disconnection → motor runs indefinitely
- Sensor false triggering → premature stop or no movement
- Dual sensor mismatch → movement blocked

**Mitigation Strategies:**

1. **Dual-Redundant Sensors**: Both sensors must agree before stopping (implemented in lab doors/hoist)
2. **Timeout Protection**: Software watchdog to stop motor after max expected travel time
3. **Current Monitoring**: Detect motor stall via driver feedback
4. **Heartbeat Monitoring**: MQTT connectivity loss triggers emergency stop
5. **Manual E-Stop**: Physical emergency stop buttons on power distribution

### Enhanced Controllers (Sensor-Enhanced)

**Failure Modes:**

- Potentiometer drift → incorrect gauge positioning
- Endstop failure → loss of homing reference

**Mitigation Strategies:**

1. **Fallback to Step Counting**: Can accept direct position commands via MQTT
2. **Periodic Recalibration**: Manual homing commands to re-establish reference
3. **Position Bounds Checking**: Software limits prevent extreme positions

### Independent Controllers (Sensor-Independent)

**Failure Modes:**

- Step loss (motor skipping) → position error accumulates
- Mechanical obstruction → motor stall

**Mitigation Strategies:**

1. **Mechanical Hard Stops**: Physical limits prevent damage
2. **Driver Current Limiting**: Stepper drivers have overcurrent protection
3. **Periodic Homing**: Manual reset to known positions
4. **Visual Inspection**: Game operators verify positions during reset

---

## Movement Hardware Summary by Type

### Stepper Motors

- **Total**: 43 stepper motors
- **Driver Types**:
  - Standard DRIVER mode (A4988/DRV8825): 7 motors
  - FULL4WIRE (4-phase direct control): 30 motors
  - DM542 differential signaling: 6 motors
- **Applications**:
  - Pressure gauge needles: 7 motors
  - Door mechanisms: 13 motors
  - Hoist/lift systems: 2 motors
  - Clock hands: 3 motors
  - Wall gears: 4 motors
  - Fan actuators: 2 motors
  - Puzzle mechanisms: 12 motors

### Servo Motors

- **Total**: 1 servo motor (PWMServo)
- **Application**: Animatronic/puppet mechanism

### Linear Actuators

- **Total**: 3 linear actuators
- **Applications**:
  - Door/gate release: 2 actuators
  - TV lift: 1 actuator

### Associated Movement Devices

- **Solenoids**: 4 (rope drop, canister, cuckoo, safe door)
- **Maglocks**: 13 (various door/gate security)
- **Motor Relays**: 6 (fog machines, TV power, fan power)

---

## Control Methodologies

### AccelStepper Library (11 controllers)

- Smooth acceleration/deceleration curves
- Precise speed and position control
- Support for multiple stepper types (DRIVER, FULL4WIRE)
- Automatic step timing and direction management

### Custom Pulse/Direction Control (2 controllers)

- Manual microsecond-level timing control
- Direct GPIO manipulation for step sequences
- Custom acceleration profiles
- Used for specialized timing requirements

### Differential Signaling (3 controllers)

- DM542 professional stepper drivers
- STEP+/STEP- and DIR+/DIR- differential pairs
- Enhanced noise immunity for long cable runs
- Higher current capacity for larger motors

### PWM Servo Control (1 controller)

- Standard hobby servo interface
- Position-based control (0-180 degrees)
- Simple command interface (open/close/set_position)

### Relay-Based Actuator Control (2 controllers)

- Forward/reverse relay pairs
- Simple on/off directional control
- Used for linear actuators and motorized mechanisms

---

## Safety Features

### Endstop/Limit Switch Protection

Controllers with proximity/hall effect sensors for movement limits:

- **floor_v2** (drawer open/closed) - 4x sensors
- **lab_rm_doors_hoist_v2** (hoist up/down, door open/close) - 12x sensors
- **lab_rm_cage_a_v2** (door open/close) - 8x sensors
- **lab_rm_cage_b_v2** (door open/close) - 12x sensors
- **lever_boiler_v2** (lever up/down) - 2x sensors
- **study_d_v2** (wall panel top/bottom) - 8x sensors
- **gauge_6_leds_v2** (valve position limits) - 1x potentiometer
- **riddle_v2** (endstops) - multiple proximity sensors

### Interlock Systems

- Maglocks disabled before motor activation (prevents motor strain)
- Door position verification before actuation
- Emergency stop support via MQTT commands
- Heartbeat monitoring for connectivity loss detection

---

## Firmware Version Standards

All controllers use **Teensy Controller Firmware v2.0.x** with:

- MQTT connectivity (local IP: 192.168.2.3:1883)
- Capability manifest auto-registration
- Real-time sensor publishing
- Standardized topic structure: `client/room/category/controller/device/item`
- Safety interlocks and endstop monitoring
- Heartbeat telemetry (10-second intervals)

---

## Development Notes

### Compiling Teensy Firmware

From VS Code: Use task **"Compile Current Teensy"**

Manual compilation:

```bash
/opt/sentient/scripts/compile_current.sh ${file}
```

**Target**: `teensy:avr:teensy41`
**Toolchain**: bundled arduino-cli at `bin/`
**Output**: `hardware/HEX_UPLOAD_FILES/<Controller>/`

### MQTT Testing

Subscribe to movement commands:

```bash
mosquitto_sub -h 192.168.2.3 -p 1883 -u paragon_devices -P password \
  -t 'paragon/clockwork/commands/+/+/#' -v
```

Publish stepper command example:

```bash
mosquitto_pub -h 192.168.2.3 -p 1883 -u paragon_devices -P password \
  -t 'paragon/clockwork/commands/gauge_1_3_4/gauge_1/set_position' \
  -m '{"value":1500,"timestamp":'$(date +%s%3N)'}'
```

---

## Document Maintenance

**Maintained by**: Sentient Engine Development Team
**Review Cycle**: Update when new movement controllers are added
**Related Documentation**:

- `Documentation/TEENSY_V2_COMPLETE_GUIDE.md` - Firmware standards
- `../docs/SYSTEM_ARCHITECTURE_v4.md` - System architecture
- `../docs/SENTIENT_DATA_FLOW.md` - Topic structure and event flows
- `BUILD_PROCESS.md` - Compilation and deployment
