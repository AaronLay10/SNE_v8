#ifndef SENTIENT_V8_H
#define SENTIENT_V8_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MQTT.h>

#if defined(ESP32)
#include <WiFi.h>
#define SENTIENT_V8_NETWORK_CLIENT WiFiClient
#else
#include <NativeEthernet.h>
#define SENTIENT_V8_NETWORK_CLIENT EthernetClient
#endif

#include <SentientCrypto.h>

namespace sentient_v8 {

enum class AckStatus { Accepted, Rejected, Completed };

struct Config {
  IPAddress brokerIp;
  const char *brokerHost = nullptr;
  uint16_t brokerPort = 1883;
  const char *username = nullptr;
  const char *password = nullptr;

  const char *roomId = nullptr;
  const char *deviceId = nullptr;

  const char *deviceHmacKeyHex = nullptr;

  uint16_t keepAliveSeconds = 10;
  uint32_t reconnectDelayMs = 1000;
  uint32_t heartbeatIntervalMs = 1000;

  size_t rxJsonCapacity = 2048;
  size_t txJsonCapacity = 2048;
};

using CommandHandler = bool (*)(const JsonDocument &cmd, JsonDocument &rejectedAckReason, void *ctx);

class Client {
public:
  explicit Client(const Config &cfg);

  bool begin();
  void loop();

  void setCommandHandler(CommandHandler handler, void *ctx = nullptr);

  bool isConnected() const { return _mqtt.connected(); }

  bool publishPresenceOnline();
  bool publishHeartbeat(const char *firmwareVersion, const char *safetyStateKind = "SAFE");
  bool publishState(const JsonDocument &state);
  bool publishTelemetry(const JsonDocument &telemetry);

  bool publishAckAccepted(const JsonDocument &cmd);
  bool publishAckRejected(const JsonDocument &cmd, const char *reasonCode);
  bool publishAckCompleted(const JsonDocument &cmd);

private:
  void ensureConnected();
  void handleIncoming(char topic[], char bytes[], int length);
  bool checkDuplicateCommandId(const char *commandId, AckStatus &outStatus, const char *&outReasonCode) const;
  void rememberCommandId(const char *commandId, AckStatus status, const char *reasonCode);
  bool verifyCommandAuth(const JsonDocument &cmd);
  bool buildSigningString(const JsonDocument &cmd, String &out);
  bool canonicalParametersJson(const JsonVariantConst &parameters, String &out);
  bool canonicalizeValue(const JsonVariantConst &v, String &out, uint8_t depth);

  String topicCmd() const;
  String topicAck() const;
  String topicHeartbeat() const;
  String topicPresence() const;
  String topicState() const;
  String topicTelemetry() const;

  String clientId() const;

  SENTIENT_V8_NETWORK_CLIENT _net;
  MQTTClient _mqtt;
  Config _cfg;
  unsigned long _lastConnectAttempt = 0;
  unsigned long _lastHeartbeat = 0;

  CommandHandler _handler = nullptr;
  void *_handlerCtx = nullptr;

  uint8_t _hmacKey[32];
  bool _hasKey = false;

  static const uint8_t kIdempotencyEntries = 16;
  static const uint8_t kCommandIdMax = 40;    // UUID string (36) + slack + NUL
  static const uint8_t kReasonCodeMax = 32;   // "INVALID_PARAMS", etc.
  char _idemCommandId[kIdempotencyEntries][kCommandIdMax];
  AckStatus _idemStatus[kIdempotencyEntries];
  char _idemReason[kIdempotencyEntries][kReasonCodeMax];
  uint8_t _idemNext = 0;

  static void mqttThunk(MQTTClient *client, char topic[], char bytes[], int length);
};

} // namespace sentient_v8

#endif
