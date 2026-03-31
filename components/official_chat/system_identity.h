#pragma once

#include <string>

namespace official_chat {

class SystemIdentity {
 public:
  static std::string GetDeviceId();
  static std::string GetUserAgent();
  static std::string GetOrCreateClientId();
  static bool HasSerialNumber();
  static std::string GetSerialNumber();
  static int GetActivationVersion();
  static std::string BuildActivationPayload(const std::string &challenge);
};

}  // namespace official_chat
