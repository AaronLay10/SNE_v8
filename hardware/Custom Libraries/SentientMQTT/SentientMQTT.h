

#ifndef SENTIENT_MQTT_H
#define SENTIENT_MQTT_H

#include <Arduino.h>
#include <ArduinoJson.h>
#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 8192 // Increased for large registration messages
#endif
#ifndef MQTT_MAX_TRANSFER_SIZE
#define MQTT_MAX_TRANSFER_SIZE 512 // Chunk large messages into 512-byte writes to prevent Ethernet buffer overflow
#endif
#include <PubSubClient.h>

#if defined(ESP32)
#include <WiFi.h>
#define SENTIENT_NETWORK_CLIENT WiFiClient
#else
#include <NativeEthernet.h>
#include <TeensyID.h>
#include <fnet.h>
#define SENTIENT_NETWORK_CLIENT EthernetClient
#endif

struct SentientMQTTConfig
{
  IPAddress brokerIp;
  const char *brokerHost = nullptr;
  uint16_t brokerPort = 1883;
  const char *username = nullptr;
  const char *password = nullptr;

  const char *namespaceId = "paragon";
  const char *roomId = nullptr;
  const char *controllerId = nullptr;
  const char *deviceId = nullptr;
  const char *displayName = nullptr;
  const char *hostnamePrefix = nullptr; // Optional prefix for network hostname (e.g., "CL" for Clockwork)

  uint16_t keepAliveSeconds = 60;
  uint32_t reconnectDelayMs = 5'000;
  uint32_t heartbeatIntervalMs = 5'000;
  bool autoHeartbeat = true;

  uint16_t commandJsonCapacity = 512;
  uint16_t publishJsonCapacity = 512;

#if defined(ESP32)
  const char *wifiSsid = nullptr;
  const char *wifiPassword = nullptr;
#else
  bool useDhcp = true;
  IPAddress staticIp;
  IPAddress dns;
  IPAddress gateway;
  IPAddress subnet;
  const uint8_t *mac = nullptr;
#endif
};

using SentientCommandCallback = void (*)(const char *command, const JsonDocument &payload, void *context);
using SentientHeartbeatBuilder = bool (*)(JsonDocument &doc, void *context);
using SentientConnectionCallback = void (*)(void *context);

class SentientMQTT
{
public:
  explicit SentientMQTT(const SentientMQTTConfig &config);

  bool begin();
  void loop();

  bool publishSensor(const char *name, float value, const char *unit = nullptr);
  bool publishMetric(const char *name, float value, const char *unit = nullptr);
  bool publishState(const char *state);
  bool publishState(const char *state, const JsonDocument &extras);
  bool publishEvent(const char *eventName, const JsonDocument &payload);
  bool publishJson(const char *category, const char *item, const JsonDocument &payload, bool retain = false);
  bool publishText(const char *category, const char *item, const char *payload, bool retain = false);
  bool publishHeartbeat();
  bool publishHeartbeat(const JsonDocument &payload);

  void setCommandCallback(SentientCommandCallback callback, void *context = nullptr);
  void setHeartbeatBuilder(SentientHeartbeatBuilder callback, void *context = nullptr);
  void setOnConnect(SentientConnectionCallback callback, void *context = nullptr);
  void setOnDisconnect(SentientConnectionCallback callback, void *context = nullptr);

  bool isConnected() { return _mqttClient.connected(); }
  const SentientMQTTConfig &config() const { return _config; }
  PubSubClient &get_client() { return _mqttClient; }

private:
  bool configureNetwork();
  void ensureConnected();
  void handleIncoming(char *topic, uint8_t *payload, unsigned int length);
  bool publishRaw(const String &topic, const char *payload, bool retain);

  String buildTopic(const char *category, const char *item = nullptr) const;
  String buildClientId() const;
  String buildConnectionPayload(const char *state) const;

  SentientMQTTConfig _config;
  SENTIENT_NETWORK_CLIENT _networkClient;
  PubSubClient _mqttClient;
  unsigned long _lastConnectAttempt = 0;
  unsigned long _lastHeartbeat = 0;
  bool _wasConnected = false;

#if !defined(ESP32)
  uint8_t _macAddress[6] = {0};
  fnet_mdns_desc_t _mdnsService = nullptr;
  fnet_llmnr_desc_t _llmnrService = nullptr;
#endif

  SentientCommandCallback _commandCallback = nullptr;
  void *_commandContext = nullptr;

  SentientHeartbeatBuilder _heartbeatBuilder = nullptr;
  void *_heartbeatContext = nullptr;

  SentientConnectionCallback _onConnect = nullptr;
  void *_onConnectContext = nullptr;

  SentientConnectionCallback _onDisconnect = nullptr;
  void *_onDisconnectContext = nullptr;

  static SentientMQTT *s_activeInstance;
  static void mqttCallbackThunk(char *topic, uint8_t *payload, unsigned int length);
};

#endif // SENTIENT_MQTT_H
