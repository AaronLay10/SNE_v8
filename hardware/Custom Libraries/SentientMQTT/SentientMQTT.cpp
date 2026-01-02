#include "SentientMQTT.h"

// Temporarily suppress ArduinoJson v7 deprecation warnings for DynamicJsonDocument
// until we fully migrate to the new JsonDocument API across the codebase.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <cstring>
#include <new>

namespace
{
  bool isValidIp(const IPAddress &ip)
  {
    return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
  }

  unsigned long secondsSinceBoot()
  {
    return millis() / 1000;
  }
} // namespace

SentientMQTT *SentientMQTT::s_activeInstance = nullptr;

SentientMQTT::SentientMQTT(const SentientMQTTConfig &config)
    : _config(config), _mqttClient(_networkClient) {}

bool SentientMQTT::begin()
{
  // Either controllerId (controller) or deviceId must be set for identification
  if ((!_config.controllerId || _config.controllerId[0] == '\0') &&
      (!_config.deviceId || _config.deviceId[0] == '\0'))
  {
    Serial.println(F("[SentientMQTT] controllerId (controller_id) or deviceId is required"));
    return false;
  }

  if (!configureNetwork())
  {
    Serial.println(F("[SentientMQTT] network configuration failed"));
    return false;
  }

  if (_config.keepAliveSeconds == 0)
  {
    _config.keepAliveSeconds = 60;
  }
  _mqttClient.setKeepAlive(_config.keepAliveSeconds);

  // Set buffer size based on publishJsonCapacity
  size_t required = static_cast<size_t>(_config.publishJsonCapacity) * 4;
  if (required < 2048)
  {
    required = 2048;
  }
  Serial.print(F("[SentientMQTT] Setting buffer size to: "));
  Serial.println(required);
  if (!_mqttClient.setBufferSize(required))
  {
    Serial.print(F("[SentientMQTT] failed to set buffer size, current="));
    Serial.println(_mqttClient.getBufferSize());
  }
  else
  {
    Serial.print(F("[SentientMQTT] buffer size set successfully to: "));
    Serial.println(_mqttClient.getBufferSize());
  }

  if (isValidIp(_config.brokerIp))
  {
    Serial.print(F("[SentientMQTT] Using broker IP: "));
    Serial.print(_config.brokerIp);
    Serial.print(F(":"));
    Serial.println(_config.brokerPort);
    _mqttClient.setServer(_config.brokerIp, _config.brokerPort);
  }
  else if (_config.brokerHost && _config.brokerHost[0] != '\0')
  {
    Serial.print(F("[SentientMQTT] Using broker Host: "));
    Serial.print(_config.brokerHost);
    Serial.print(F(":"));
    Serial.println(_config.brokerPort);
    _mqttClient.setServer(_config.brokerHost, _config.brokerPort);
  }
  else
  {
    Serial.println(F("[SentientMQTT] brokerHost or brokerIp must be provided"));
    return false;
  }

  s_activeInstance = this;
  _mqttClient.setCallback(mqttCallbackThunk);

  Serial.println(F("[SentientMQTT] initialized"));
  return true;
}

void SentientMQTT::loop()
{
#if !defined(ESP32)
  // Maintain Ethernet connection (DHCP lease renewal, link status check)
  // This is critical for Teensy devices using DHCP
  if (_config.useDhcp)
  {
    Ethernet.maintain();
  }

  // Poll FNET services (mDNS, etc.)
  fnet_service_poll();
#endif

  ensureConnected();
  _mqttClient.loop();

  if (_config.autoHeartbeat && _mqttClient.connected())
  {
    const unsigned long now = millis();
    if (now - _lastHeartbeat >= _config.heartbeatIntervalMs)
    {
      publishHeartbeat();
    }
  }
}

bool SentientMQTT::publishSensor(const char *name, float value, const char *unit)
{
  DynamicJsonDocument doc(_config.publishJsonCapacity);
  doc["name"] = name ? name : "sensor";
  doc["value"] = value;
  if (unit && unit[0] != '\0')
  {
    doc["unit"] = unit;
  }
  doc["timestamp"] = secondsSinceBoot();
  return publishJson("sensors", name, doc);
}

bool SentientMQTT::publishMetric(const char *name, float value, const char *unit)
{
  DynamicJsonDocument doc(_config.publishJsonCapacity);
  doc["name"] = name ? name : "metric";
  doc["value"] = value;
  if (unit && unit[0] != '\0')
  {
    doc["unit"] = unit;
  }
  doc["timestamp"] = secondsSinceBoot();
  return publishJson("metrics", name, doc);
}

bool SentientMQTT::publishState(const char *state)
{
  DynamicJsonDocument doc(_config.publishJsonCapacity);
  doc["state"] = state ? state : "unknown";
  doc["timestamp"] = secondsSinceBoot();
  if (_config.deviceId)
  {
    doc["deviceId"] = _config.deviceId;
  }
  return publishJson("status", "state", doc, false);
}

bool SentientMQTT::publishState(const char *state, const JsonDocument &extras)
{
  DynamicJsonDocument doc(_config.publishJsonCapacity);
  doc["state"] = state ? state : "unknown";
  doc["timestamp"] = secondsSinceBoot();
  if (_config.deviceId)
  {
    doc["deviceId"] = _config.deviceId;
  }

  JsonObjectConst extraObject = extras.as<JsonObjectConst>();
  for (JsonPairConst kv : extraObject)
  {
    doc[kv.key()] = kv.value();
  }
  return publishJson("status", "state", doc, false);
}

bool SentientMQTT::publishEvent(const char *eventName, const JsonDocument &payload)
{
  const char *name = (eventName && eventName[0] != '\0') ? eventName : "Event";
  return publishJson("events", name, payload);
}

bool SentientMQTT::publishJson(const char *category, const char *item, const JsonDocument &payload, bool retain)
{
  String topic = buildTopic(category, item);
  String message;
  serializeJson(payload, message);
  return publishRaw(topic, message.c_str(), retain);
}

bool SentientMQTT::publishText(const char *category, const char *item, const char *payload, bool retain)
{
  String topic = buildTopic(category, item);
  const char *safePayload = (payload && payload[0] != '\0') ? payload : "";
  return publishRaw(topic, safePayload, retain);
}

bool SentientMQTT::publishHeartbeat()
{
  DynamicJsonDocument doc(_config.publishJsonCapacity);

  // If custom heartbeat builder is provided, use ONLY its output (stateless minimal heartbeat)
  if (_heartbeatBuilder)
  {
    if (!_heartbeatBuilder(doc, _heartbeatContext))
    {
      return false;
    }
  }
  else
  {
    // Default heartbeat (legacy - for backward compatibility)
    doc["timestamp"] = secondsSinceBoot();
    doc["state"] = _mqttClient.connected() ? "online" : "disconnected";
    if (_config.deviceId)
    {
      doc["deviceId"] = _config.deviceId;
      doc["displayName"] = _config.displayName ? _config.displayName : _config.deviceId;
    }
    if (_config.roomId)
    {
      doc["roomId"] = _config.roomId;
    }
    if (_config.controllerId)
    {
      doc["controllerId"] = _config.controllerId;
    }
  }

  return publishHeartbeat(doc);
}

bool SentientMQTT::publishHeartbeat(const JsonDocument &payload)
{
  bool ok = publishJson("status", "heartbeat", payload, false);
  if (ok)
  {
    _lastHeartbeat = millis();
  }
  return ok;
}

void SentientMQTT::setCommandCallback(SentientCommandCallback callback, void *context)
{
  _commandCallback = callback;
  _commandContext = context;
}

void SentientMQTT::setHeartbeatBuilder(SentientHeartbeatBuilder callback, void *context)
{
  _heartbeatBuilder = callback;
  _heartbeatContext = context;
}

void SentientMQTT::setOnConnect(SentientConnectionCallback callback, void *context)
{
  _onConnect = callback;
  _onConnectContext = context;
}

void SentientMQTT::setOnDisconnect(SentientConnectionCallback callback, void *context)
{
  _onDisconnect = callback;
  _onDisconnectContext = context;
}

bool SentientMQTT::configureNetwork()
{
  if (!Serial)
  {
    Serial.begin(115200);
    unsigned long waited = 0;
    while (!Serial && waited < 1000)
    {
      delay(10);
      waited += 10;
    }
  }

#if defined(ESP32)
  if (!_config.wifiSsid || _config.wifiSsid[0] == '\0')
  {
    Serial.println(F("[SentientMQTT] Wi-Fi SSID not provided"));
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(_config.wifiSsid, _config.wifiPassword);

  Serial.print(F("[SentientMQTT] Connecting to Wi-Fi"));
  const unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30'000)
  {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("[SentientMQTT] Wi-Fi connection failed"));
    return false;
  }

  Serial.print(F("[SentientMQTT] Wi-Fi connected, IP="));
  Serial.println(WiFi.localIP());
  return true;
#else
  if (_config.mac)
  {
    memcpy(_macAddress, _config.mac, sizeof(_macAddress));
  }
  else
  {
    teensyMAC(_macAddress);
  }

  Serial.print(F("[SentientMQTT] MAC="));
  for (size_t i = 0; i < sizeof(_macAddress); ++i)
  {
    if (i > 0)
    {
      Serial.print(':');
    }
    Serial.print(_macAddress[i], HEX);
  }
  Serial.println();

  bool success = false;
  Serial.println(F("[SentientMQTT] Starting Ethernet initialization..."));

  if (_config.useDhcp)
  {
    Serial.println(F("[SentientMQTT] Using DHCP..."));
    success = Ethernet.begin(_macAddress) != 0;
  }
  else
  {
    Serial.print(F("[SentientMQTT] Using static IP: "));
    Serial.println(_config.staticIp);
    Ethernet.begin(_macAddress, _config.staticIp, _config.dns, _config.gateway, _config.subnet);
    success = true;
  }

  if (!success)
  {
    Serial.println(F("[SentientMQTT] Ethernet configuration failed"));
    return false;
  }

  delay(500);
  Serial.print(F("[SentientMQTT] Ethernet up, IP="));
  Serial.println(Ethernet.localIP());

  // Initialize mDNS for network identification
  if (_config.displayName && _config.displayName[0] != '\0')
  {
    // Build hostname with optional prefix
    char mdnsName[64];
    if (_config.hostnamePrefix && _config.hostnamePrefix[0] != '\0')
    {
      snprintf(mdnsName, sizeof(mdnsName), "%s-%s", _config.hostnamePrefix, _config.displayName);
    }
    else
    {
      strncpy(mdnsName, _config.displayName, sizeof(mdnsName) - 1);
    }
    mdnsName[sizeof(mdnsName) - 1] = '\0';

    // Convert to lowercase and replace spaces with hyphens
    for (char *p = mdnsName; *p; ++p)
    {
      if (*p == ' ')
        *p = '-';
      else
        *p = tolower(*p);
    }

    fnet_mdns_params_t mdnsParams;
    mdnsParams.netif_desc = fnet_netif_get_default();
    mdnsParams.addr_family = AF_INET;
    mdnsParams.name = mdnsName;

    _mdnsService = fnet_mdns_init(&mdnsParams);
    if (_mdnsService)
    {
      Serial.print(F("[SentientMQTT] mDNS started: "));
      Serial.print(mdnsName);
      Serial.println(F(".local"));
    }
    else
    {
      Serial.println(F("[SentientMQTT] mDNS initialization failed"));
    }

    // Also start LLMNR for Windows/UniFi hostname resolution
    fnet_llmnr_params_t llmnrParams;
    llmnrParams.netif_desc = fnet_netif_get_default();
    llmnrParams.addr_family = AF_INET;
    llmnrParams.host_name = mdnsName;

    _llmnrService = fnet_llmnr_init(&llmnrParams);
    if (_llmnrService)
    {
      Serial.print(F("[SentientMQTT] LLMNR started: "));
      Serial.println(mdnsName);
    }
    else
    {
      Serial.println(F("[SentientMQTT] LLMNR initialization failed"));
    }
  }

  return true;
#endif
}

void SentientMQTT::ensureConnected()
{
  if (_mqttClient.connected())
  {
    if (!_wasConnected)
    {
      _wasConnected = true;
      if (_onConnect)
      {
        _onConnect(_onConnectContext);
      }
      String onlinePayload = buildConnectionPayload("online");
      publishRaw(buildTopic("status", "connection"), onlinePayload.c_str(), false);
    }
    return;
  }

  if (_wasConnected)
  {
    _wasConnected = false;
    if (_onDisconnect)
    {
      _onDisconnect(_onDisconnectContext);
    }
  }

  const unsigned long now = millis();
  if (now - _lastConnectAttempt < _config.reconnectDelayMs)
  {
    return;
  }
  _lastConnectAttempt = now;

  String clientId = buildClientId();
  String willTopic = buildTopic("status", "connection");
  String willPayload = buildConnectionPayload("offline");

  // Debug output for connection attempt
  Serial.println(F("\n[SentientMQTT] Attempting broker connection:"));
  Serial.print(F("  Client ID: "));
  Serial.println(clientId);
  Serial.print(F("  Will Topic: "));
  Serial.println(willTopic);
  Serial.print(F("  Will Topic Length: "));
  Serial.println(willTopic.length());
  Serial.print(F("  Will Payload: "));
  Serial.println(willPayload);
  Serial.print(F("  Will Payload Length: "));
  Serial.println(willPayload.length());
  Serial.print(F("  Username: "));
  Serial.println(_config.username ? _config.username : "null");
  Serial.print(F("  Password: "));
  Serial.println(_config.password ? "***SET***" : "null");

  bool connected = false;

  // TEMPORARY: Try connecting WITHOUT Will message to isolate the issue
  Serial.println(F("[SentientMQTT] DEBUG: Connecting WITHOUT Will message"));
  if (_config.username && _config.password)
  {
    connected = _mqttClient.connect(clientId.c_str(), _config.username, _config.password);
  }
  else
  {
    connected = _mqttClient.connect(clientId.c_str());
  }

  if (!connected)
  {
    Serial.print(F("[SentientMQTT] Broker connect failed, rc="));
    Serial.println(_mqttClient.state());
    return;
  }

  Serial.println(F("[SentientMQTT] Broker connected"));

  // Subscribe to commands for this controller
  // Canonical structure: [namespace]/[room]/commands/[controller_id]/[device_id]/[specific_command]
  // Subscribe with wildcard for any device on this controller: [namespace]/[room]/commands/[controller_id]/#
  {
    String topic;
    // namespace
    topic += (_config.namespaceId && _config.namespaceId[0] != '\0') ? _config.namespaceId : "paragon";
    topic += '/';
    // room
    if (_config.roomId && _config.roomId[0] != '\0')
    {
      topic += _config.roomId;
    }
    topic += "/commands/";
    // controller (controllerId)
    if (_config.controllerId && _config.controllerId[0] != '\0')
    {
      topic += _config.controllerId;
    }
    topic += "/#";

    _mqttClient.subscribe(topic.c_str());
    Serial.print(F("[SentientMQTT] Subscribed to commands: "));
    Serial.println(topic);
  }

  if (_onConnect)
  {
    _onConnect(_onConnectContext);
  }

  _wasConnected = true;
  _lastHeartbeat = millis();
  publishRaw(willTopic, buildConnectionPayload("online").c_str(), false);
}

void SentientMQTT::handleIncoming(char *topic, uint8_t *payload, unsigned int length)
{
  if (!_commandCallback)
  {
    return;
  }

  const char *lastSlash = strrchr(topic, '/');
  const char *commandStart = lastSlash ? lastSlash + 1 : topic;
  if (commandStart[0] == '\0' || strcmp(commandStart, "#") == 0)
  {
    return;
  }

  DynamicJsonDocument doc(_config.commandJsonCapacity);
  const size_t bufferSize = static_cast<size_t>(length) + 1;
  char *buffer = new (std::nothrow) char[bufferSize];
  if (!buffer)
  {
    Serial.println(F("[SentientMQTT] command buffer allocation failed"));
    return;
  }
  memcpy(buffer, payload, length);
  buffer[length] = '\0';

  DeserializationError error = deserializeJson(doc, buffer);
  if (error)
  {
    doc.clear();
    doc["value"] = buffer;
  }

  _commandCallback(commandStart, doc, _commandContext);
  delete[] buffer;
}

bool SentientMQTT::publishRaw(const String &topic, const char *payload, bool retain)
{
  ensureConnected();
  if (!_mqttClient.connected())
  {
    return false;
  }

  if (!payload)
  {
    payload = "";
  }

  bool ok = _mqttClient.publish(topic.c_str(), payload, retain);
  if (!ok)
  {
    Serial.print(F("[SentientMQTT] publish failed for topic "));
    Serial.print(topic);
    Serial.print(F(" len="));
    Serial.print(strlen(payload));
    Serial.print(F(" buffer="));
    Serial.print(_mqttClient.getBufferSize());
    Serial.print(F(" state="));
    Serial.println(_mqttClient.state());
  }
  return ok;
}

String SentientMQTT::buildTopic(const char *category, const char *item) const
{
  String topic;
  topic.reserve(96);

  auto appendSegment = [&topic](const char *segment)
  {
    if (!segment || segment[0] == '\0')
    {
      return;
    }
    if (topic.length() > 0 && topic[topic.length() - 1] != '/')
    {
      topic += '/';
    }
    topic += segment;
  };

  // New topic structure: [namespace]/[room]/[category]/[controller_id]/[device_id]/[item]
  appendSegment(_config.namespaceId ? _config.namespaceId : "paragon");
  appendSegment(_config.roomId);
  appendSegment(category);
  appendSegment(_config.controllerId);
  appendSegment(_config.deviceId);
  appendSegment(item);

  return topic;
}

String SentientMQTT::buildClientId() const
{
  String clientId;
  // Use controllerId (controller_id) for client ID, not deviceId
  if (_config.controllerId)
  {
    clientId += _config.controllerId;
  }
  else if (_config.deviceId)
  {
    clientId += _config.deviceId;
  }
  else
  {
    clientId += "controller";
  }
  clientId += '-';
  clientId += String(millis() & 0xFFFF, HEX);
  return clientId;
}

String SentientMQTT::buildConnectionPayload(const char *state) const
{
  DynamicJsonDocument doc(_config.publishJsonCapacity);
  doc["state"] = state;
  doc["timestamp"] = secondsSinceBoot();
  if (_config.deviceId)
  {
    doc["deviceId"] = _config.deviceId;
  }
  if (_config.roomId)
  {
    doc["roomId"] = _config.roomId;
  }
  if (_config.controllerId)
  {
    doc["controllerId"] = _config.controllerId;
  }

  String payload;
  serializeJson(doc, payload);
  return payload;
}

void SentientMQTT::mqttCallbackThunk(char *topic, uint8_t *payload, unsigned int length)
{
  if (s_activeInstance)
  {
    s_activeInstance->handleIncoming(topic, payload, length);
  }
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
