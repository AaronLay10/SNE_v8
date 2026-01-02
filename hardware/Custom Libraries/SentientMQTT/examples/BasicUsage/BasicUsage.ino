#include <MythraOS_MQTT.h>

MythraOSMQTTConfig makeConfig() {
  MythraOSMQTTConfig cfg;
  cfg.brokerHost = "192.168.20.3";
  cfg.brokerPort = 1883;
  cfg.namespaceId = "paragon";
  cfg.roomId = "Clockwork";
  cfg.puzzleId = "PilotLight";
  cfg.deviceId = "PilotLight";
  cfg.heartbeatIntervalMs = 5000;
  return cfg;
}

MythraOSMQTT mqtt(makeConfig());

bool buildHeartbeat(JsonDocument &doc, void * /*ctx*/) {
  doc["uptimeMs"] = millis();
  doc["firmware"] = "example";
  return true;
}

void handleCommand(const char *command, const JsonDocument &payload, void * /*ctx*/) {
  Serial.print("[Command] ");
  Serial.println(command);

  if (payload.is<JsonObjectConst>()) {
    serializeJson(payload, Serial);
    Serial.println();
  }
}

void setup() {
  if (!mqtt.begin()) {
    Serial.println(F("Failed to start MQTT helper"));
    return;
  }

  mqtt.setCommandCallback(handleCommand);
  mqtt.setHeartbeatBuilder(buildHeartbeat);
}

void loop() {
  mqtt.loop();

  static unsigned long lastSensor = 0;
  if (millis() - lastSensor > 2000) {
    lastSensor = millis();
    mqtt.publishSensor("ColorTemp", 1456.0f, "K");
    mqtt.publishMetric("Lux", 234.0f, "lux");
  }
}
