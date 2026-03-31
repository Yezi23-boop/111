#pragma once

#include <string>

namespace official_chat {

enum class ProtocolKind {
  kWebsocket,
  kMqtt,
};

enum class ProtocolConfigSource {
  kNvsMqtt,
  kNvsWebsocket,
  kPublicConfig,
  kBuiltinDefault,
};

struct MqttRuntimeConfig {
  std::string endpoint;
  std::string client_id;
  std::string username;
  std::string password;
  int keepalive = 240;
  std::string publish_topic;

  bool IsValid() const;
};

struct WebsocketRuntimeConfig {
  std::string url;
  std::string token;
  int version = 2;

  bool IsValid() const;
};

struct WebsocketFallbackConfig {
  std::string url;
  std::string token;
  int version = 2;
  bool from_public_config = false;
};

struct ProtocolConfigSelection {
  ProtocolKind kind = ProtocolKind::kWebsocket;
  ProtocolConfigSource source = ProtocolConfigSource::kBuiltinDefault;
  MqttRuntimeConfig mqtt;
  WebsocketRuntimeConfig websocket;
  bool mqtt_config_present = false;
  bool websocket_config_present = false;
};

ProtocolConfigSelection LoadProtocolConfigSelection(
    const WebsocketFallbackConfig &fallback);

const char *ProtocolKindToString(ProtocolKind kind);
const char *ProtocolConfigSourceToString(ProtocolConfigSource source);

}  // namespace official_chat
