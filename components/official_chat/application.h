#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "esp_err.h"
#include "esp_timer.h"

#include "audio/audio_service.h"
#include "assets_runtime.h"
#include "audio/local_audio_codec_adapter.h"
#include "device_state_machine.h"
#include "official_chat.h"
#include "ota.h"
#include "protocol_config.h"
#include "protocols/protocol.h"

namespace official_chat {

constexpr EventBits_t kMainEventSchedule = BIT0;
constexpr EventBits_t kMainEventSendAudio = BIT1;
constexpr EventBits_t kMainEventToggleChat = BIT2;
constexpr EventBits_t kMainEventStartListening = BIT3;
constexpr EventBits_t kMainEventStopListening = BIT4;
constexpr EventBits_t kMainEventStateChanged = BIT5;
constexpr EventBits_t kMainEventProtocolClosed = BIT6;
constexpr EventBits_t kMainEventWakeWordDetected = BIT7;
constexpr EventBits_t kMainEventVadChange = BIT8;
constexpr EventBits_t kMainEventWorkerExited = BIT9;
constexpr EventBits_t kMainEventActivationDone = BIT10;
constexpr EventBits_t kMainEventUpgradeProgress = BIT11;
constexpr EventBits_t kMainEventGracefulButtonStopTimeout = BIT12;

struct ModelListDeleter {
  void operator()(srmodel_list_t *models) const;
};

class Application {
 public:
  Application();
  ~Application();

  esp_err_t Initialize(const official_chat_config_t &config);
  esp_err_t Start();
  esp_err_t StartListening();
  esp_err_t StartSyntheticWakeWord();
  esp_err_t ToggleChat();
  esp_err_t StopListening();
  esp_err_t ReloadProtocol();
  esp_err_t SetDeviceAecEnabled(bool enabled);
  esp_err_t SetEventCallback(official_chat_event_callback_t callback,
                             void *user_data);
  bool GetDeviceAecEnabled() const;
  DeviceState GetState() const;

 private:
  struct ProgressSnapshot {
    int progress = 0;
    size_t speed_bytes_per_sec = 0;
    bool valid = false;
  };

  void Schedule(std::function<void()> &&callback);
  void RunLoop();
  void HandleStateChanged();
  void HandleActivationDoneEvent();
  void HandleUpgradeProgressEvent();
  void HandleToggleChatEvent();
  void HandleStartListeningEvent();
  void HandleStartListeningEvent(ListeningMode mode);
  void HandleStopListeningEvent();
  void HandleProtocolClosedEvent();
  void HandleWakeWordDetectedEvent();
  void HandleGracefulButtonStopTimeout();
  void StartActivationTask();
  void ActivationTask();
  void ContinueOpenAudioChannel(ListeningMode mode);
  void ContinueWakeWordInvoke(const std::string &wake_word);
  ListeningMode GetDefaultListeningMode() const;
  void SetListeningMode(ListeningMode mode);
  void StartGracefulButtonStop();
  void CancelGracefulButtonStop(const char *reason);
  void TryFinalizeGracefulButtonStop(const char *reason);
  void FinalizeGracefulButtonStopToIdle(bool timed_out, const char *reason);
  bool SetDeviceState(DeviceState state);
  std::string ResolveOtaUrl() const;
  void EmitEvent(official_chat_event_type_t type, official_chat_state_t state,
                 const std::string &message, int progress, size_t speed,
                 esp_err_t error);
  void EmitStateChangedEvent();
  void EmitMessageEvent(official_chat_event_type_t type,
                        const std::string &message);
  void EmitProgressEvent(official_chat_event_type_t type, int progress,
                         size_t speed);
  void EmitErrorEvent(esp_err_t error, const std::string &message);
  void EmitRebootingEvent();
  void ReplayEventSnapshot();
  void InitializeProtocol();
  esp_err_t InitializeAudioService();
  bool ShouldAcceptIncomingDownlinkAudio(DeviceState state) const;
  void SetDownlinkAudioActive(bool active, const char *reason);

  int speak_volume_ = 0;
  float record_gain_db_ = 0.0f;
  std::string websocket_url_;
  std::string access_token_;
  std::string ota_url_;
  bool has_public_websocket_config_ = false;
  std::mutex mutex_;
  std::deque<std::function<void()>> main_tasks_;
  std::unique_ptr<Protocol> protocol_;
  std::unique_ptr<LocalAudioCodecAdapter> codec_;
  std::unique_ptr<Ota> ota_;
  AssetsRuntime assets_runtime_;
  EventGroupHandle_t event_group_ = nullptr;
  esp_timer_handle_t graceful_button_stop_timer_ = nullptr;
  TaskHandle_t worker_task_handle_ = nullptr;
  TaskHandle_t activation_task_handle_ = nullptr;
  DeviceStateMachine state_machine_;
  ListeningMode listening_mode_ = kListeningModeManualStop;
  std::unique_ptr<srmodel_list_t, ModelListDeleter> models_list_;
  AudioService audio_service_;
  bool device_aec_enabled_ = false;
  bool started_ = false;
  official_chat_event_callback_t event_callback_ = nullptr;
  void *event_callback_user_data_ = nullptr;
  bool replay_events_pending_ = false;
  std::string last_event_message_;
  ProgressSnapshot last_assets_progress_;
  ProgressSnapshot last_upgrade_progress_;
  esp_err_t last_error_ = ESP_OK;
  std::atomic<bool> downlink_audio_active_{false};
  std::atomic<uint32_t> downlink_audio_accepted_while_pending_count_{0};
  std::atomic<uint32_t> downlink_audio_dropped_by_gate_count_{0};
  std::atomic<int> downlink_audio_pending_log_budget_{3};
  std::atomic<int> downlink_audio_drop_log_budget_{5};
  std::atomic<int> downlink_audio_queue_drop_log_budget_{5};
  bool button_stop_pending_ = false;
  bool button_stop_tts_stop_seen_ = false;
  bool button_stop_channel_closed_ = false;
  bool button_stop_finalizing_ = false;
  int64_t button_stop_deadline_us_ = 0;
};

}  // namespace official_chat
