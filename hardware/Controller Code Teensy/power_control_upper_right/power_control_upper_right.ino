// ══════════════════════════════════════════════════════════════════════════════
// Power Control Upper Right v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Power Control Upper Right - Zone Power Distribution Controller
 *
 * Controlled Systems:
 * - Main Lighting (24V, 12V, 5V rails)
 * - Gauge Cluster Power (12V A, 12V B, 5V)
 * - Lever Boiler Power (5V, 12V)
 * - Pilot Light Power (5V)
 * - Kraken Controls Power (5V)
 * - Fuse Puzzle Power (12V, 5V)
 * - Syringe Puzzle Power (24V, 12V, 5V)
 * - Chemical Puzzle Power (24V, 12V, 5V)
 * - Special Effects (Crawl Space Blacklight, Floor Audio Amp, Kraken Radar Amp)
 * - Vault Power (24V, 12V, 5V)
 *
 * Total: 24 Relay Outputs
 *
 * STATELESS ARCHITECTURE:
 * - Sentient decides all power state changes
 * - Controller executes ON/OFF commands and reports status
 * - No autonomous behavior - pure input/output controller
 */

#define USE_NO_SEND_PWM
#define SUPPRESS_ERROR_MESSAGE_FOR_BEGIN

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 512
#endif

#include <SentientCapabilityManifest.h>
#include <SentientMQTT.h>
#include <SentientDeviceRegistry.h>
#include <ArduinoJson.h>

#include "FirmwareMetadata.h"
#include "controller_naming.h"

// ──────────────────────────────────────────────────────────────────────────────
// Pin Assignments (24 relay outputs)
// ──────────────────────────────────────────────────────────────────────────────
const int power_led_pin = 13;

// Main Lighting Power Rails
const int main_lighting_24v_pin = 9;
const int main_lighting_12v_pin = 10;
const int main_lighting_5v_pin = 11;

// Gauge Cluster Power Rails
const int gauges_12v_a_pin = 3;
const int gauges_12v_b_pin = 4;
const int gauges_5v_pin = 5;

// Lever Boiler Power Rails
const int lever_boiler_5v_pin = 6;
const int lever_boiler_12v_pin = 7;

// Pilot Light Power
const int pilot_light_5v_pin = 8;

// Kraken Controls Power
const int kraken_controls_5v_pin = 0;

// Fuse Puzzle Power Rails
const int fuse_12v_pin = 1;
const int fuse_5v_pin = 2;

// Syringe Puzzle Power Rails
const int syringe_24v_pin = 28;
const int syringe_12v_pin = 27;
const int syringe_5v_pin = 26;

// Chemical Puzzle Power Rails
const int chemical_24v_pin = 25;
const int chemical_12v_pin = 24;
const int chemical_5v_pin = 12;

// Special Effects Power
const int crawl_space_blacklight_pin = 31;
const int floor_audio_amp_pin = 30;
const int kraken_radar_amp_pin = 29;

// Vault Power Rails
const int vault_24v_pin = 33;
const int vault_12v_pin = 34;
const int vault_5v_pin = 32;

// ──────────────────────────────────────────────────────────────────────────────
// Configuration Constants
// ──────────────────────────────────────────────────────────────────────────────
const unsigned long heartbeat_interval_ms = 5000;
static const size_t metadata_json_capacity = 1024;

// ──────────────────────────────────────────────────────────────────────────────
// MQTT Configuration
// ──────────────────────────────────────────────────────────────────────────────
const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_user = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";

// ──────────────────────────────────────────────────────────────────────────────
// Hardware State Variables (relay states: true = ON/energized, false = OFF)
// ──────────────────────────────────────────────────────────────────────────────
bool main_lighting_24v_state = false;
bool main_lighting_12v_state = false;
bool main_lighting_5v_state = false;
bool gauges_12v_a_state = false;
bool gauges_12v_b_state = false;
bool gauges_5v_state = false;
bool lever_boiler_5v_state = false;
bool lever_boiler_12v_state = false;
bool pilot_light_5v_state = false;
bool kraken_controls_5v_state = false;
bool fuse_12v_state = false;
bool fuse_5v_state = false;
bool syringe_24v_state = false;
bool syringe_12v_state = false;
bool syringe_5v_state = false;
bool chemical_24v_state = false;
bool chemical_12v_state = false;
bool chemical_5v_state = false;
bool crawl_space_blacklight_state = false;
bool floor_audio_amp_state = false;
bool kraken_radar_amp_state = false;
bool vault_24v_state = false;
bool vault_12v_state = false;
bool vault_5v_state = false;

// ============================================================================
// DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!)
// ============================================================================

// Standard power commands (on/off)
const char *power_commands[] = {
    naming::CMD_POWER_ON,
    naming::CMD_POWER_OFF};

// Controller-level commands
const char *controller_commands[] = {
    naming::CMD_ALL_ON,
    naming::CMD_ALL_OFF,
    naming::CMD_EMERGENCY_OFF,
    naming::CMD_RESET,
    naming::CMD_REQUEST_STATUS};

// Create device definitions for all 24 relays
SentientDeviceDef dev_main_lighting_24v(naming::DEV_MAIN_LIGHTING_24V, naming::FRIENDLY_MAIN_LIGHTING_24V, "relay", power_commands, 2);
SentientDeviceDef dev_main_lighting_12v(naming::DEV_MAIN_LIGHTING_12V, naming::FRIENDLY_MAIN_LIGHTING_12V, "relay", power_commands, 2);
SentientDeviceDef dev_main_lighting_5v(naming::DEV_MAIN_LIGHTING_5V, naming::FRIENDLY_MAIN_LIGHTING_5V, "relay", power_commands, 2);
SentientDeviceDef dev_gauges_12v_a(naming::DEV_GAUGES_12V_A, naming::FRIENDLY_GAUGES_12V_A, "relay", power_commands, 2);
SentientDeviceDef dev_gauges_12v_b(naming::DEV_GAUGES_12V_B, naming::FRIENDLY_GAUGES_12V_B, "relay", power_commands, 2);
SentientDeviceDef dev_gauges_5v(naming::DEV_GAUGES_5V, naming::FRIENDLY_GAUGES_5V, "relay", power_commands, 2);
SentientDeviceDef dev_lever_boiler_5v(naming::DEV_LEVER_BOILER_5V, naming::FRIENDLY_LEVER_BOILER_5V, "relay", power_commands, 2);
SentientDeviceDef dev_lever_boiler_12v(naming::DEV_LEVER_BOILER_12V, naming::FRIENDLY_LEVER_BOILER_12V, "relay", power_commands, 2);
SentientDeviceDef dev_pilot_light_5v(naming::DEV_PILOT_LIGHT_5V, naming::FRIENDLY_PILOT_LIGHT_5V, "relay", power_commands, 2);
SentientDeviceDef dev_kraken_controls_5v(naming::DEV_KRAKEN_CONTROLS_5V, naming::FRIENDLY_KRAKEN_CONTROLS_5V, "relay", power_commands, 2);
SentientDeviceDef dev_fuse_12v(naming::DEV_FUSE_12V, naming::FRIENDLY_FUSE_12V, "relay", power_commands, 2);
SentientDeviceDef dev_fuse_5v(naming::DEV_FUSE_5V, naming::FRIENDLY_FUSE_5V, "relay", power_commands, 2);
SentientDeviceDef dev_syringe_24v(naming::DEV_SYRINGE_24V, naming::FRIENDLY_SYRINGE_24V, "relay", power_commands, 2);
SentientDeviceDef dev_syringe_12v(naming::DEV_SYRINGE_12V, naming::FRIENDLY_SYRINGE_12V, "relay", power_commands, 2);
SentientDeviceDef dev_syringe_5v(naming::DEV_SYRINGE_5V, naming::FRIENDLY_SYRINGE_5V, "relay", power_commands, 2);
SentientDeviceDef dev_chemical_24v(naming::DEV_CHEMICAL_24V, naming::FRIENDLY_CHEMICAL_24V, "relay", power_commands, 2);
SentientDeviceDef dev_chemical_12v(naming::DEV_CHEMICAL_12V, naming::FRIENDLY_CHEMICAL_12V, "relay", power_commands, 2);
SentientDeviceDef dev_chemical_5v(naming::DEV_CHEMICAL_5V, naming::FRIENDLY_CHEMICAL_5V, "relay", power_commands, 2);
SentientDeviceDef dev_crawl_space_blacklight(naming::DEV_CRAWL_SPACE_BLACKLIGHT, naming::FRIENDLY_CRAWL_SPACE_BLACKLIGHT, "relay", power_commands, 2);
SentientDeviceDef dev_floor_audio_amp(naming::DEV_FLOOR_AUDIO_AMP, naming::FRIENDLY_FLOOR_AUDIO_AMP, "relay", power_commands, 2);
SentientDeviceDef dev_kraken_radar_amp(naming::DEV_KRAKEN_RADAR_AMP, naming::FRIENDLY_KRAKEN_RADAR_AMP, "relay", power_commands, 2);
SentientDeviceDef dev_vault_24v(naming::DEV_VAULT_24V, naming::FRIENDLY_VAULT_24V, "relay", power_commands, 2);
SentientDeviceDef dev_vault_12v(naming::DEV_VAULT_12V, naming::FRIENDLY_VAULT_12V, "relay", power_commands, 2);
SentientDeviceDef dev_vault_5v(naming::DEV_VAULT_5V, naming::FRIENDLY_VAULT_5V, "relay", power_commands, 2);
SentientDeviceDef dev_controller(naming::DEV_CONTROLLER, naming::FRIENDLY_CONTROLLER, "controller", controller_commands, 5);

// Create the device registry
SentientDeviceRegistry deviceRegistry;

// ──────────────────────────────────────────────────────────────────────────────
// Forward Declarations
// ──────────────────────────────────────────────────────────────────────────────
void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);
void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx);
void set_relay_state(int pin, bool state, bool &state_var, const char *device_name, const char *device_id, const char *command);
void publish_relay_state(const char *device_id, bool state);
void publish_command_acknowledgement(const char *device_id, const char *command, bool state);
void publish_hardware_status();
void publish_full_status();
void report_actual_relay_states();
void all_relays_on();
void all_relays_off();
void emergency_power_off();
const char *build_device_identifier();
const char *get_hardware_label();

// MQTT objects
SentientCapabilityManifest manifest;
SentientMQTT mqtt(build_mqtt_config());

// ============================================================================
// SECTION 2: SETUP FUNCTION
// ============================================================================

void setup()
{
    Serial.begin(115200);
    unsigned long waited = 0;
    while (!Serial && waited < 2000)
    {
        delay(10);
        waited += 10;
    }

    Serial.println(F("=== Power Control Upper Right v2.0.1 - STATELESS MODE ==="));
    Serial.print(F("Board: "));
    Serial.println(teensyBoardVersion());
    Serial.print(F("USB SN: "));
    Serial.println(teensyUsbSN());
    Serial.print(F("MAC: "));
    Serial.println(teensyMAC());
    Serial.print(F("Firmware: "));
    Serial.println(firmware::VERSION);
    Serial.print(F("Controller ID: "));
    Serial.println(naming::CONTROLLER_ID);

    // Initialize power LED
    pinMode(power_led_pin, OUTPUT);
    digitalWrite(power_led_pin, HIGH);

    // Initialize all relay pins (LOW = OFF by default)
    pinMode(main_lighting_24v_pin, OUTPUT);
    pinMode(main_lighting_12v_pin, OUTPUT);
    pinMode(main_lighting_5v_pin, OUTPUT);
    pinMode(gauges_12v_a_pin, OUTPUT);
    pinMode(gauges_12v_b_pin, OUTPUT);
    pinMode(gauges_5v_pin, OUTPUT);
    pinMode(lever_boiler_5v_pin, OUTPUT);
    pinMode(lever_boiler_12v_pin, OUTPUT);
    pinMode(pilot_light_5v_pin, OUTPUT);
    pinMode(kraken_controls_5v_pin, OUTPUT);
    pinMode(fuse_12v_pin, OUTPUT);
    pinMode(fuse_5v_pin, OUTPUT);
    pinMode(syringe_24v_pin, OUTPUT);
    pinMode(syringe_12v_pin, OUTPUT);
    pinMode(syringe_5v_pin, OUTPUT);
    pinMode(chemical_24v_pin, OUTPUT);
    pinMode(chemical_12v_pin, OUTPUT);
    pinMode(chemical_5v_pin, OUTPUT);
    pinMode(crawl_space_blacklight_pin, OUTPUT);
    pinMode(floor_audio_amp_pin, OUTPUT);
    pinMode(kraken_radar_amp_pin, OUTPUT);
    pinMode(vault_24v_pin, OUTPUT);
    pinMode(vault_12v_pin, OUTPUT);
    pinMode(vault_5v_pin, OUTPUT);

    // Set all relays to OFF
    digitalWrite(main_lighting_24v_pin, LOW);
    digitalWrite(main_lighting_12v_pin, LOW);
    digitalWrite(main_lighting_5v_pin, LOW);
    digitalWrite(gauges_12v_a_pin, LOW);
    digitalWrite(gauges_12v_b_pin, LOW);
    digitalWrite(gauges_5v_pin, LOW);
    digitalWrite(lever_boiler_5v_pin, LOW);
    digitalWrite(lever_boiler_12v_pin, LOW);
    digitalWrite(pilot_light_5v_pin, LOW);
    digitalWrite(kraken_controls_5v_pin, LOW);
    digitalWrite(fuse_12v_pin, LOW);
    digitalWrite(fuse_5v_pin, LOW);
    digitalWrite(syringe_24v_pin, LOW);
    digitalWrite(syringe_12v_pin, LOW);
    digitalWrite(syringe_5v_pin, LOW);
    digitalWrite(chemical_24v_pin, LOW);
    digitalWrite(chemical_12v_pin, LOW);
    digitalWrite(chemical_5v_pin, LOW);
    digitalWrite(crawl_space_blacklight_pin, LOW);
    digitalWrite(floor_audio_amp_pin, LOW);
    digitalWrite(kraken_radar_amp_pin, LOW);
    digitalWrite(vault_24v_pin, LOW);
    digitalWrite(vault_12v_pin, LOW);
    digitalWrite(vault_5v_pin, LOW);

    Serial.println(F("[PowerCtrl] All 24 relays initialized to OFF"));

    // Register all devices
    Serial.println(F("[PowerCtrl] Registering devices..."));
    deviceRegistry.addDevice(&dev_main_lighting_24v);
    deviceRegistry.addDevice(&dev_main_lighting_12v);
    deviceRegistry.addDevice(&dev_main_lighting_5v);
    deviceRegistry.addDevice(&dev_gauges_12v_a);
    deviceRegistry.addDevice(&dev_gauges_12v_b);
    deviceRegistry.addDevice(&dev_gauges_5v);
    deviceRegistry.addDevice(&dev_lever_boiler_5v);
    deviceRegistry.addDevice(&dev_lever_boiler_12v);
    deviceRegistry.addDevice(&dev_pilot_light_5v);
    deviceRegistry.addDevice(&dev_kraken_controls_5v);
    deviceRegistry.addDevice(&dev_fuse_12v);
    deviceRegistry.addDevice(&dev_fuse_5v);
    deviceRegistry.addDevice(&dev_syringe_24v);
    deviceRegistry.addDevice(&dev_syringe_12v);
    deviceRegistry.addDevice(&dev_syringe_5v);
    deviceRegistry.addDevice(&dev_chemical_24v);
    deviceRegistry.addDevice(&dev_chemical_12v);
    deviceRegistry.addDevice(&dev_chemical_5v);
    deviceRegistry.addDevice(&dev_crawl_space_blacklight);
    deviceRegistry.addDevice(&dev_floor_audio_amp);
    deviceRegistry.addDevice(&dev_kraken_radar_amp);
    deviceRegistry.addDevice(&dev_vault_24v);
    deviceRegistry.addDevice(&dev_vault_12v);
    deviceRegistry.addDevice(&dev_vault_5v);
    deviceRegistry.addDevice(&dev_controller);
    deviceRegistry.printSummary();

    // Build capability manifest
    Serial.println(F("[PowerCtrl] Building capability manifest..."));
    build_capability_manifest();
    Serial.println(F("[PowerCtrl] Manifest built successfully"));

    // Initialize MQTT
    Serial.println(F("[PowerCtrl] Initializing MQTT..."));
    if (!mqtt.begin())
    {
        Serial.println(F("[PowerCtrl] MQTT initialization failed - continuing without network"));
    }
    else
    {
        Serial.println(F("[PowerCtrl] MQTT initialization successful"));
        mqtt.setHeartbeatBuilder(build_heartbeat_payload);

        // Wait for broker connection (max 5 seconds)
        Serial.println(F("[PowerCtrl] Waiting for broker connection..."));
        unsigned long connection_start = millis();
        while (!mqtt.isConnected() && (millis() - connection_start < 5000))
        {
            mqtt.loop();
            delay(100);
        }

        if (mqtt.isConnected())
        {
            Serial.println(F("[PowerCtrl] Broker connected!"));

            // Register with Sentient system
            Serial.println(F("[PowerCtrl] Registering with Sentient system..."));
            if (manifest.publish_registration(mqtt.get_client(), naming::ROOM_ID, naming::CONTROLLER_ID))
            {
                Serial.println(F("[PowerCtrl] Registration successful!"));
            }
            else
            {
                Serial.println(F("[PowerCtrl] Registration failed - will retry later"));
            }

            // Subscribe to device-scoped commands
            String topic = String(naming::CLIENT_ID) + "/" + naming::ROOM_ID + "/commands/" + naming::CONTROLLER_ID + "/+/+";
            mqtt.get_client().subscribe(topic.c_str());
            Serial.print(F("[PowerCtrl] Subscribed to: "));
            Serial.println(topic); // Set custom message handler for device routing
            mqtt.get_client().setCallback([](char *topic, uint8_t *payload, unsigned int length)
                                          {
                // Parse topic to extract device and command
                String device, command;
                
                // Parse topic segments: client/room/commands/controller/device/command
                int seg = 0;
                const char *p = topic;
                const char *segStart[8] = {nullptr};
                size_t segLen[8] = {0};
                segStart[0] = p;
                
                while (*p && seg < 8) {
                    if (*p == '/') {
                        segLen[seg] = p - segStart[seg];
                        seg++;
                        if (seg < 8) segStart[seg] = p + 1;
                    }
                    p++;
                }
                if (seg < 8 && segStart[seg]) {
                    segLen[seg] = p - segStart[seg];
                    seg++;
                }
                
                auto equalsSeg = [&](int idx, const char *lit) -> bool {
                    size_t litLen = strlen(lit);
                    return segLen[idx] == litLen && strncmp(segStart[idx], lit, litLen) == 0;
                };
                
                // Validate topic structure and extract device/command
                if (seg >= 6 && equalsSeg(0, naming::CLIENT_ID) && equalsSeg(1, naming::ROOM_ID) && 
                    equalsSeg(2, "commands") && equalsSeg(3, naming::CONTROLLER_ID)) {
                    device = String(segStart[4]).substring(0, segLen[4]);
                    command = String(segStart[5]).substring(0, segLen[5]);
                    
                    Serial.print(F("[PowerCtrl] Device: "));
                    Serial.print(device);
                    Serial.print(F(" Command: "));
                    Serial.println(command);
                    
                    // Route to device-specific handlers
                    if (device == naming::DEV_MAIN_LIGHTING_24V) {
                        if (command == "power_on") {
                            set_relay_state(main_lighting_24v_pin, true, main_lighting_24v_state, "Main Lighting 24V", naming::DEV_MAIN_LIGHTING_24V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(main_lighting_24v_pin, false, main_lighting_24v_state, "Main Lighting 24V", naming::DEV_MAIN_LIGHTING_24V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_MAIN_LIGHTING_12V) {
                        if (command == "power_on") {
                            set_relay_state(main_lighting_12v_pin, true, main_lighting_12v_state, "Main Lighting 12V", naming::DEV_MAIN_LIGHTING_12V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(main_lighting_12v_pin, false, main_lighting_12v_state, "Main Lighting 12V", naming::DEV_MAIN_LIGHTING_12V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_MAIN_LIGHTING_5V) {
                        if (command == "power_on") {
                            set_relay_state(main_lighting_5v_pin, true, main_lighting_5v_state, "Main Lighting 5V", naming::DEV_MAIN_LIGHTING_5V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(main_lighting_5v_pin, false, main_lighting_5v_state, "Main Lighting 5V", naming::DEV_MAIN_LIGHTING_5V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_GAUGES_12V_A) {
                        if (command == "power_on") {
                            set_relay_state(gauges_12v_a_pin, true, gauges_12v_a_state, "Gauges 12V A", naming::DEV_GAUGES_12V_A, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(gauges_12v_a_pin, false, gauges_12v_a_state, "Gauges 12V A", naming::DEV_GAUGES_12V_A, "power_off");
                        }
                    }
                    else if (device == naming::DEV_GAUGES_12V_B) {
                        if (command == "power_on") {
                            set_relay_state(gauges_12v_b_pin, true, gauges_12v_b_state, "Gauges 12V B", naming::DEV_GAUGES_12V_B, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(gauges_12v_b_pin, false, gauges_12v_b_state, "Gauges 12V B", naming::DEV_GAUGES_12V_B, "power_off");
                        }
                    }
                    else if (device == naming::DEV_GAUGES_5V) {
                        if (command == "power_on") {
                            set_relay_state(gauges_5v_pin, true, gauges_5v_state, "Gauges 5V", naming::DEV_GAUGES_5V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(gauges_5v_pin, false, gauges_5v_state, "Gauges 5V", naming::DEV_GAUGES_5V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_LEVER_BOILER_5V) {
                        if (command == "power_on") {
                            set_relay_state(lever_boiler_5v_pin, true, lever_boiler_5v_state, "Lever Boiler 5V", naming::DEV_LEVER_BOILER_5V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(lever_boiler_5v_pin, false, lever_boiler_5v_state, "Lever Boiler 5V", naming::DEV_LEVER_BOILER_5V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_LEVER_BOILER_12V) {
                        if (command == "power_on") {
                            set_relay_state(lever_boiler_12v_pin, true, lever_boiler_12v_state, "Lever Boiler 12V", naming::DEV_LEVER_BOILER_12V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(lever_boiler_12v_pin, false, lever_boiler_12v_state, "Lever Boiler 12V", naming::DEV_LEVER_BOILER_12V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_PILOT_LIGHT_5V) {
                        if (command == "power_on") {
                            set_relay_state(pilot_light_5v_pin, true, pilot_light_5v_state, "Pilot Light 5V", naming::DEV_PILOT_LIGHT_5V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(pilot_light_5v_pin, false, pilot_light_5v_state, "Pilot Light 5V", naming::DEV_PILOT_LIGHT_5V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_KRAKEN_CONTROLS_5V) {
                        if (command == "power_on") {
                            set_relay_state(kraken_controls_5v_pin, true, kraken_controls_5v_state, "Kraken Controls 5V", naming::DEV_KRAKEN_CONTROLS_5V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(kraken_controls_5v_pin, false, kraken_controls_5v_state, "Kraken Controls 5V", naming::DEV_KRAKEN_CONTROLS_5V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_FUSE_12V) {
                        if (command == "power_on") {
                            set_relay_state(fuse_12v_pin, true, fuse_12v_state, "Fuse 12V", naming::DEV_FUSE_12V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(fuse_12v_pin, false, fuse_12v_state, "Fuse 12V", naming::DEV_FUSE_12V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_FUSE_5V) {
                        if (command == "power_on") {
                            set_relay_state(fuse_5v_pin, true, fuse_5v_state, "Fuse 5V", naming::DEV_FUSE_5V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(fuse_5v_pin, false, fuse_5v_state, "Fuse 5V", naming::DEV_FUSE_5V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_SYRINGE_24V) {
                        if (command == "power_on") {
                            set_relay_state(syringe_24v_pin, true, syringe_24v_state, "Syringe 24V", naming::DEV_SYRINGE_24V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(syringe_24v_pin, false, syringe_24v_state, "Syringe 24V", naming::DEV_SYRINGE_24V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_SYRINGE_12V) {
                        if (command == "power_on") {
                            set_relay_state(syringe_12v_pin, true, syringe_12v_state, "Syringe 12V", naming::DEV_SYRINGE_12V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(syringe_12v_pin, false, syringe_12v_state, "Syringe 12V", naming::DEV_SYRINGE_12V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_SYRINGE_5V) {
                        if (command == "power_on") {
                            set_relay_state(syringe_5v_pin, true, syringe_5v_state, "Syringe 5V", naming::DEV_SYRINGE_5V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(syringe_5v_pin, false, syringe_5v_state, "Syringe 5V", naming::DEV_SYRINGE_5V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_CHEMICAL_24V) {
                        if (command == "power_on") {
                            set_relay_state(chemical_24v_pin, true, chemical_24v_state, "Chemical 24V", naming::DEV_CHEMICAL_24V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(chemical_24v_pin, false, chemical_24v_state, "Chemical 24V", naming::DEV_CHEMICAL_24V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_CHEMICAL_12V) {
                        if (command == "power_on") {
                            set_relay_state(chemical_12v_pin, true, chemical_12v_state, "Chemical 12V", naming::DEV_CHEMICAL_12V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(chemical_12v_pin, false, chemical_12v_state, "Chemical 12V", naming::DEV_CHEMICAL_12V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_CHEMICAL_5V) {
                        if (command == "power_on") {
                            set_relay_state(chemical_5v_pin, true, chemical_5v_state, "Chemical 5V", naming::DEV_CHEMICAL_5V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(chemical_5v_pin, false, chemical_5v_state, "Chemical 5V", naming::DEV_CHEMICAL_5V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_CRAWL_SPACE_BLACKLIGHT) {
                        if (command == "power_on") {
                            set_relay_state(crawl_space_blacklight_pin, true, crawl_space_blacklight_state, "Crawl Space Blacklight", naming::DEV_CRAWL_SPACE_BLACKLIGHT, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(crawl_space_blacklight_pin, false, crawl_space_blacklight_state, "Crawl Space Blacklight", naming::DEV_CRAWL_SPACE_BLACKLIGHT, "power_off");
                        }
                    }
                    else if (device == naming::DEV_FLOOR_AUDIO_AMP) {
                        if (command == "power_on") {
                            set_relay_state(floor_audio_amp_pin, true, floor_audio_amp_state, "Floor Audio Amp", naming::DEV_FLOOR_AUDIO_AMP, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(floor_audio_amp_pin, false, floor_audio_amp_state, "Floor Audio Amp", naming::DEV_FLOOR_AUDIO_AMP, "power_off");
                        }
                    }
                    else if (device == naming::DEV_KRAKEN_RADAR_AMP) {
                        if (command == "power_on") {
                            set_relay_state(kraken_radar_amp_pin, true, kraken_radar_amp_state, "Kraken Radar Amp", naming::DEV_KRAKEN_RADAR_AMP, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(kraken_radar_amp_pin, false, kraken_radar_amp_state, "Kraken Radar Amp", naming::DEV_KRAKEN_RADAR_AMP, "power_off");
                        }
                    }
                    else if (device == naming::DEV_VAULT_24V) {
                        if (command == "power_on") {
                            set_relay_state(vault_24v_pin, true, vault_24v_state, "Vault 24V", naming::DEV_VAULT_24V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(vault_24v_pin, false, vault_24v_state, "Vault 24V", naming::DEV_VAULT_24V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_VAULT_12V) {
                        if (command == "power_on") {
                            set_relay_state(vault_12v_pin, true, vault_12v_state, "Vault 12V", naming::DEV_VAULT_12V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(vault_12v_pin, false, vault_12v_state, "Vault 12V", naming::DEV_VAULT_12V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_VAULT_5V) {
                        if (command == "power_on") {
                            set_relay_state(vault_5v_pin, true, vault_5v_state, "Vault 5V", naming::DEV_VAULT_5V, "power_on");
                        } else if (command == "power_off") {
                            set_relay_state(vault_5v_pin, false, vault_5v_state, "Vault 5V", naming::DEV_VAULT_5V, "power_off");
                        }
                    }
                    else if (device == naming::DEV_CONTROLLER) {
                        // Controller-level commands handled separately
                        if (command == naming::CMD_ALL_ON) {
                            all_relays_on();
                        } else if (command == naming::CMD_ALL_OFF) {
                            all_relays_off();
                        } else if (command == naming::CMD_EMERGENCY_OFF) {
                            emergency_power_off();
                        } else if (command == naming::CMD_RESET) {
                            all_relays_off();
                        } else if (command == naming::CMD_REQUEST_STATUS) {
                            publish_full_status();
                        }
                    }
                    else {
                        Serial.print(F("[PowerCtrl] Unknown device: "));
                        Serial.println(device);
                    }

                    // Publish status update after command
                    publish_hardware_status();
                } });

            // Report actual physical relay states as single source of truth
            // This ensures database and cache reflect actual hardware state after power-up
            Serial.println(F("[PowerCtrl] Reporting actual relay states..."));
            report_actual_relay_states();
        }
        else
        {
            Serial.println(F("[PowerCtrl] Broker connection timeout - will retry in main loop"));
        }
    }

    Serial.println(F("[PowerCtrl] Ready - awaiting Sentient commands"));
    Serial.print(F("[PowerCtrl] Firmware: "));
    Serial.println(firmware::VERSION);
}

// ============================================================================
// SECTION 3: LOOP FUNCTION
// ============================================================================

void loop()
{
    // LISTEN for commands from Sentient
    mqtt.loop();
}

// ============================================================================
// SECTION 4: COMMAND HANDLER
// ============================================================================

void handle_mqtt_command(const char *command, const JsonDocument &payload, void *ctx)
{
    String cmd = String(command);

    Serial.print(F("[PowerCtrl] Command: "));
    Serial.println(command);

    // ──────────────────────────────────────────────────────────────────────────
    // INDIVIDUAL DEVICE COMMANDS
    // ──────────────────────────────────────────────────────────────────────────

    // Main Lighting
    if (cmd.equals(naming::CMD_POWER_ON))
    {
        // Command routing handled by device context from MQTT topic
        // This simplified handler will be expanded with device-specific routing
    }
    else if (cmd.equals(naming::CMD_POWER_OFF))
    {
        // Same as above
    }

    // ──────────────────────────────────────────────────────────────────────────
    // CONTROLLER-LEVEL COMMANDS
    // ──────────────────────────────────────────────────────────────────────────
    else if (cmd.equals(naming::CMD_ALL_ON))
    {
        Serial.println(F("[PowerCtrl] ALL ON command"));
        all_relays_on();
        publish_hardware_status();
    }
    else if (cmd.equals(naming::CMD_ALL_OFF))
    {
        Serial.println(F("[PowerCtrl] ALL OFF command"));
        all_relays_off();
        publish_hardware_status();
    }
    else if (cmd.equals(naming::CMD_EMERGENCY_OFF))
    {
        Serial.println(F("[PowerCtrl] EMERGENCY OFF command"));
        emergency_power_off();
        publish_hardware_status();
    }
    else if (cmd.equals(naming::CMD_RESET))
    {
        Serial.println(F("[PowerCtrl] RESET command"));
        all_relays_off();
        publish_hardware_status();
    }
    else if (cmd.equals(naming::CMD_REQUEST_STATUS))
    {
        Serial.println(F("[PowerCtrl] Status requested"));
        publish_full_status();
    }
    else
    {
        Serial.print(F("[PowerCtrl] Unknown command: "));
        Serial.println(command);
    }
}

// ============================================================================
// SECTION 5: RELAY CONTROL FUNCTIONS
// ============================================================================

/**
 * Publish individual relay state to MQTT
 * Topic: paragon/clockwork/status/{controller_id}/{device_id}/state
 * Payload: {"state": 1} or {"state": 0}
 */
void publish_relay_state(const char *device_id, bool state)
{
    JsonDocument doc;
    doc["state"] = state ? 1 : 0;
    doc["power"] = state;
    doc["ts"] = millis();

    // Publish to status/{device_id}/state
    String topic = String(naming::CLIENT_ID) + "/" + String(naming::ROOM_ID) + "/" +
                   String(naming::CAT_STATUS) + "/" + String(naming::CONTROLLER_ID) + "/" +
                   String(device_id) + "/state";

    char jsonBuffer[128];
    serializeJson(doc, jsonBuffer);

    mqtt.get_client().publish(topic.c_str(), jsonBuffer);

    Serial.print(F("[PowerCtrl] Published state for "));
    Serial.print(device_id);
    Serial.print(F(": "));
    Serial.println(state ? F("ON") : F("OFF"));
}

/**
 * Publish command acknowledgement to MQTT
 * Topic: paragon/clockwork/acknowledgement/{controller_id}/{device_id}/{command}
 * Payload: {"state": 1/0, "success": true, "ts": millis}
 *
 * This is published IMMEDIATELY after a command is executed to confirm receipt
 * and provide instant UI feedback separate from periodic status updates.
 */
void publish_command_acknowledgement(const char *device_id, const char *command, bool state)
{
    JsonDocument doc;
    doc["state"] = state ? 1 : 0;
    doc["power"] = state;
    doc["success"] = true;
    doc["command"] = command;
    doc["ts"] = millis();

    // Publish to acknowledgement/{controller_id}/{device_id}/{command}
    String topic = String(naming::CLIENT_ID) + "/" + String(naming::ROOM_ID) + "/" +
                   String(naming::CAT_ACKNOWLEDGEMENT) + "/" + String(naming::CONTROLLER_ID) + "/" +
                   String(device_id) + "/" + String(command);

    char jsonBuffer[128];
    serializeJson(doc, jsonBuffer);

    mqtt.get_client().publish(topic.c_str(), jsonBuffer);

    Serial.print(F("[PowerCtrl] Published ACK for "));
    Serial.print(device_id);
    Serial.print(F("/"));
    Serial.print(command);
    Serial.print(F(": "));
    Serial.println(state ? F("ON") : F("OFF"));
}

void set_relay_state(int pin, bool state, bool &state_var, const char *device_name, const char *device_id, const char *command)
{
    digitalWrite(pin, state ? HIGH : LOW);
    state_var = state;

    Serial.print(F("[PowerCtrl] "));
    Serial.print(device_name);
    Serial.print(F(": "));
    Serial.println(state ? F("ON") : F("OFF"));

    // Publish command acknowledgement for immediate UI feedback
    if (mqtt.isConnected() && command != nullptr)
    {
        publish_command_acknowledgement(device_id, command, state);
    }

    // Also publish individual relay state to MQTT for status updates
    if (mqtt.isConnected())
    {
        publish_relay_state(device_id, state);
    }
}

void all_relays_on()
{
    set_relay_state(main_lighting_24v_pin, true, main_lighting_24v_state, "Main Lighting 24V", naming::DEV_MAIN_LIGHTING_24V, nullptr);
    set_relay_state(main_lighting_12v_pin, true, main_lighting_12v_state, "Main Lighting 12V", naming::DEV_MAIN_LIGHTING_12V, nullptr);
    set_relay_state(main_lighting_5v_pin, true, main_lighting_5v_state, "Main Lighting 5V", naming::DEV_MAIN_LIGHTING_5V, nullptr);
    set_relay_state(gauges_12v_a_pin, true, gauges_12v_a_state, "Gauges 12V A", naming::DEV_GAUGES_12V_A, nullptr);
    set_relay_state(gauges_12v_b_pin, true, gauges_12v_b_state, "Gauges 12V B", naming::DEV_GAUGES_12V_B, nullptr);
    set_relay_state(gauges_5v_pin, true, gauges_5v_state, "Gauges 5V", naming::DEV_GAUGES_5V, nullptr);
    set_relay_state(lever_boiler_5v_pin, true, lever_boiler_5v_state, "Lever Boiler 5V", naming::DEV_LEVER_BOILER_5V, nullptr);
    set_relay_state(lever_boiler_12v_pin, true, lever_boiler_12v_state, "Lever Boiler 12V", naming::DEV_LEVER_BOILER_12V, nullptr);
    set_relay_state(pilot_light_5v_pin, true, pilot_light_5v_state, "Pilot Light 5V", naming::DEV_PILOT_LIGHT_5V, nullptr);
    set_relay_state(kraken_controls_5v_pin, true, kraken_controls_5v_state, "Kraken Controls 5V", naming::DEV_KRAKEN_CONTROLS_5V, nullptr);
    set_relay_state(fuse_12v_pin, true, fuse_12v_state, "Fuse 12V", naming::DEV_FUSE_12V, nullptr);
    set_relay_state(fuse_5v_pin, true, fuse_5v_state, "Fuse 5V", naming::DEV_FUSE_5V, nullptr);
    set_relay_state(syringe_24v_pin, true, syringe_24v_state, "Syringe 24V", naming::DEV_SYRINGE_24V, nullptr);
    set_relay_state(syringe_12v_pin, true, syringe_12v_state, "Syringe 12V", naming::DEV_SYRINGE_12V, nullptr);
    set_relay_state(syringe_5v_pin, true, syringe_5v_state, "Syringe 5V", naming::DEV_SYRINGE_5V, nullptr);
    set_relay_state(chemical_24v_pin, true, chemical_24v_state, "Chemical 24V", naming::DEV_CHEMICAL_24V, nullptr);
    set_relay_state(chemical_12v_pin, true, chemical_12v_state, "Chemical 12V", naming::DEV_CHEMICAL_12V, nullptr);
    set_relay_state(chemical_5v_pin, true, chemical_5v_state, "Chemical 5V", naming::DEV_CHEMICAL_5V, nullptr);
    set_relay_state(crawl_space_blacklight_pin, true, crawl_space_blacklight_state, "Crawl Space Blacklight", naming::DEV_CRAWL_SPACE_BLACKLIGHT, nullptr);
    set_relay_state(floor_audio_amp_pin, true, floor_audio_amp_state, "Floor Audio Amp", naming::DEV_FLOOR_AUDIO_AMP, nullptr);
    set_relay_state(kraken_radar_amp_pin, true, kraken_radar_amp_state, "Kraken Radar Amp", naming::DEV_KRAKEN_RADAR_AMP, nullptr);
    set_relay_state(vault_24v_pin, true, vault_24v_state, "Vault 24V", naming::DEV_VAULT_24V, nullptr);
    set_relay_state(vault_12v_pin, true, vault_12v_state, "Vault 12V", naming::DEV_VAULT_12V, nullptr);
    set_relay_state(vault_5v_pin, true, vault_5v_state, "Vault 5V", naming::DEV_VAULT_5V, nullptr);

    Serial.println(F("[PowerCtrl] All relays powered ON"));
}

void all_relays_off()
{
    set_relay_state(main_lighting_24v_pin, false, main_lighting_24v_state, "Main Lighting 24V", naming::DEV_MAIN_LIGHTING_24V, nullptr);
    set_relay_state(main_lighting_12v_pin, false, main_lighting_12v_state, "Main Lighting 12V", naming::DEV_MAIN_LIGHTING_12V, nullptr);
    set_relay_state(main_lighting_5v_pin, false, main_lighting_5v_state, "Main Lighting 5V", naming::DEV_MAIN_LIGHTING_5V, nullptr);
    set_relay_state(gauges_12v_a_pin, false, gauges_12v_a_state, "Gauges 12V A", naming::DEV_GAUGES_12V_A, nullptr);
    set_relay_state(gauges_12v_b_pin, false, gauges_12v_b_state, "Gauges 12V B", naming::DEV_GAUGES_12V_B, nullptr);
    set_relay_state(gauges_5v_pin, false, gauges_5v_state, "Gauges 5V", naming::DEV_GAUGES_5V, nullptr);
    set_relay_state(lever_boiler_5v_pin, false, lever_boiler_5v_state, "Lever Boiler 5V", naming::DEV_LEVER_BOILER_5V, nullptr);
    set_relay_state(lever_boiler_12v_pin, false, lever_boiler_12v_state, "Lever Boiler 12V", naming::DEV_LEVER_BOILER_12V, nullptr);
    set_relay_state(pilot_light_5v_pin, false, pilot_light_5v_state, "Pilot Light 5V", naming::DEV_PILOT_LIGHT_5V, nullptr);
    set_relay_state(kraken_controls_5v_pin, false, kraken_controls_5v_state, "Kraken Controls 5V", naming::DEV_KRAKEN_CONTROLS_5V, nullptr);
    set_relay_state(fuse_12v_pin, false, fuse_12v_state, "Fuse 12V", naming::DEV_FUSE_12V, nullptr);
    set_relay_state(fuse_5v_pin, false, fuse_5v_state, "Fuse 5V", naming::DEV_FUSE_5V, nullptr);
    set_relay_state(syringe_24v_pin, false, syringe_24v_state, "Syringe 24V", naming::DEV_SYRINGE_24V, nullptr);
    set_relay_state(syringe_12v_pin, false, syringe_12v_state, "Syringe 12V", naming::DEV_SYRINGE_12V, nullptr);
    set_relay_state(syringe_5v_pin, false, syringe_5v_state, "Syringe 5V", naming::DEV_SYRINGE_5V, nullptr);
    set_relay_state(chemical_24v_pin, false, chemical_24v_state, "Chemical 24V", naming::DEV_CHEMICAL_24V, nullptr);
    set_relay_state(chemical_12v_pin, false, chemical_12v_state, "Chemical 12V", naming::DEV_CHEMICAL_12V, nullptr);
    set_relay_state(chemical_5v_pin, false, chemical_5v_state, "Chemical 5V", naming::DEV_CHEMICAL_5V, nullptr);
    set_relay_state(crawl_space_blacklight_pin, false, crawl_space_blacklight_state, "Crawl Space Blacklight", naming::DEV_CRAWL_SPACE_BLACKLIGHT, nullptr);
    set_relay_state(floor_audio_amp_pin, false, floor_audio_amp_state, "Floor Audio Amp", naming::DEV_FLOOR_AUDIO_AMP, nullptr);
    set_relay_state(kraken_radar_amp_pin, false, kraken_radar_amp_state, "Kraken Radar Amp", naming::DEV_KRAKEN_RADAR_AMP, nullptr);
    set_relay_state(vault_24v_pin, false, vault_24v_state, "Vault 24V", naming::DEV_VAULT_24V, nullptr);
    set_relay_state(vault_12v_pin, false, vault_12v_state, "Vault 12V", naming::DEV_VAULT_12V, nullptr);
    set_relay_state(vault_5v_pin, false, vault_5v_state, "Vault 5V", naming::DEV_VAULT_5V, nullptr);

    Serial.println(F("[PowerCtrl] All relays powered OFF"));
}

void emergency_power_off()
{
    Serial.println(F("[PowerCtrl] !!! EMERGENCY POWER OFF !!!"));

    // Immediately cut all power
    digitalWrite(main_lighting_24v_pin, LOW);
    digitalWrite(main_lighting_12v_pin, LOW);
    digitalWrite(main_lighting_5v_pin, LOW);
    digitalWrite(gauges_12v_a_pin, LOW);
    digitalWrite(gauges_12v_b_pin, LOW);
    digitalWrite(gauges_5v_pin, LOW);
    digitalWrite(lever_boiler_5v_pin, LOW);
    digitalWrite(lever_boiler_12v_pin, LOW);
    digitalWrite(pilot_light_5v_pin, LOW);
    digitalWrite(kraken_controls_5v_pin, LOW);
    digitalWrite(fuse_12v_pin, LOW);
    digitalWrite(fuse_5v_pin, LOW);
    digitalWrite(syringe_24v_pin, LOW);
    digitalWrite(syringe_12v_pin, LOW);
    digitalWrite(syringe_5v_pin, LOW);
    digitalWrite(chemical_24v_pin, LOW);
    digitalWrite(chemical_12v_pin, LOW);
    digitalWrite(chemical_5v_pin, LOW);
    digitalWrite(crawl_space_blacklight_pin, LOW);
    digitalWrite(floor_audio_amp_pin, LOW);
    digitalWrite(kraken_radar_amp_pin, LOW);
    digitalWrite(vault_24v_pin, LOW);
    digitalWrite(vault_12v_pin, LOW);
    digitalWrite(vault_5v_pin, LOW);

    // Update all state variables
    main_lighting_24v_state = false;
    main_lighting_12v_state = false;
    main_lighting_5v_state = false;
    gauges_12v_a_state = false;
    gauges_12v_b_state = false;
    gauges_5v_state = false;
    lever_boiler_5v_state = false;
    lever_boiler_12v_state = false;
    pilot_light_5v_state = false;
    kraken_controls_5v_state = false;
    fuse_12v_state = false;
    fuse_5v_state = false;
    syringe_24v_state = false;
    syringe_12v_state = false;
    syringe_5v_state = false;
    chemical_24v_state = false;
    chemical_12v_state = false;
    chemical_5v_state = false;
    crawl_space_blacklight_state = false;
    floor_audio_amp_state = false;
    kraken_radar_amp_state = false;
    vault_24v_state = false;
    vault_12v_state = false;
    vault_5v_state = false;

    // Publish individual relay OFF states for real-time UI updates
    if (mqtt.isConnected())
    {
        publish_relay_state(naming::DEV_MAIN_LIGHTING_24V, false);
        publish_relay_state(naming::DEV_MAIN_LIGHTING_12V, false);
        publish_relay_state(naming::DEV_MAIN_LIGHTING_5V, false);
        publish_relay_state(naming::DEV_GAUGES_12V_A, false);
        publish_relay_state(naming::DEV_GAUGES_12V_B, false);
        publish_relay_state(naming::DEV_GAUGES_5V, false);
        publish_relay_state(naming::DEV_LEVER_BOILER_5V, false);
        publish_relay_state(naming::DEV_LEVER_BOILER_12V, false);
        publish_relay_state(naming::DEV_PILOT_LIGHT_5V, false);
        publish_relay_state(naming::DEV_KRAKEN_CONTROLS_5V, false);
        publish_relay_state(naming::DEV_FUSE_12V, false);
        publish_relay_state(naming::DEV_FUSE_5V, false);
        publish_relay_state(naming::DEV_SYRINGE_24V, false);
        publish_relay_state(naming::DEV_SYRINGE_12V, false);
        publish_relay_state(naming::DEV_SYRINGE_5V, false);
        publish_relay_state(naming::DEV_CHEMICAL_24V, false);
        publish_relay_state(naming::DEV_CHEMICAL_12V, false);
        publish_relay_state(naming::DEV_CHEMICAL_5V, false);
        publish_relay_state(naming::DEV_CRAWL_SPACE_BLACKLIGHT, false);
        publish_relay_state(naming::DEV_FLOOR_AUDIO_AMP, false);
        publish_relay_state(naming::DEV_KRAKEN_RADAR_AMP, false);
        publish_relay_state(naming::DEV_VAULT_24V, false);
        publish_relay_state(naming::DEV_VAULT_12V, false);
        publish_relay_state(naming::DEV_VAULT_5V, false);
    }

    // Publish emergency event
    JsonDocument doc;
    doc["event"] = "emergency_power_off";
    doc["controller"] = naming::CONTROLLER_ID;
    doc["ts"] = millis();
    mqtt.publishJson(naming::CAT_EVENTS, "emergency", doc);
}

// ============================================================================
// SECTION 6: STATUS PUBLISHING
// ============================================================================

void publish_hardware_status()
{
    JsonDocument doc;
    doc["main_lighting_24v"] = main_lighting_24v_state;
    doc["main_lighting_12v"] = main_lighting_12v_state;
    doc["main_lighting_5v"] = main_lighting_5v_state;
    doc["gauges_12v_a"] = gauges_12v_a_state;
    doc["gauges_12v_b"] = gauges_12v_b_state;
    doc["gauges_5v"] = gauges_5v_state;
    doc["lever_boiler_5v"] = lever_boiler_5v_state;
    doc["lever_boiler_12v"] = lever_boiler_12v_state;
    doc["pilot_light_5v"] = pilot_light_5v_state;
    doc["kraken_controls_5v"] = kraken_controls_5v_state;
    doc["fuse_12v"] = fuse_12v_state;
    doc["fuse_5v"] = fuse_5v_state;
    doc["syringe_24v"] = syringe_24v_state;
    doc["syringe_12v"] = syringe_12v_state;
    doc["syringe_5v"] = syringe_5v_state;
    doc["chemical_24v"] = chemical_24v_state;
    doc["chemical_12v"] = chemical_12v_state;
    doc["chemical_5v"] = chemical_5v_state;
    doc["crawl_space_blacklight"] = crawl_space_blacklight_state;
    doc["floor_audio_amp"] = floor_audio_amp_state;
    doc["kraken_radar_amp"] = kraken_radar_amp_state;
    doc["vault_24v"] = vault_24v_state;
    doc["vault_12v"] = vault_12v_state;
    doc["vault_5v"] = vault_5v_state;
    doc["ts"] = millis();
    doc["uid"] = naming::CONTROLLER_ID;

    mqtt.publishJson(naming::CAT_STATUS, naming::ITEM_HARDWARE, doc);
}

void publish_full_status()
{
    JsonDocument doc;

    // All relay states
    doc["main_lighting_24v"] = main_lighting_24v_state;
    doc["main_lighting_12v"] = main_lighting_12v_state;
    doc["main_lighting_5v"] = main_lighting_5v_state;
    doc["gauges_12v_a"] = gauges_12v_a_state;
    doc["gauges_12v_b"] = gauges_12v_b_state;
    doc["gauges_5v"] = gauges_5v_state;
    doc["lever_boiler_5v"] = lever_boiler_5v_state;
    doc["lever_boiler_12v"] = lever_boiler_12v_state;
    doc["pilot_light_5v"] = pilot_light_5v_state;
    doc["kraken_controls_5v"] = kraken_controls_5v_state;
    doc["fuse_12v"] = fuse_12v_state;
    doc["fuse_5v"] = fuse_5v_state;
    doc["syringe_24v"] = syringe_24v_state;
    doc["syringe_12v"] = syringe_12v_state;
    doc["syringe_5v"] = syringe_5v_state;
    doc["chemical_24v"] = chemical_24v_state;
    doc["chemical_12v"] = chemical_12v_state;
    doc["chemical_5v"] = chemical_5v_state;
    doc["crawl_space_blacklight"] = crawl_space_blacklight_state;
    doc["floor_audio_amp"] = floor_audio_amp_state;
    doc["kraken_radar_amp"] = kraken_radar_amp_state;
    doc["vault_24v"] = vault_24v_state;
    doc["vault_12v"] = vault_12v_state;
    doc["vault_5v"] = vault_5v_state;

    // Controller metadata
    doc["uptime"] = millis();
    doc["ts"] = millis();
    doc["uid"] = naming::CONTROLLER_ID;
    doc["fw"] = firmware::VERSION;

    mqtt.publishJson(naming::CAT_STATUS, "full", doc);
    Serial.println(F("[PowerCtrl] Full status published"));
}

/**
 * Report actual physical relay states after power-up
 *
 * CRITICAL: This establishes hardware as the single source of truth.
 * After power outages, this ensures database/cache reflect actual physical state.
 *
 * Reads actual pin states with digitalRead() and publishes to MQTT/database.
 */
void report_actual_relay_states()
{
    Serial.println(F("[PowerCtrl] === Reading Actual Physical Relay States ==="));

    // Read and report all 24 relays
    main_lighting_24v_state = digitalRead(main_lighting_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_MAIN_LIGHTING_24V, main_lighting_24v_state);

    main_lighting_12v_state = digitalRead(main_lighting_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_MAIN_LIGHTING_12V, main_lighting_12v_state);

    main_lighting_5v_state = digitalRead(main_lighting_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_MAIN_LIGHTING_5V, main_lighting_5v_state);

    gauges_12v_a_state = digitalRead(gauges_12v_a_pin) == HIGH;
    publish_relay_state(naming::DEV_GAUGES_12V_A, gauges_12v_a_state);

    gauges_12v_b_state = digitalRead(gauges_12v_b_pin) == HIGH;
    publish_relay_state(naming::DEV_GAUGES_12V_B, gauges_12v_b_state);

    gauges_5v_state = digitalRead(gauges_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_GAUGES_5V, gauges_5v_state);

    lever_boiler_5v_state = digitalRead(lever_boiler_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_LEVER_BOILER_5V, lever_boiler_5v_state);

    lever_boiler_12v_state = digitalRead(lever_boiler_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_LEVER_BOILER_12V, lever_boiler_12v_state);

    pilot_light_5v_state = digitalRead(pilot_light_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_PILOT_LIGHT_5V, pilot_light_5v_state);

    kraken_controls_5v_state = digitalRead(kraken_controls_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_KRAKEN_CONTROLS_5V, kraken_controls_5v_state);

    fuse_12v_state = digitalRead(fuse_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_FUSE_12V, fuse_12v_state);

    fuse_5v_state = digitalRead(fuse_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_FUSE_5V, fuse_5v_state);

    syringe_24v_state = digitalRead(syringe_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_SYRINGE_24V, syringe_24v_state);

    syringe_12v_state = digitalRead(syringe_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_SYRINGE_12V, syringe_12v_state);

    syringe_5v_state = digitalRead(syringe_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_SYRINGE_5V, syringe_5v_state);

    chemical_24v_state = digitalRead(chemical_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_CHEMICAL_24V, chemical_24v_state);

    chemical_12v_state = digitalRead(chemical_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_CHEMICAL_12V, chemical_12v_state);

    chemical_5v_state = digitalRead(chemical_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_CHEMICAL_5V, chemical_5v_state);

    crawl_space_blacklight_state = digitalRead(crawl_space_blacklight_pin) == HIGH;
    publish_relay_state(naming::DEV_CRAWL_SPACE_BLACKLIGHT, crawl_space_blacklight_state);

    floor_audio_amp_state = digitalRead(floor_audio_amp_pin) == HIGH;
    publish_relay_state(naming::DEV_FLOOR_AUDIO_AMP, floor_audio_amp_state);

    kraken_radar_amp_state = digitalRead(kraken_radar_amp_pin) == HIGH;
    publish_relay_state(naming::DEV_KRAKEN_RADAR_AMP, kraken_radar_amp_state);

    vault_24v_state = digitalRead(vault_24v_pin) == HIGH;
    publish_relay_state(naming::DEV_VAULT_24V, vault_24v_state);

    vault_12v_state = digitalRead(vault_12v_pin) == HIGH;
    publish_relay_state(naming::DEV_VAULT_12V, vault_12v_state);

    vault_5v_state = digitalRead(vault_5v_pin) == HIGH;
    publish_relay_state(naming::DEV_VAULT_5V, vault_5v_state);

    Serial.println(F("[PowerCtrl] === All 24 Relay States Reported ==="));

    // Also publish consolidated status for backward compatibility
    publish_hardware_status();
}

// ============================================================================
// SECTION 7: MQTT CONFIGURATION
// ============================================================================

void build_capability_manifest()
{
    manifest.set_controller_info(
        naming::CONTROLLER_ID,
        naming::CONTROLLER_FRIENDLY_NAME,
        firmware::VERSION,
        naming::ROOM_ID,
        naming::CONTROLLER_ID);

    // Auto-generate entire manifest from device registry
    deviceRegistry.buildManifest(manifest);
}

SentientMQTTConfig build_mqtt_config()
{
    SentientMQTTConfig cfg{};
    if (mqtt_host && mqtt_host[0] != '\0')
    {
        cfg.brokerHost = mqtt_host;
    }
    cfg.brokerIp = mqtt_broker_ip;
    cfg.brokerPort = mqtt_port;
    cfg.username = mqtt_user;
    cfg.password = mqtt_password;
    cfg.namespaceId = naming::CLIENT_ID;
    cfg.roomId = naming::ROOM_ID;
    cfg.controllerId = naming::CONTROLLER_ID;
    cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
    cfg.displayName = naming::CONTROLLER_FRIENDLY_NAME;
    cfg.publishJsonCapacity = 1536;
    cfg.heartbeatIntervalMs = heartbeat_interval_ms;
    cfg.autoHeartbeat = true;
#if !defined(ESP32)
    cfg.useDhcp = true;
#endif
    return cfg;
}

bool build_heartbeat_payload(JsonDocument &doc, void *ctx)
{
    doc["uid"] = naming::CONTROLLER_ID;
    doc["fw"] = firmware::VERSION;
    doc["up"] = millis();
    return true;
}

const char *build_device_identifier()
{
    static char buffer[32];
    String board = String(teensyBoardVersion());
    board.trim();
    if (board.length() == 0)
    {
        board = "Teensy Controller";
    }
    board.replace("Teensy", "teensy");
    board.toLowerCase();
    board.replace(" ", "");
    board.replace("-", "");
    board.replace(".", "");
    board.replace("/", "");
    board.toCharArray(buffer, sizeof(buffer));
    return buffer;
}

const char *get_hardware_label()
{
    static char label[32];
    static bool initialized = false;
    if (!initialized)
    {
        String board = String(teensyBoardVersion());
        board.trim();
        if (board.length() == 0)
        {
            board = "Teensy Controller";
        }
        board.toCharArray(label, sizeof(label));
        initialized = true;
    }
    return label;
}
