// Vault Puzzle Controller — v8 (Teensy 4.1)
//
// Permanent v8 (no legacy bridge):
// - Option 2 device identity: one v8 device_id per logical sub-device.
// - One MQTT connection per device_id (required for correct LWT OFFLINE semantics).
// - Commands: action="SET" + parameters.op (string). (Sensor-only device.)
//
// Devices (room-unique v8 device_ids):
// - vault_rfid_reader

#include <Arduino.h>
#include <ArduinoJson.h>

#include <SoftwareSerial.h>
#include <SerialRFID.h>

#include <SentientV8.h>

// --- Per-room config (do not commit secrets) ---
#define ROOM_ID "room1"

#define MQTT_BROKER_HOST "mqtt." ROOM_ID ".sentientengine.ai"
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "sentient";
static const char *MQTT_PASSWORD = "CHANGE_ME";

// 32-byte HMAC key, hex encoded (64 chars).
static const char *HMAC_RFID_READER = "0000000000000000000000000000000000000000000000000000000000000000";

// Pins
static const int PIN_POWER_LED = 13;
static const int PIN_RX = 15;
static const int PIN_TX = 14;
static const int PIN_TIR = 19; // Tag In Range sensor

// Tag size for RFID
#define SIZE_TAG_ID 13

// 36 vault tags mapped to vault numbers 1-36
static const char TAG_LIST[][SIZE_TAG_ID] = {
    "0C007DAE1DC2", // Vault 1
    "3C0088C9CFB2", // Vault 2
    "3C008923A630", // Vault 3
    "0C007E25A9FE", // Vault 4
    "0A005A9CF438", // Vault 5
    "3C00D64911B2", // Vault 6
    "3C00D58E96F1", // Vault 7
    "3C00D633459C", // Vault 8
    "3C00D5EDF6F2", // Vault 9
    "3C00D5C16C44", // Vault 10
    "3C00D63C61B7", // Vault 11
    "3C00D5B2F4AF", // Vault 12
    "3C00892935A9", // Vault 13
    "3C00891AB11E", // Vault 14
    "3C0088EA237D", // Vault 15
    "0C007DCE47F8", // Vault 16
    "3C0088A0DFCB", // Vault 17
    "3C00D5E96666", // Vault 18
    "3C008900EB5E", // Vault 19
    "3C00D6359C43", // Vault 20
    "0C007D1B7D17", // Vault 21
    "0C007E107E1C", // Vault 22
    "3C0088804470", // Vault 23
    "3C0088E695C7", // Vault 24
    "3C00D5E3FEF4", // Vault 25
    "0C007DF174F4", // Vault 26
    "0C007DF2FF7C", // Vault 27
    "0C007DEAE873", // Vault 28
    "3C0089199539", // Vault 29
    "0C007DC9BE06", // Vault 30
    "3C0088BFDAD1", // Vault 31
    "3C00D5CCEBCE", // Vault 32
    "0C007D5CDFF2", // Vault 33
    "3C0089198F23", // Vault 34
    "3C00892541D1", // Vault 35
    "0C007DD9832B"  // Vault 36
};

static sentient_v8::Config make_cfg(const char *device_id, const char *hmac_key_hex) {
  sentient_v8::Config c;
  c.brokerIp = IPAddress(0, 0, 0, 0);
  c.brokerHost = MQTT_BROKER_HOST;
  c.brokerPort = MQTT_PORT;
  c.username = MQTT_USERNAME;
  c.password = MQTT_PASSWORD;
  c.roomId = ROOM_ID;
  c.deviceId = device_id;
  c.deviceHmacKeyHex = hmac_key_hex;
  c.keepAliveSeconds = 10;
  c.reconnectDelayMs = 1000;
  c.heartbeatIntervalMs = 2000;
  c.rxJsonCapacity = 2048;
  c.txJsonCapacity = 2048;
  return c;
}

static sentient_v8::Client sne_rfid_reader(make_cfg("vault_rfid_reader", HMAC_RFID_READER));

static SoftwareSerial sSerial(PIN_RX, PIN_TX);
static SerialRFID rfid(sSerial);

static char current_tag[SIZE_TAG_ID];
static char last_tag[SIZE_TAG_ID] = "";
static int current_vault_number = 0;
static int last_vault_number = 0;
static bool last_tag_in_range = false;

static void ensure_ethernet_dhcp() {
#if !defined(ESP32)
  static bool started = false;
  if (started) return;
  byte mac[6];
  teensyMAC(mac);
  Ethernet.begin(mac);
  delay(250);
  started = true;
#endif
}

static int lookup_vault_number(const char *tag) {
  for (int i = 0; i < 36; i++) {
    if (strcmp(tag, TAG_LIST[i]) == 0) return i + 1;
  }
  return 0;
}

static void publish_state(const char *reason, bool tag_in_range) {
  StaticJsonDocument<256> st;
  st["vault_number"] = current_vault_number;
  st["tag_id"] = (current_vault_number == 0 ? "EMPTY" : current_tag);
  st["tag_in_range"] = tag_in_range;
  st["reason"] = reason ? reason : "";
  sne_rfid_reader.publishState(st);
}

static bool handleCommand(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void * /*vctx*/) {
  const char *action = cmd["action"] | "";
  if (strcmp(action, "SET") != 0) {
    rejectedAckReason["reason_code"] = "UNSUPPORTED_ACTION";
    return false;
  }

  JsonVariantConst p = cmd["parameters"];
  const char *op = p["op"] | "";
  if (!op || !op[0]) {
    rejectedAckReason["reason_code"] = "INVALID_PARAMS";
    return false;
  }

  if (strcmp(op, "request_status") == 0) {
    bool tag_in_range = (digitalRead(PIN_TIR) == HIGH);
    publish_state("request_status", tag_in_range);
    return true;
  }
  if (strcmp(op, "set_power_led") == 0) {
    if (!p.containsKey("on")) {
      rejectedAckReason["reason_code"] = "INVALID_PARAMS";
      return false;
    }
    bool on = p["on"] | false;
    digitalWrite(PIN_POWER_LED, on ? HIGH : LOW);
    return true;
  }
  if (strcmp(op, "noop") == 0) return true;

  rejectedAckReason["reason_code"] = "INVALID_PARAMS";
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  ensure_ethernet_dhcp();

  pinMode(PIN_POWER_LED, OUTPUT);
  digitalWrite(PIN_POWER_LED, HIGH);

  pinMode(PIN_TIR, INPUT);
  sSerial.begin(9600);

  if (!sne_rfid_reader.begin()) while (true) delay(1000);
  sne_rfid_reader.setCommandHandler(handleCommand, nullptr);

  bool tag_in_range = (digitalRead(PIN_TIR) == HIGH);
  publish_state("boot", tag_in_range);
}

void loop() {
  static unsigned long last_publish = 0;
  sne_rfid_reader.loop();

  const bool tag_in_range = (digitalRead(PIN_TIR) == HIGH);

  // Read tag (if present on the serial stream).
  if (rfid.readTag(current_tag, sizeof(current_tag))) {
    current_vault_number = lookup_vault_number(current_tag);
  }

  bool changed = false;
  if (current_vault_number != last_vault_number) changed = true;
  if (tag_in_range != last_tag_in_range) changed = true;
  if (current_vault_number != 0 && strncmp(last_tag, current_tag, SIZE_TAG_ID) != 0) changed = true;

  const unsigned long now = millis();
  if (changed || (now - last_publish) > 60UL * 1000UL) {
    last_publish = now;
    last_vault_number = current_vault_number;
    last_tag_in_range = tag_in_range;
    if (current_vault_number != 0) strncpy(last_tag, current_tag, SIZE_TAG_ID);
    publish_state(changed ? "change" : "periodic", tag_in_range);
  }

  // Clear vault number if tag removed.
  if (!tag_in_range && current_vault_number != 0) {
    current_vault_number = 0;
    last_vault_number = 0;
    last_tag_in_range = tag_in_range;
    strncpy(last_tag, "", SIZE_TAG_ID);
    publish_state("tag_removed", false);
  }
}
