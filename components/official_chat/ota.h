#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <esp_err.h>

struct cJSON;

namespace official_chat {

class Ota {
 public:
  explicit Ota(std::string ota_url);
  ~Ota() = default;

  esp_err_t CheckVersion();
  esp_err_t Activate();
  bool StartUpgrade(std::function<void(int progress, size_t speed)> callback);
  static bool Upgrade(const std::string &firmware_url,
                      std::function<void(int progress, size_t speed)> callback);
  void MarkCurrentVersionValid();

  bool HasActivationChallenge() const { return has_activation_challenge_; }
  bool HasNewVersion() const { return has_new_version_; }
  bool HasMqttConfig() const { return has_mqtt_config_; }
  bool HasWebsocketConfig() const { return has_websocket_config_; }
  bool HasActivationCode() const { return has_activation_code_; }
  bool HasServerTime() const { return has_server_time_; }
  const std::string &GetFirmwareVersion() const { return firmware_version_; }
  const std::string &GetCurrentVersion() const { return current_version_; }
  const std::string &GetFirmwareUrl() const { return firmware_url_; }
  const std::string &GetActivationMessage() const { return activation_message_; }
  const std::string &GetActivationCode() const { return activation_code_; }
  int GetActivationTimeoutMs() const { return activation_timeout_ms_; }
  const std::string &GetOtaUrl() const { return ota_url_; }

 private:
  static std::vector<int> ParseVersion(const std::string &version);
  static bool IsNewVersionAvailable(const std::string &current_version,
                                    const std::string &new_version);
  esp_err_t PerformJsonRequest(const char *method, const std::string &url,
                               const std::string &payload, int *status_code,
                               std::string *response) const;
  std::string BuildSystemInfoJson() const;
  std::string BuildActivateUrl() const;
  void PersistProtocolConfig(const cJSON *root, const char *section_name);
  void PersistServerTime(const cJSON *root);

  std::string ota_url_;
  std::string activation_message_;
  std::string activation_code_;
  std::string current_version_;
  std::string firmware_version_;
  std::string firmware_url_;
  std::string activation_challenge_;
  bool has_new_version_ = false;
  bool has_mqtt_config_ = false;
  bool has_websocket_config_ = false;
  bool has_server_time_ = false;
  bool has_activation_code_ = false;
  bool has_activation_challenge_ = false;
  int activation_timeout_ms_ = 30000;
};

}  // namespace official_chat
