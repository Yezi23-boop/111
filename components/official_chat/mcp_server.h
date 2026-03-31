#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <cJSON.h>

#include "device_state.h"

namespace official_chat {

class McpServer {
 public:
  using SendCallback = std::function<void(const std::string &payload)>;
  struct RuntimeStatus {
    DeviceState chat_state = DeviceState::kUnknown;
    bool device_aec_enabled = false;
  };
  using RuntimeStatusCallback = std::function<RuntimeStatus()>;

  static McpServer &GetInstance();

  void SetSendCallback(SendCallback callback);
  void SetServerInfo(std::string name, std::string version);
  void SetRuntimeStatusCallback(RuntimeStatusCallback callback);
  void AddCommonTools();
  void ParseMessage(const cJSON *json);

 private:
  struct ToolDefinition {
    std::string name;
    std::string description;
  };

  McpServer() = default;

  void AddToolLocked(std::string name, std::string description);
  void HandleToolCall(int id, const cJSON *params);
  cJSON *BuildToolsListResult();
  cJSON *BuildTextToolResult(const std::string &text);
  cJSON *BuildDeviceStatusJson();
  cJSON *BuildNetworkStatusJson();
  void ReplyResult(int id, cJSON *result);
  void ReplyError(int id, const std::string &message);
  bool SendEnvelope(cJSON *root);

  std::mutex mutex_;
  SendCallback send_callback_;
  RuntimeStatusCallback runtime_status_callback_;
  std::string server_name_ = "official_chat";
  std::string server_version_ = "unknown";
  std::vector<ToolDefinition> tools_;
  bool common_tools_added_ = false;
};

}  // namespace official_chat
