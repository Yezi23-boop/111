#pragma once

#include <functional>
#include <string>

#include <esp_err.h>

namespace official_chat {

class AssetsRuntime {
 public:
  struct Result {
    bool attempted = false;
    bool applied = false;
    esp_err_t error = ESP_OK;
    std::string message;
  };

  AssetsRuntime() = default;

  Result CheckAndApplyPendingDownload(
      std::function<void(int progress, size_t speed)> progress_callback);

 private:
  Result DownloadAndApply(const std::string &url,
                          std::function<void(int progress, size_t speed)>
                              progress_callback);
  bool RefreshSpeechModels();
};

}  // namespace official_chat
