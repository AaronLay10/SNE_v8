// ══════════════════════════════════════════════════════════════════════════════
// Main Lighting Controller v2.3.0
// Teensy 4.1 - Connected to Sentient System (STATELESS EXECUTOR)
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Main Lighting Controller
 *
 * Controlled Systems:
 * - Study Lights (analog dimmer)
 * - Boiler Room Lights (analog dimmer)
 * - Lab Lights (8 ceiling squares + 3 floor grates via FastLED)
 * - Sconces (digital relay)
 * - Crawlspace Lights (digital relay)
 *
 * STATELESS ARCHITECTURE:
 * - Device-scoped command routing via MQTT topic parsing
 * - Command acknowledgments published on events channel
 * - Pure command-driven controller with state updates
 * - No autonomous behavior - executes commands from Sentient
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
#include <FastLED.h>

#include "FirmwareMetadata.h"
#include "controller_naming.h"

// ──────────────────────────────────────────────────────────────────────────────
// Device Definitions
// ──────────────────────────────────────────────────────────────────────────────
// *** Study Lights - Analog dimmer control (PWM 0-255)
// *** Boiler Room Lights - Analog dimmer control (PWM 0-255)
// *** Lab Lights - 11 FastLED strips (8 ceiling squares + 3 floor grates)
// *** Sconces - Digital relay control (ON/OFF)
// *** Crawlspace Lights - Digital relay control (ON/OFF)

// ──────────────────────────────────────────────────────────────────────────────
// Pin Assignments
// ──────────────────────────────────────────────────────────────────────────────
const int power_led_pin = 13;

// LED Strips (FastLED)
const int ceiling_square_a_pin = 4;
const int ceiling_square_b_pin = 3;
const int ceiling_square_c_pin = 5;
const int ceiling_square_d_pin = 6;
const int ceiling_square_e_pin = 1;
const int ceiling_square_f_pin = 8;
const int ceiling_square_g_pin = 7;
const int ceiling_square_h_pin = 10;
const int grate_1_pin = 2;
const int grate_2_pin = 0;
const int grate_3_pin = 9;

// Analog Dimmers
const int study_lights_pin = A1;
const int boiler_lights_pin = A4;

// Digital Relays
const int sconces_pin = 12;
const int crawlspace_lights_pin = 11;

// LED configuration
const int num_leds_per_strip = 300;

CRGB leds_sa[num_leds_per_strip];
CRGB leds_sb[num_leds_per_strip];
CRGB leds_sc[num_leds_per_strip];
CRGB leds_sd[num_leds_per_strip];
CRGB leds_se[num_leds_per_strip];
CRGB leds_sf[num_leds_per_strip];
CRGB leds_sg[num_leds_per_strip];
CRGB leds_sh[num_leds_per_strip];
CRGB leds_g1[num_leds_per_strip];
CRGB leds_g2[num_leds_per_strip];
CRGB leds_g3[num_leds_per_strip];

// ============================================================================
// DEVICE REGISTRY (SINGLE SOURCE OF TRUTH!) — Updated to canonical device IDs
// ============================================================================

// Study Lights — analog dimmer commands
const char *study_lights_commands[] = {
    naming::CMD_STUDY_SET_BRIGHTNESS};

// Boiler Room Lights — analog dimmer commands
const char *boiler_lights_commands[] = {
    naming::CMD_BOILER_SET_BRIGHTNESS};

// Lab Lights Squares — FastLED ceiling strips commands
const char *lab_lights_squares_commands[] = {
    naming::CMD_LAB_SET_SQUARES_BRIGHTNESS,
    naming::CMD_LAB_SET_SQUARES_COLOR};

// Lab Lights Grates — FastLED floor grate commands
const char *lab_lights_grates_commands[] = {
    naming::CMD_LAB_SET_GRATES_BRIGHTNESS,
    naming::CMD_LAB_SET_GRATES_COLOR};

// Sconces — digital relay commands
const char *sconces_commands[] = {
    naming::CMD_SCONCES_ON,
    naming::CMD_SCONCES_OFF};

// Crawlspace Lights — digital relay commands
const char *crawlspace_lights_commands[] = {
    naming::CMD_CRAWLSPACE_ON,
    naming::CMD_CRAWLSPACE_OFF};

// Create device definitions with friendly names
SentientDeviceDef dev_study_lights(
    naming::DEV_STUDY_LIGHTS, naming::FRIENDLY_STUDY_LIGHTS, "dimmer",
    study_lights_commands, 1);

SentientDeviceDef dev_boiler_lights(
    naming::DEV_BOILER_LIGHTS, naming::FRIENDLY_BOILER_LIGHTS, "dimmer",
    boiler_lights_commands, 1);

SentientDeviceDef dev_lab_lights_squares(
    naming::DEV_LAB_LIGHTS_SQUARES, naming::FRIENDLY_LAB_LIGHTS_SQUARES, "led_strip",
    lab_lights_squares_commands, 2);

SentientDeviceDef dev_lab_lights_grates(
    naming::DEV_LAB_LIGHTS_GRATES, naming::FRIENDLY_LAB_LIGHTS_GRATES, "led_strip",
    lab_lights_grates_commands, 2);

SentientDeviceDef dev_sconces(
    naming::DEV_SCONCES, naming::FRIENDLY_SCONCES, "relay",
    sconces_commands, 2);

SentientDeviceDef dev_crawlspace_lights(
    naming::DEV_CRAWLSPACE_LIGHTS, naming::FRIENDLY_CRAWLSPACE_LIGHTS, "relay",
    crawlspace_lights_commands, 2);

// Create the device registry (manifest builder will use these IDs and names)
SentientDeviceRegistry deviceRegistry;

// ──────────────────────────────────────────────────────────────────────────────
// Configuration Constants
// ──────────────────────────────────────────────────────────────────────────────
const unsigned long heartbeat_interval_ms = 5000;
// JSON capacity for metadata/status publishes
static const size_t metadata_json_capacity = 1024;

// ============================================================================
// MQTT CONFIGURATION
// ============================================================================
const IPAddress mqtt_broker_ip(192, 168, 2, 3);
const char *mqtt_host = "mqtt.sentientengine.ai";
const int mqtt_port = 1883;
const char *mqtt_user = "paragon_devices";
const char *mqtt_password = "wF9Wwejkjdml3EA599e1fTOb9xyAixaduEMID7UfDDs=";
const char *mqtt_namespace = naming::CLIENT_ID;
const char *room_id = naming::ROOM_ID;
// Single source of truth: controller_id comes from firmware::UNIQUE_ID
const char *controller_id = naming::CONTROLLER_ID;
const char *controller_model = "controller"; // pseudo-device for generic status
const char *controller_friendly_name = naming::CONTROLLER_FRIENDLY_NAME;

// ──────────────────────────────────────────────────────────────────────────────
// Hardware State Variables (NOT game state - just hardware execution flags)
// ──────────────────────────────────────────────────────────────────────────────
int study_dimmer = 0;
int boiler_dimmer = 0;
bool lab_squares_on = false;
uint8_t lab_squares_brightness = 255;
CRGB lab_squares_color = CRGB::Yellow;
bool lab_grates_on = false;
uint8_t lab_grates_brightness = 255;
CRGB lab_grates_color = CRGB::Blue;
bool sconces_on = false;
bool crawlspace_lights_on = false;

// ──────────────────────────────────────────────────────────────────────────────
// Forward Declarations (required for MQTT initialization)
// ──────────────────────────────────────────────────────────────────────────────
void build_capability_manifest();
SentientMQTTConfig build_mqtt_config();
bool build_heartbeat_payload(JsonDocument &doc, void *ctx);

// ──────────────────────────────────────────────────────────────────────────────
// MQTT Objects
// ──────────────────────────────────────────────────────────────────────────────
SentientCapabilityManifest manifest;
SentientMQTT mqtt(build_mqtt_config());

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 2: SETUP FUNCTION
// ══════════════════════════════════════════════════════════════════════════════

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println(F(""));
  Serial.println(F("╔════════════════════════════════════════════════════════════╗"));
  Serial.println(F("║       Sentient Engine - Main Lighting Controller v2   ║"));
  Serial.println(F("╚════════════════════════════════════════════════════════════╝"));
  Serial.print(F("[MainLighting] Firmware Version: "));
  Serial.println(firmware::VERSION);
  Serial.print(F("[MainLighting] Unique ID: "));
  Serial.println(firmware::UNIQUE_ID);

  // Configure power LED
  pinMode(power_led_pin, OUTPUT);
  digitalWrite(power_led_pin, HIGH);

  // Configure analog dimmers
  pinMode(study_lights_pin, OUTPUT);
  pinMode(boiler_lights_pin, OUTPUT);
  analogWrite(study_lights_pin, 0);
  analogWrite(boiler_lights_pin, 0);

  // Configure digital relays
  pinMode(sconces_pin, OUTPUT);
  pinMode(crawlspace_lights_pin, OUTPUT);
  digitalWrite(sconces_pin, LOW);
  digitalWrite(crawlspace_lights_pin, LOW);

  // Configure LED strips
  FastLED.addLeds<WS2812B, ceiling_square_a_pin, GRB>(leds_sa, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_b_pin, GRB>(leds_sb, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_c_pin, GRB>(leds_sc, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_d_pin, GRB>(leds_sd, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_e_pin, GRB>(leds_se, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_f_pin, GRB>(leds_sf, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_g_pin, GRB>(leds_sg, num_leds_per_strip);
  FastLED.addLeds<WS2812B, ceiling_square_h_pin, GRB>(leds_sh, num_leds_per_strip);
  FastLED.addLeds<WS2812B, grate_1_pin, GRB>(leds_g1, num_leds_per_strip);
  FastLED.addLeds<WS2812B, grate_2_pin, GRB>(leds_g2, num_leds_per_strip);
  FastLED.addLeds<WS2812B, grate_3_pin, GRB>(leds_g3, num_leds_per_strip);

  FastLED.setBrightness(255);
  fill_solid(leds_sa, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_sb, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_sc, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_sd, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_se, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_sf, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_sg, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_sh, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_g1, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_g2, num_leds_per_strip, CRGB::Black);
  fill_solid(leds_g3, num_leds_per_strip, CRGB::Black);
  FastLED.show();

  Serial.println(F("[MainLighting] Hardware initialized"));

  // Register all devices (SINGLE SOURCE OF TRUTH!) — canonical IDs + friendly names
  Serial.println(F("[MainLighting] Registering devices (canonical)..."));
  deviceRegistry.addDevice(&dev_study_lights);
  deviceRegistry.addDevice(&dev_boiler_lights);
  deviceRegistry.addDevice(&dev_lab_lights_squares);
  deviceRegistry.addDevice(&dev_lab_lights_grates);
  deviceRegistry.addDevice(&dev_sconces);
  deviceRegistry.addDevice(&dev_crawlspace_lights);
  deviceRegistry.printSummary();

  // Build capability manifest
  build_capability_manifest();

  // Initialize MQTT
  Serial.println(F("[MainLighting] Initializing MQTT..."));
  if (!mqtt.begin())
  {
    Serial.println(F("[MainLighting] ERROR: MQTT initialization failed"));
    return;
  }

  Serial.println(F("[MainLighting] MQTT connected successfully!"));

  // Set callbacks
  // We override command routing to parse device + command from topic
  mqtt.setHeartbeatBuilder(build_heartbeat_payload);

  // Wait for broker connection (max 5 seconds)
  Serial.println(F("[MainLighting] Waiting for broker connection..."));
  unsigned long connection_start = millis();
  while (!mqtt.isConnected() && (millis() - connection_start < 5000))
  {
    mqtt.loop();
    delay(100);
  }

  if (mqtt.isConnected())
  {
    Serial.println(F("[MainLighting] Broker connected!"));

    // Register with Sentient system
    Serial.println(F("[MainLighting] Registering with Sentient system..."));
    if (manifest.publish_registration(mqtt.get_client(), room_id, controller_id))
    {
      Serial.println(F("[MainLighting] Registration successful!"));
    }
    else
    {
      Serial.println(F("[MainLighting] WARNING: Registration publish failed"));
    }

    // Publish initial status
    publish_hardware_status();
  }
  else
  {
    Serial.println(F("[MainLighting] Broker connection timeout - will retry in main loop"));
  }

  // Subscribe to device-scoped commands and set our own message handler
  {
    String topic = String(mqtt_namespace) + "/" + room_id + "/" + naming::CAT_COMMANDS + "/" + controller_id + "/+/+";
    mqtt.get_client().subscribe(topic.c_str());
    Serial.print(F("[MainLighting] Subscribed (device+command): "));
    Serial.println(topic);
    mqtt.get_client().setCallback([](char *topic, uint8_t *payload, unsigned int length)
                                  {
      // Parse topic → device + command
      String device, command;
      // Extract segments (expect: client/room/commands/controller/device/command)
      int seg = 0; const char *p = topic;
      const char *segStart[8] = {nullptr}; size_t segLen[8] = {0};
      segStart[0] = p;
      while (*p && seg < 8) { if (*p == '/') { segLen[seg] = p - segStart[seg]; seg++; if (seg < 8) segStart[seg] = p + 1; } p++; }
      if (seg < 8 && segStart[seg]) { segLen[seg] = p - segStart[seg]; seg++; }
      auto equalsSeg = [&](int idx, const char *lit) -> bool {
        size_t litLen = strlen(lit); return segLen[idx] == litLen && strncmp(segStart[idx], lit, litLen) == 0; };
      if (seg >= 6 && equalsSeg(0, mqtt_namespace) && equalsSeg(1, room_id) && equalsSeg(2, naming::CAT_COMMANDS) && equalsSeg(3, controller_id)) {
        device = String(segStart[4]).substring(0, segLen[4]);
        command = String(segStart[5]).substring(0, segLen[5]);
        // Serial observability for command receipt
        Serial.print(F("[MainLighting] CMD recv: device="));
        Serial.print(device);
        Serial.print(F(" command="));
        Serial.print(command);
        Serial.print(F(" payload_len="));
        Serial.println(length);
      } else {
        return; // Not for us
      }

      // Parse payload (JSON or raw)
      DynamicJsonDocument doc(256);
      bool hasJson = false;
      if (length > 0) {
        DeserializationError err = deserializeJson(doc, payload, length);
        hasJson = !err;
        if (err) {
          // Note: payload not valid JSON, treat as raw string
          Serial.println(F("[MainLighting] Payload parse: non-JSON, treating as raw"));
          doc.clear(); doc["value"] = String((const char *)payload).substring(0, length);
        }
      }

      long ack_duration_ms = -1; // will be included in ACK if >= 0

      // Dispatch commands by device
      // Study Lights
      if (device == naming::DEV_STUDY_LIGHTS) {
        if (command == naming::CMD_STUDY_SET_BRIGHTNESS) {
          int brightness = 255; // default full brightness
          if (hasJson && !doc["brightness"].isNull()) {
            brightness = doc["brightness"].as<int>();
          } else if (hasJson && !doc["value"].isNull()) {
            brightness = doc["value"].as<int>();
          }
          brightness = constrain(brightness, 0, 255);
          study_dimmer = brightness;
          analogWrite(study_lights_pin, study_dimmer);
          Serial.print(F("[MainLighting] Study lights set to: "));
          Serial.println(study_dimmer);
          publish_hardware_status();
        }
      }
      // Boiler Room Lights
      else if (device == naming::DEV_BOILER_LIGHTS) {
        if (command == naming::CMD_BOILER_SET_BRIGHTNESS) {
          int brightness = 255; // default full brightness
          if (hasJson && !doc["brightness"].isNull()) {
            brightness = doc["brightness"].as<int>();
          } else if (hasJson && !doc["value"].isNull()) {
            brightness = doc["value"].as<int>();
          }
          brightness = constrain(brightness, 0, 255);
          boiler_dimmer = brightness;
          analogWrite(boiler_lights_pin, boiler_dimmer);
          Serial.print(F("[MainLighting] Boiler lights set to: "));
          Serial.println(boiler_dimmer);
          publish_hardware_status();
        }
      }
      // Lab Lights Squares (Ceiling FastLED)
      else if (device == naming::DEV_LAB_LIGHTS_SQUARES) {
        if (command == naming::CMD_LAB_SET_SQUARES_BRIGHTNESS) {
          int brightness = 255; // default full brightness
          if (hasJson && !doc["brightness"].isNull()) {
            brightness = doc["brightness"].as<int>();
          } else if (hasJson && !doc["value"].isNull()) {
            brightness = doc["value"].as<int>();
          }
          brightness = constrain(brightness, 0, 255);
          lab_squares_brightness = brightness;
          
          if (brightness == 0) {
            // Turn off ceiling squares
            fill_solid(leds_sa, num_leds_per_strip, CRGB::Black);
            fill_solid(leds_sb, num_leds_per_strip, CRGB::Black);
            fill_solid(leds_sc, num_leds_per_strip, CRGB::Black);
            fill_solid(leds_sd, num_leds_per_strip, CRGB::Black);
            fill_solid(leds_se, num_leds_per_strip, CRGB::Black);
            fill_solid(leds_sf, num_leds_per_strip, CRGB::Black);
            fill_solid(leds_sg, num_leds_per_strip, CRGB::Black);
            fill_solid(leds_sh, num_leds_per_strip, CRGB::Black);
            lab_squares_on = false;
          } else {
            // Set brightness and turn on with current color
            FastLED.setBrightness(lab_squares_brightness);
            fill_solid(leds_sa, num_leds_per_strip, lab_squares_color);
            fill_solid(leds_sb, num_leds_per_strip, lab_squares_color);
            fill_solid(leds_sc, num_leds_per_strip, lab_squares_color);
            fill_solid(leds_sd, num_leds_per_strip, lab_squares_color);
            fill_solid(leds_se, num_leds_per_strip, lab_squares_color);
            fill_solid(leds_sf, num_leds_per_strip, lab_squares_color);
            fill_solid(leds_sg, num_leds_per_strip, lab_squares_color);
            fill_solid(leds_sh, num_leds_per_strip, lab_squares_color);
            lab_squares_on = true;
          }
          FastLED.show();
          Serial.print(F("[MainLighting] Lab squares brightness: "));
          Serial.println(lab_squares_brightness);
          publish_hardware_status();
        }
        else if (command == naming::CMD_LAB_SET_SQUARES_COLOR) {
          // Parse color from JSON payload
          if (hasJson && !doc["color"].isNull()) {
            String colorStr = doc["color"].as<String>();
            colorStr.toLowerCase();
            
            if (colorStr == "yellow") lab_squares_color = CRGB::Yellow;
            else if (colorStr == "red") lab_squares_color = CRGB::Red;
            else if (colorStr == "green") lab_squares_color = CRGB::Green;
            else if (colorStr == "blue") lab_squares_color = CRGB::Blue;
            else if (colorStr == "white") lab_squares_color = CRGB::White;
            else if (colorStr == "purple") lab_squares_color = CRGB::Purple;
            else if (colorStr == "orange") lab_squares_color = CRGB::Orange;
            else lab_squares_color = CRGB::Yellow; // default
            
            // Apply color to ceiling squares if lights are on
            if (lab_squares_on) {
              fill_solid(leds_sa, num_leds_per_strip, lab_squares_color);
              fill_solid(leds_sb, num_leds_per_strip, lab_squares_color);
              fill_solid(leds_sc, num_leds_per_strip, lab_squares_color);
              fill_solid(leds_sd, num_leds_per_strip, lab_squares_color);
              fill_solid(leds_se, num_leds_per_strip, lab_squares_color);
              fill_solid(leds_sf, num_leds_per_strip, lab_squares_color);
              fill_solid(leds_sg, num_leds_per_strip, lab_squares_color);
              fill_solid(leds_sh, num_leds_per_strip, lab_squares_color);
              FastLED.show();
            }
            
            Serial.print(F("[MainLighting] Lab squares color: "));
            Serial.println(colorStr);
            publish_hardware_status();
          }
        }
      }
      // Lab Lights Grates (Floor FastLED)
      else if (device == naming::DEV_LAB_LIGHTS_GRATES) {
        if (command == naming::CMD_LAB_SET_GRATES_BRIGHTNESS) {
          int brightness = 255; // default full brightness
          if (hasJson && !doc["brightness"].isNull()) {
            brightness = doc["brightness"].as<int>();
          } else if (hasJson && !doc["value"].isNull()) {
            brightness = doc["value"].as<int>();
          }
          brightness = constrain(brightness, 0, 255);
          lab_grates_brightness = brightness;
          
          if (brightness == 0) {
            // Turn off floor grates
            fill_solid(leds_g1, num_leds_per_strip, CRGB::Black);
            fill_solid(leds_g2, num_leds_per_strip, CRGB::Black);
            fill_solid(leds_g3, num_leds_per_strip, CRGB::Black);
            lab_grates_on = false;
          } else {
            // Set brightness and turn on with current color
            FastLED.setBrightness(lab_grates_brightness);
            fill_solid(leds_g1, num_leds_per_strip, lab_grates_color);
            fill_solid(leds_g2, num_leds_per_strip, lab_grates_color);
            fill_solid(leds_g3, num_leds_per_strip, lab_grates_color);
            lab_grates_on = true;
          }
          FastLED.show();
          Serial.print(F("[MainLighting] Lab grates brightness: "));
          Serial.println(lab_grates_brightness);
          publish_hardware_status();
        }
        else if (command == naming::CMD_LAB_SET_GRATES_COLOR) {
          // Parse color from JSON payload
          if (hasJson && !doc["color"].isNull()) {
            String colorStr = doc["color"].as<String>();
            colorStr.toLowerCase();
            
            if (colorStr == "yellow") lab_grates_color = CRGB::Yellow;
            else if (colorStr == "red") lab_grates_color = CRGB::Red;
            else if (colorStr == "green") lab_grates_color = CRGB::Green;
            else if (colorStr == "blue") lab_grates_color = CRGB::Blue;
            else if (colorStr == "white") lab_grates_color = CRGB::White;
            else if (colorStr == "purple") lab_grates_color = CRGB::Purple;
            else if (colorStr == "orange") lab_grates_color = CRGB::Orange;
            else lab_grates_color = CRGB::Blue; // default
            
            // Apply color to floor grates if lights are on
            if (lab_grates_on) {
              fill_solid(leds_g1, num_leds_per_strip, lab_grates_color);
              fill_solid(leds_g2, num_leds_per_strip, lab_grates_color);
              fill_solid(leds_g3, num_leds_per_strip, lab_grates_color);
              FastLED.show();
            }
            
            Serial.print(F("[MainLighting] Lab grates color: "));
            Serial.println(colorStr);
            publish_hardware_status();
          }
        }
      }
      // Sconces
      else if (device == naming::DEV_SCONCES) {
        if (command == naming::CMD_SCONCES_ON) {
          digitalWrite(sconces_pin, HIGH);
          sconces_on = true;
          Serial.println(F("[MainLighting] Sconces: ON"));
          publish_hardware_status();
        }
        else if (command == naming::CMD_SCONCES_OFF) {
          digitalWrite(sconces_pin, LOW);
          sconces_on = false;
          Serial.println(F("[MainLighting] Sconces: OFF"));
          publish_hardware_status();
        }
      }
      // Crawlspace Lights
      else if (device == naming::DEV_CRAWLSPACE_LIGHTS) {
        if (command == naming::CMD_CRAWLSPACE_ON) {
          digitalWrite(crawlspace_lights_pin, HIGH);
          crawlspace_lights_on = true;
          Serial.println(F("[MainLighting] Crawlspace lights: ON"));
          publish_hardware_status();
        }
        else if (command == naming::CMD_CRAWLSPACE_OFF) {
          digitalWrite(crawlspace_lights_pin, LOW);
          crawlspace_lights_on = false;
          Serial.println(F("[MainLighting] Crawlspace lights: OFF"));
          publish_hardware_status();
        }
      }

      // Publish command acknowledgement to dedicated acknowledgement topic
      StaticJsonDocument<160> ack;
      ack["controller_id"] = controller_id;
      ack["device_id"] = device;
      ack["command"] = command;
      ack["success"] = true;
      ack["timestamp_ms"] = millis();
      if (ack_duration_ms >= 0) { ack["duration_ms"] = (long)ack_duration_ms; }
      char buf[196]; serializeJson(ack, buf, sizeof(buf));
      // Topic: <tenant>/<room>/acknowledgement/<controller>/<device>/<command>
      String ackTopic = String(mqtt_namespace) + "/" + room_id + "/" + naming::CAT_ACKNOWLEDGEMENT + "/" + controller_id + "/" + device + "/" + command;
      mqtt.get_client().publish(ackTopic.c_str(), buf, false);
      Serial.print(F("[MainLighting] ACK -> "));
      Serial.println(ackTopic); });
  }

  Serial.println(F("[MainLighting] Ready - awaiting Sentient commands"));
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 3: LOOP FUNCTION
// ══════════════════════════════════════════════════════════════════════════════

void loop()
{
  // 1. LISTEN for commands from Sentient
  mqtt.loop();

  // 2. EXECUTE LED updates (non-blocking)
  static unsigned long last_led_update = 0;
  if (millis() - last_led_update >= 20) // Update LEDs every 20ms
  {
    FastLED.show();
    last_led_update = millis();
  }

  // 3. Service MQTT again to ensure heartbeats aren't blocked
  mqtt.loop();

  delay(5);
}

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 4: (Legacy handler removed) — All commands are routed via topic-based
//            device+command parsing inside the MQTT callback set in setup().
// ══════════════════════════════════════════════════════════════════════════════

// ══════════════════════════════════════════════════════════════════════════════
// SECTION 5: ALL OTHER FUNCTIONS
// ══════════════════════════════════════════════════════════════════════════════

// ──────────────────────────────────────────────────────────────────────────────
// Capability Manifest Builder
// ──────────────────────────────────────────────────────────────────────────────

void build_capability_manifest()
{
  manifest.set_controller_info(
      firmware::UNIQUE_ID,
      controller_friendly_name,
      firmware::VERSION,
      room_id,
      controller_id);

  // Auto-generate entire manifest from device registry - ONE LINE!
  deviceRegistry.buildManifest(manifest);

  // That's it! No manual device/topic registration needed!
  // All devices and topics are defined once in the Device Registry section above.
}

// ──────────────────────────────────────────────────────────────────────────────
// MQTT Configuration Builder
// ──────────────────────────────────────────────────────────────────────────────

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
  cfg.namespaceId = mqtt_namespace;
  cfg.roomId = room_id;
  cfg.controllerId = controller_id;
  cfg.deviceId = nullptr; // Controller-level messages don't have deviceId
  cfg.displayName = controller_friendly_name;
  cfg.useDhcp = true;
  cfg.heartbeatIntervalMs = heartbeat_interval_ms;
  cfg.publishJsonCapacity = metadata_json_capacity;
  cfg.autoHeartbeat = true;
  return cfg;
}

// ──────────────────────────────────────────────────────────────────────────────
// Heartbeat Builder
// ──────────────────────────────────────────────────────────────────────────────

bool build_heartbeat_payload(JsonDocument &doc, void *ctx)
{
  doc["uid"] = firmware::UNIQUE_ID;
  doc["fw"] = firmware::VERSION;
  doc["up"] = millis();
  doc["study"] = study_dimmer;
  doc["boiler"] = boiler_dimmer;
  doc["lab_squares"] = lab_squares_on ? 1 : 0;
  doc["lab_grates"] = lab_grates_on ? 1 : 0;
  doc["sconces"] = sconces_on ? 1 : 0;
  doc["crawlspace"] = crawlspace_lights_on ? 1 : 0;
  return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Status Publishing
// ──────────────────────────────────────────────────────────────────────────────

void publish_hardware_status()
{
  JsonDocument doc;
  doc["uid"] = firmware::UNIQUE_ID;
  doc["study_dimmer"] = study_dimmer;
  doc["boiler_dimmer"] = boiler_dimmer;
  doc["lab_squares_on"] = lab_squares_on;
  doc["lab_squares_brightness"] = lab_squares_brightness;
  doc["lab_grates_on"] = lab_grates_on;
  doc["lab_grates_brightness"] = lab_grates_brightness;
  doc["sconces"] = sconces_on;
  doc["crawlspace"] = crawlspace_lights_on;
  doc["ts"] = millis();

  mqtt.publishJson("status", "hardware", doc);
}