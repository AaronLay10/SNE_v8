#include "SentientV8.h"

namespace sentient_v8 {

static const char *schema_v8 = "v8";

static const char *ack_status_str(AckStatus s) {
  switch (s) {
  case AckStatus::Accepted:
    return "ACCEPTED";
  case AckStatus::Rejected:
    return "REJECTED";
  case AckStatus::Completed:
    return "COMPLETED";
  }
  return "REJECTED";
}

static unsigned long secondsSinceBoot() { return millis() / 1000; }

static void cstr_copy(char *dst, size_t dstSize, const char *src) {
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; i + 1 < dstSize && src[i]; i++) dst[i] = src[i];
  dst[i] = '\0';
}

Client::Client(const Config &cfg) : _mqtt(2048, 2048), _cfg(cfg) {
  for (uint8_t i = 0; i < kIdempotencyEntries; i++) {
    _idemCommandId[i][0] = '\0';
    _idemStatus[i] = AckStatus::Rejected;
    _idemReason[i][0] = '\0';
  }
}

bool Client::begin() {
  if (!_cfg.roomId || !_cfg.deviceId) {
    return false;
  }

  _mqtt.begin(_net);
  if (_cfg.brokerIp != IPAddress(0, 0, 0, 0)) {
    _mqtt.setHost(_cfg.brokerIp, _cfg.brokerPort);
  } else if (_cfg.brokerHost && _cfg.brokerHost[0]) {
    _mqtt.setHost(_cfg.brokerHost, _cfg.brokerPort);
  } else {
    return false;
  }

  _mqtt.setOptions(_cfg.keepAliveSeconds, true, 1000);
  _mqtt.onMessageAdvanced(mqttThunk);
  _mqtt.ref = this;

  if (_cfg.deviceHmacKeyHex && _cfg.deviceHmacKeyHex[0]) {
    _hasKey = sentient_crypto::hex_to_bytes(_cfg.deviceHmacKeyHex, _hmacKey, sizeof(_hmacKey));
  }

  return true;
}

void Client::loop() {
  ensureConnected();
  _mqtt.loop();

  if (_mqtt.connected()) {
    const unsigned long now = millis();
    if (now - _lastHeartbeat >= _cfg.heartbeatIntervalMs) {
      publishHeartbeat("unknown");
      _lastHeartbeat = now;
    }
  }
}

void Client::setCommandHandler(CommandHandler handler, void *ctx) {
  _handler = handler;
  _handlerCtx = ctx;
}

String Client::clientId() const {
  String id = "sentient-v8-";
  id += _cfg.roomId;
  id += "-";
  id += _cfg.deviceId;
  return id;
}

String Client::topicCmd() const { return String("room/") + _cfg.roomId + "/device/" + _cfg.deviceId + "/cmd"; }
String Client::topicAck() const { return String("room/") + _cfg.roomId + "/device/" + _cfg.deviceId + "/ack"; }
String Client::topicHeartbeat() const { return String("room/") + _cfg.roomId + "/device/" + _cfg.deviceId + "/heartbeat"; }
String Client::topicPresence() const { return String("room/") + _cfg.roomId + "/device/" + _cfg.deviceId + "/presence"; }
String Client::topicState() const { return String("room/") + _cfg.roomId + "/device/" + _cfg.deviceId + "/state"; }
String Client::topicTelemetry() const { return String("room/") + _cfg.roomId + "/device/" + _cfg.deviceId + "/telemetry"; }

void Client::ensureConnected() {
  if (_mqtt.connected()) return;

  unsigned long now = millis();
  if (now - _lastConnectAttempt < _cfg.reconnectDelayMs) return;
  _lastConnectAttempt = now;

  String willTopic = topicPresence();
  DynamicJsonDocument willDoc(_cfg.txJsonCapacity);
  willDoc["schema"] = schema_v8;
  willDoc["room_id"] = _cfg.roomId;
  willDoc["device_id"] = _cfg.deviceId;
  willDoc["status"] = "OFFLINE";
  willDoc["observed_at_unix_ms"] = 0;
  String willPayload;
  serializeJson(willDoc, willPayload);

  _mqtt.setWill(willTopic.c_str(), willPayload.c_str(), true, 1);

  if (!_mqtt.connect(clientId().c_str(), _cfg.username, _cfg.password)) {
    return;
  }

  _mqtt.subscribe(topicCmd().c_str(), 1);
  publishPresenceOnline();
}

bool Client::publishPresenceOnline() {
  DynamicJsonDocument doc(_cfg.txJsonCapacity);
  doc["schema"] = schema_v8;
  doc["room_id"] = _cfg.roomId;
  doc["device_id"] = _cfg.deviceId;
  doc["status"] = "ONLINE";
  doc["observed_at_unix_ms"] = 0;
  String payload;
  serializeJson(doc, payload);
  return _mqtt.publish(topicPresence().c_str(), payload.c_str(), true, 1);
}

bool Client::publishHeartbeat(const char *firmwareVersion, const char *safetyStateKind) {
  DynamicJsonDocument doc(_cfg.txJsonCapacity);
  doc["schema"] = schema_v8;
  doc["room_id"] = _cfg.roomId;
  doc["device_id"] = _cfg.deviceId;
  doc["uptime_ms"] = (uint64_t)millis();
  doc["firmware_version"] = firmwareVersion ? firmwareVersion : "unknown";
  JsonObject safety = doc.createNestedObject("safety_state");
  safety["kind"] = safetyStateKind ? safetyStateKind : "SAFE";
  safety["latched"] = false;
  doc["observed_at_unix_ms"] = 0;

  String payload;
  serializeJson(doc, payload);
  return _mqtt.publish(topicHeartbeat().c_str(), payload.c_str(), false, 0);
}

bool Client::publishState(const JsonDocument &state) {
  DynamicJsonDocument doc(_cfg.txJsonCapacity);
  doc["schema"] = schema_v8;
  doc["room_id"] = _cfg.roomId;
  doc["device_id"] = _cfg.deviceId;
  JsonObject safety = doc.createNestedObject("safety_state");
  safety["kind"] = "SAFE";
  safety["latched"] = false;
  doc["state"] = state.as<JsonVariantConst>();
  doc["observed_at_unix_ms"] = 0;

  String payload;
  serializeJson(doc, payload);
  return _mqtt.publish(topicState().c_str(), payload.c_str(), true, 1);
}

bool Client::publishTelemetry(const JsonDocument &telemetry) {
  DynamicJsonDocument doc(_cfg.txJsonCapacity);
  doc["schema"] = schema_v8;
  doc["room_id"] = _cfg.roomId;
  doc["device_id"] = _cfg.deviceId;
  JsonObject safety = doc.createNestedObject("safety_state");
  safety["kind"] = "SAFE";
  safety["latched"] = false;
  doc["telemetry"] = telemetry.as<JsonVariantConst>();
  doc["observed_at_unix_ms"] = 0;

  String payload;
  serializeJson(doc, payload);
  return _mqtt.publish(topicTelemetry().c_str(), payload.c_str(), false, 0);
}

bool Client::publishAckAccepted(const JsonDocument &cmd) {
  return publishAckRejected(cmd, nullptr) ? true : false;
}

bool Client::publishAckRejected(const JsonDocument &cmd, const char *reasonCode) {
  DynamicJsonDocument doc(_cfg.txJsonCapacity);
  doc["schema"] = schema_v8;
  doc["room_id"] = _cfg.roomId;
  doc["device_id"] = _cfg.deviceId;
  doc["command_id"] = cmd["command_id"] | "";
  doc["correlation_id"] = cmd["correlation_id"] | "";
  doc["status"] = reasonCode ? ack_status_str(AckStatus::Rejected) : ack_status_str(AckStatus::Accepted);
  if (reasonCode) doc["reason_code"] = reasonCode;
  JsonObject safety = doc.createNestedObject("safety_state");
  safety["kind"] = "SAFE";
  safety["latched"] = false;
  doc["observed_at_unix_ms"] = 0;

  String payload;
  serializeJson(doc, payload);
  return _mqtt.publish(topicAck().c_str(), payload.c_str(), false, 1);
}

bool Client::publishAckCompleted(const JsonDocument &cmd) {
  DynamicJsonDocument doc(_cfg.txJsonCapacity);
  doc["schema"] = schema_v8;
  doc["room_id"] = _cfg.roomId;
  doc["device_id"] = _cfg.deviceId;
  doc["command_id"] = cmd["command_id"] | "";
  doc["correlation_id"] = cmd["correlation_id"] | "";
  doc["status"] = ack_status_str(AckStatus::Completed);
  JsonObject safety = doc.createNestedObject("safety_state");
  safety["kind"] = "SAFE";
  safety["latched"] = false;
  doc["observed_at_unix_ms"] = 0;

  String payload;
  serializeJson(doc, payload);
  return _mqtt.publish(topicAck().c_str(), payload.c_str(), false, 1);
}

void Client::mqttThunk(MQTTClient *client, char topic[], char bytes[], int length) {
  Client *self = (Client *)client->ref;
  if (!self) return;
  self->handleIncoming(topic, bytes, length);
}

void Client::handleIncoming(char topic[], char bytes[], int length) {
  if (!_handler) return;
  if (String(topic) != topicCmd()) return;

  DynamicJsonDocument cmdDoc(_cfg.rxJsonCapacity);
  DeserializationError err = deserializeJson(cmdDoc, bytes, length);
  if (err) return;

  if (String(cmdDoc["schema"] | "") != schema_v8) return;
  if (String(cmdDoc["room_id"] | "") != _cfg.roomId) return;
  if (String(cmdDoc["device_id"] | "") != _cfg.deviceId) return;

  if (!_hasKey || !verifyCommandAuth(cmdDoc)) {
    publishAckRejected(cmdDoc, "AUTH_INVALID");
    return;
  }

  const char *commandId = cmdDoc["command_id"] | "";
  if (commandId && commandId[0]) {
    AckStatus cachedStatus = AckStatus::Rejected;
    const char *cachedReason = nullptr;
    if (checkDuplicateCommandId(commandId, cachedStatus, cachedReason)) {
      if (cachedStatus == AckStatus::Rejected) {
        publishAckRejected(cmdDoc, cachedReason ? cachedReason : "REJECTED");
      } else {
        publishAckAccepted(cmdDoc);
        publishAckCompleted(cmdDoc);
      }
      return;
    }
  }

  DynamicJsonDocument rejectReason(_cfg.txJsonCapacity);
  bool ok = _handler(cmdDoc, rejectReason, _handlerCtx);
  if (!ok) {
    const char *code = rejectReason["reason_code"] | "REJECTED";
    publishAckRejected(cmdDoc, code);
    if (commandId && commandId[0]) rememberCommandId(commandId, AckStatus::Rejected, code);
    return;
  }

  publishAckAccepted(cmdDoc);
  publishAckCompleted(cmdDoc);
  if (commandId && commandId[0]) rememberCommandId(commandId, AckStatus::Completed, nullptr);
}

bool Client::checkDuplicateCommandId(const char *commandId, AckStatus &outStatus, const char *&outReasonCode) const {
  outReasonCode = nullptr;
  if (!commandId || !commandId[0]) return false;

  for (uint8_t i = 0; i < kIdempotencyEntries; i++) {
    if (_idemCommandId[i][0] == '\0') continue;
    if (strcmp(_idemCommandId[i], commandId) == 0) {
      outStatus = _idemStatus[i];
      outReasonCode = (_idemStatus[i] == AckStatus::Rejected && _idemReason[i][0]) ? _idemReason[i] : nullptr;
      return true;
    }
  }
  return false;
}

void Client::rememberCommandId(const char *commandId, AckStatus status, const char *reasonCode) {
  if (!commandId || !commandId[0]) return;

  // Overwrite existing entry if present.
  for (uint8_t i = 0; i < kIdempotencyEntries; i++) {
    if (_idemCommandId[i][0] == '\0') continue;
    if (strcmp(_idemCommandId[i], commandId) == 0) {
      _idemStatus[i] = status;
      if (status == AckStatus::Rejected) {
        cstr_copy(_idemReason[i], sizeof(_idemReason[i]), reasonCode ? reasonCode : "REJECTED");
      } else {
        _idemReason[i][0] = '\0';
      }
      return;
    }
  }

  uint8_t idx = _idemNext;
  _idemNext = (uint8_t)((_idemNext + 1) % kIdempotencyEntries);

  cstr_copy(_idemCommandId[idx], sizeof(_idemCommandId[idx]), commandId);
  _idemStatus[idx] = status;
  if (status == AckStatus::Rejected) {
    cstr_copy(_idemReason[idx], sizeof(_idemReason[idx]), reasonCode ? reasonCode : "REJECTED");
  } else {
    _idemReason[idx][0] = '\0';
  }
}

bool Client::verifyCommandAuth(const JsonDocument &cmd) {
  JsonVariantConst auth = cmd["auth"];
  const char *alg = auth["alg"] | "";
  const char *mac_hex = auth["mac_hex"] | "";
  if (String(alg) != "HMAC-SHA256") return false;

  String signing;
  if (!buildSigningString(cmd, signing)) return false;

  uint8_t mac[32];
  sentient_crypto::hmac_sha256(_hmacKey, sizeof(_hmacKey), (const uint8_t *)signing.c_str(), signing.length(), mac);
  char expected_hex[65];
  sentient_crypto::bytes_to_hex_lower(mac, sizeof(mac), expected_hex, sizeof(expected_hex));
  return sentient_crypto::constant_time_eq_hex(expected_hex, mac_hex);
}

bool Client::buildSigningString(const JsonDocument &cmd, String &out) {
  String params;
  if (!canonicalParametersJson(cmd["parameters"], params)) {
    return false;
  }

  out = "";
  out.reserve(512);
  out += "schema=";
  out += (const char *)(cmd["schema"] | "");
  out += "\nroom_id=";
  out += (const char *)(cmd["room_id"] | "");
  out += "\ndevice_id=";
  out += (const char *)(cmd["device_id"] | "");
  out += "\ncommand_id=";
  out += (const char *)(cmd["command_id"] | "");
  out += "\ncorrelation_id=";
  out += (const char *)(cmd["correlation_id"] | "");
  out += "\nsequence=";
  out += String((uint64_t)(cmd["sequence"] | 0));
  out += "\nissued_at_unix_ms=";
  out += String((uint64_t)(cmd["issued_at_unix_ms"] | 0));
  out += "\naction=";
  out += (const char *)(cmd["action"] | "");
  out += "\nsafety_class=";
  out += (const char *)(cmd["safety_class"] | "");
  out += "\nparameters=";
  out += params;
  return true;
}

bool Client::canonicalParametersJson(const JsonVariantConst &parameters, String &out) {
  if (parameters.isNull()) {
    out = "{}";
    return true;
  }
  return canonicalizeValue(parameters, out, 0);
}

static int compare_cstr(const void *a, const void *b) {
  const char *aa = *(const char *const *)a;
  const char *bb = *(const char *const *)b;
  return strcmp(aa, bb);
}

bool Client::canonicalizeValue(const JsonVariantConst &v, String &out, uint8_t depth) {
  if (depth > 10) return false;

  if (v.is<JsonObjectConst>()) {
    JsonObjectConst obj = v.as<JsonObjectConst>();
    const size_t kMaxKeys = 64;
    const char *keys[kMaxKeys];
    size_t n = 0;
    for (JsonPairConst kv : obj) {
      if (n >= kMaxKeys) return false;
      keys[n++] = kv.key().c_str();
    }
    qsort(keys, n, sizeof(const char *), compare_cstr);

    out += "{";
    for (size_t i = 0; i < n; i++) {
      if (i) out += ",";
      out += "\"";
      out += keys[i];
      out += "\":";
      String nested;
      JsonVariantConst vv = obj[keys[i]];
      if (!canonicalizeValue(vv, nested, depth + 1)) return false;
      out += nested;
    }
    out += "}";
    return true;
  }

  if (v.is<JsonArrayConst>()) {
    JsonArrayConst arr = v.as<JsonArrayConst>();
    out += "[";
    bool first = true;
    for (JsonVariantConst item : arr) {
      if (!first) out += ",";
      first = false;
      String nested;
      if (!canonicalizeValue(item, nested, depth + 1)) return false;
      out += nested;
    }
    out += "]";
    return true;
  }

  if (v.is<const char *>()) {
    String s = v.as<const char *>();
    String escaped;
    escaped.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if (c == '\\' || c == '"') {
        escaped += '\\';
        escaped += c;
      } else if (c == '\n') {
        escaped += "\\n";
      } else if (c == '\r') {
        escaped += "\\r";
      } else if (c == '\t') {
        escaped += "\\t";
      } else {
        escaped += c;
      }
    }
    out += "\"";
    out += escaped;
    out += "\"";
    return true;
  }

  if (v.is<bool>()) {
    out += v.as<bool>() ? "true" : "false";
    return true;
  }

  if (v.is<int>() || v.is<long>() || v.is<long long>()) {
    out += String((long long)v.as<long long>());
    return true;
  }
  if (v.is<unsigned int>() || v.is<unsigned long>() || v.is<unsigned long long>()) {
    out += String((unsigned long long)v.as<unsigned long long>());
    return true;
  }
  if (v.is<float>() || v.is<double>()) {
    out += String(v.as<double>(), 6);
    return true;
  }

  if (v.isNull()) {
    out += "null";
    return true;
  }

  String tmp;
  serializeJson(v, tmp);
  out += tmp;
  return true;
}

} // namespace sentient_v8
