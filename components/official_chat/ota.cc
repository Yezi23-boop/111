#include "ota.h"

#include <sys/time.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <utility>

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <sdkconfig.h>

#include "board_metadata.h"
#include "settings.h"
#include "system_identity.h"

extern "C" {
#include "system_util.h"
}

namespace official_chat {

namespace {

constexpr char kTag[] = "official_ota";
constexpr int kHttpBufferSize = 1024;                /**< HTTP 的接收和发送缓冲区大小，单位为字节，用于适配有限的 RAM 资源。 */
constexpr char kDefaultLanguageCode[] = "zh-CN";     /**< 设备在出厂或者未设置语言时的默认识别配置使用。 */

/**
 * @brief 按后缀拼装完整有效的网络 URL 请求目标。
 * 
 * 因为外部配置或系统配置结尾可能会出现或缺乏 '/'，统一容错处理。
 * 
 * @param[in] base 基地址 URL
 * @param[in] suffix 需要添加请求路径或者是文件名
 * @return 组装成功有效的可调用地址。
 */
std::string JoinUrl(const std::string &base, const char *suffix) {
  if (base.empty()) {
    return {};
  }
  if (base.back() == '/') {
    return base + suffix;
  }
  return base + "/" + suffix;
}

const char *GetLanguageCode() {
#if defined(CONFIG_OFFICIAL_CHAT_LANGUAGE_CODE)
  if (std::strlen(CONFIG_OFFICIAL_CHAT_LANGUAGE_CODE) > 0) {
    return CONFIG_OFFICIAL_CHAT_LANGUAGE_CODE;
  }
#endif
  return kDefaultLanguageCode;
}

std::string BuildCompileTime(const esp_app_desc_t *app_desc) {
  if (app_desc == nullptr) {
    return {};
  }
  return std::string(get_compile_time(app_desc)) + "Z";
}

std::string BuildElfSha256(const esp_app_desc_t *app_desc) {
  if (app_desc == nullptr) {
    return {};
  }

  std::array<char, 65> sha256 = {};
  for (size_t i = 0; i < 32; ++i) {
    std::snprintf(sha256.data() + (i * 2), sha256.size() - (i * 2), "%02x",
                  app_desc->app_elf_sha256[i]);
  }
  return sha256.data();
}

void AddChipInfoJson(cJSON *root) {
  esp_chip_info_t chip_info = {};
  esp_chip_info(&chip_info);

  cJSON *chip_info_json = cJSON_CreateObject();
  cJSON_AddNumberToObject(chip_info_json, "model", chip_info.model);
  cJSON_AddNumberToObject(chip_info_json, "cores", chip_info.cores);
  cJSON_AddNumberToObject(chip_info_json, "revision", chip_info.revision);
  cJSON_AddNumberToObject(chip_info_json, "features", chip_info.features);
  cJSON_AddItemToObject(root, "chip_info", chip_info_json);
}

void AddApplicationJson(cJSON *root, const esp_app_desc_t *app_desc) {
  cJSON *application = cJSON_CreateObject();
  if (app_desc != nullptr) {
    cJSON_AddStringToObject(application, "name", app_desc->project_name);
    cJSON_AddStringToObject(application, "version", app_desc->version);
    cJSON_AddStringToObject(application, "compile_time",
                            BuildCompileTime(app_desc).c_str());
    cJSON_AddStringToObject(application, "idf_version", app_desc->idf_ver);
    cJSON_AddStringToObject(application, "elf_sha256",
                            BuildElfSha256(app_desc).c_str());
  }
  cJSON_AddItemToObject(root, "application", application);
}

void AddPartitionTableJson(cJSON *root) {
  cJSON *partition_table = cJSON_CreateArray();
  esp_partition_iterator_t iterator =
      esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY,
                         nullptr);
  while (iterator != nullptr) {
    const esp_partition_t *partition = esp_partition_get(iterator);
    cJSON *partition_json = cJSON_CreateObject();
    cJSON_AddStringToObject(partition_json, "label", partition->label);
    cJSON_AddNumberToObject(partition_json, "type", partition->type);
    cJSON_AddNumberToObject(partition_json, "subtype", partition->subtype);
    cJSON_AddNumberToObject(partition_json, "address", partition->address);
    cJSON_AddNumberToObject(partition_json, "size", partition->size);
    cJSON_AddItemToArray(partition_table, partition_json);
    iterator = esp_partition_next(iterator);
  }
  cJSON_AddItemToObject(root, "partition_table", partition_table);
}

void AddRunningOtaJson(cJSON *root) {
  cJSON *ota = cJSON_CreateObject();
  const esp_partition_t *running_partition = esp_ota_get_running_partition();
  if (running_partition != nullptr) {
    cJSON_AddStringToObject(ota, "label", running_partition->label);
  }
  cJSON_AddItemToObject(root, "ota", ota);
}

void AddBoardMetadataJson(cJSON *root) {
  const BoardMetadata board_metadata = LoadCurrentBoardMetadata();

  cJSON *board = cJSON_CreateObject();
  cJSON_AddStringToObject(board, "type", board_metadata.type.c_str());
  cJSON_AddStringToObject(board, "name", board_metadata.name.c_str());
  cJSON_AddItemToObject(root, "board", board);

  if (board_metadata.has_display) {
    cJSON *display = cJSON_CreateObject();
    cJSON_AddBoolToObject(display, "monochrome",
                          board_metadata.display_monochrome);
    cJSON_AddNumberToObject(display, "width", board_metadata.display_width);
    cJSON_AddNumberToObject(display, "height", board_metadata.display_height);
    cJSON_AddItemToObject(root, "display", display);
  }
}

}  // namespace

Ota::Ota(std::string ota_url) : ota_url_(std::move(ota_url)) {
  if (ota_url_.empty()) {
    Settings settings("wifi", false);
    ota_url_ = settings.GetString("ota_url");
  }
}

std::vector<int> Ota::ParseVersion(const std::string &version) {
  std::vector<int> values;
  std::stringstream ss(version);
  std::string segment;
  while (std::getline(ss, segment, '.')) {
    char *end = nullptr;
    const long parsed = std::strtol(segment.c_str(), &end, 10);
    if (end == segment.c_str() || (end != nullptr && *end != '\0')) {
      values.push_back(0);
    } else {
      values.push_back(static_cast<int>(parsed));
    }
  }
  return values;
}

bool Ota::IsNewVersionAvailable(const std::string &current_version,
                                const std::string &new_version) {
  const std::vector<int> current = ParseVersion(current_version);
  const std::vector<int> newer = ParseVersion(new_version);
  for (size_t i = 0; i < std::min(current.size(), newer.size()); ++i) {
    if (newer[i] > current[i]) {
      return true;
    }
    if (newer[i] < current[i]) {
      return false;
    }
  }
  return newer.size() > current.size();
}

std::string Ota::BuildSystemInfoJson() const {
  cJSON *root = cJSON_CreateObject();
  const esp_app_desc_t *app_desc = esp_app_get_description();
  cJSON_AddNumberToObject(root, "version", 2);
  cJSON_AddStringToObject(root, "language", GetLanguageCode());
  cJSON_AddNumberToObject(root, "flash_size",
                          static_cast<double>(get_flash_size()));
  cJSON_AddStringToObject(
      root, "minimum_free_heap_size",
      std::to_string(get_minimum_free_heap_size()).c_str());
  cJSON_AddStringToObject(root, "mac_address",
                          SystemIdentity::GetDeviceId().c_str());
  cJSON_AddStringToObject(root, "uuid",
                          SystemIdentity::GetOrCreateClientId().c_str());
  cJSON_AddStringToObject(root, "chip_model_name", CONFIG_IDF_TARGET);
  AddChipInfoJson(root);
  AddApplicationJson(root, app_desc);
  AddPartitionTableJson(root);
  AddRunningOtaJson(root);
  AddBoardMetadataJson(root);

  char *json = cJSON_PrintUnformatted(root);
  std::string payload = json != nullptr ? json : "{}";
  if (json != nullptr) {
    cJSON_free(json);
  }
  cJSON_Delete(root);
  return payload;
}

esp_err_t Ota::PerformJsonRequest(const char *method, const std::string &url,
                                  const std::string &payload,
                                  int *status_code,
                                  std::string *response) const {
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = activation_timeout_ms_;
  config.buffer_size = kHttpBufferSize;
  config.buffer_size_tx = kHttpBufferSize;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  esp_http_client_set_method(
      client, std::strcmp(method, "POST") == 0 ? HTTP_METHOD_POST
                                                 : HTTP_METHOD_GET);
  esp_http_client_set_header(
      client, "Activation-Version",
      SystemIdentity::GetActivationVersion() == 2 ? "2" : "1");
  esp_http_client_set_header(client, "Device-Id",
                             SystemIdentity::GetDeviceId().c_str());
  esp_http_client_set_header(client, "Client-Id",
                             SystemIdentity::GetOrCreateClientId().c_str());
  esp_http_client_set_header(client, "User-Agent",
                             SystemIdentity::GetUserAgent().c_str());
  esp_http_client_set_header(client, "Accept-Language", GetLanguageCode());
  esp_http_client_set_header(client, "Content-Type", "application/json");
  if (SystemIdentity::HasSerialNumber()) {
    esp_http_client_set_header(client, "Serial-Number",
                               SystemIdentity::GetSerialNumber().c_str());
  }
  if (!payload.empty()) {
    esp_http_client_set_post_field(client, payload.data(), payload.size());
  }

  esp_err_t err = esp_http_client_open(client, payload.empty() ? 0 : payload.size());
  if (err != ESP_OK) {
    esp_http_client_cleanup(client);
    return err;
  }

  if (!payload.empty()) {
    const int written =
        esp_http_client_write(client, payload.data(), payload.size());
    if (written < 0 || static_cast<size_t>(written) != payload.size()) {
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return ESP_FAIL;
    }
  }

  if (status_code != nullptr) {
    const int fetch_result = esp_http_client_fetch_headers(client);
    if (fetch_result < 0) {
      *status_code = esp_http_client_get_status_code(client);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return ESP_FAIL;
    }
    *status_code = esp_http_client_get_status_code(client);
  }

  response->clear();
  char buffer[kHttpBufferSize];
  while (true) {
    const int read = esp_http_client_read(client, buffer, sizeof(buffer));
    if (read < 0) {
      err = ESP_FAIL;
      break;
    }
    if (read == 0) {
      break;
    }
    response->append(buffer, read);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return err;
}

void Ota::PersistProtocolConfig(const cJSON *root, const char *section_name) {
  const cJSON *section = cJSON_GetObjectItem(root, section_name);
  if (!cJSON_IsObject(section)) {
    return;
  }

  Settings settings(section_name, true);
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, section) {
    if (cJSON_IsString(item)) {
      settings.SetString(item->string, item->valuestring);
    } else if (cJSON_IsNumber(item)) {
      settings.SetInt(item->string, item->valueint);
    } else if (cJSON_IsBool(item)) {
      settings.SetBool(item->string, cJSON_IsTrue(item));
    }
  }
}

void Ota::PersistServerTime(const cJSON *root) {
  const cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
  if (!cJSON_IsObject(server_time)) {
    return;
  }

  const cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
  const cJSON *timezone_offset =
      cJSON_GetObjectItem(server_time, "timezone_offset");
  if (!cJSON_IsNumber(timestamp)) {
    return;
  }

  double milliseconds = timestamp->valuedouble;
  if (cJSON_IsNumber(timezone_offset)) {
    milliseconds += timezone_offset->valueint * 60 * 1000;
  }

  struct timeval tv = {};
  tv.tv_sec = static_cast<time_t>(milliseconds / 1000);
  tv.tv_usec =
      static_cast<suseconds_t>(static_cast<long long>(milliseconds) % 1000) *
      1000;
  settimeofday(&tv, nullptr);
  has_server_time_ = true;
}

esp_err_t Ota::CheckVersion() {
  const esp_app_desc_t *app_desc = esp_app_get_description();
  current_version_ = app_desc != nullptr ? app_desc->version : "0.0.0";
  has_activation_code_ = false;
  has_activation_challenge_ = false;
  has_mqtt_config_ = false;
  has_websocket_config_ = false;
  has_new_version_ = false;
  has_server_time_ = false;

  int status_code = 0;
  std::string response;
  const std::string payload = BuildSystemInfoJson();
  const esp_err_t err =
      PerformJsonRequest("POST", ota_url_, payload, &status_code, &response);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "version check request failed: %s", esp_err_to_name(err));
    return err;
  }
  if (status_code != 200) {
    ESP_LOGE(kTag, "version check status=%d body=%s", status_code,
             response.c_str());
    return ESP_FAIL;
  }

  cJSON *root = cJSON_Parse(response.c_str());
  if (root == nullptr) {
    return ESP_ERR_INVALID_RESPONSE;
  }

  const cJSON *activation = cJSON_GetObjectItem(root, "activation");
  if (cJSON_IsObject(activation)) {
    const cJSON *message = cJSON_GetObjectItem(activation, "message");
    if (cJSON_IsString(message)) {
      activation_message_ = message->valuestring;
    }
    const cJSON *code = cJSON_GetObjectItem(activation, "code");
    if (cJSON_IsString(code)) {
      activation_code_ = code->valuestring;
      has_activation_code_ = true;
    }
    const cJSON *challenge = cJSON_GetObjectItem(activation, "challenge");
    if (cJSON_IsString(challenge)) {
      activation_challenge_ = challenge->valuestring;
      has_activation_challenge_ = true;
    }
    const cJSON *timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
    if (cJSON_IsNumber(timeout_ms)) {
      activation_timeout_ms_ = timeout_ms->valueint;
    }
  }

  PersistProtocolConfig(root, "mqtt");
  PersistProtocolConfig(root, "websocket");
  has_mqtt_config_ = cJSON_IsObject(cJSON_GetObjectItem(root, "mqtt"));
  has_websocket_config_ =
      cJSON_IsObject(cJSON_GetObjectItem(root, "websocket"));
  PersistServerTime(root);

  const cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
  if (cJSON_IsObject(firmware)) {
    const cJSON *version = cJSON_GetObjectItem(firmware, "version");
    const cJSON *url = cJSON_GetObjectItem(firmware, "url");
    const cJSON *force = cJSON_GetObjectItem(firmware, "force");
    if (cJSON_IsString(version)) {
      firmware_version_ = version->valuestring;
    }
    if (cJSON_IsString(url)) {
      firmware_url_ = url->valuestring;
    }
    if (cJSON_IsString(version) && cJSON_IsString(url)) {
      has_new_version_ =
          IsNewVersionAvailable(current_version_, firmware_version_);
      if (cJSON_IsNumber(force) && force->valueint == 1) {
        has_new_version_ = true;
      }
    }
  }

  cJSON_Delete(root);
  return ESP_OK;
}

std::string Ota::BuildActivateUrl() const { return JoinUrl(ota_url_, "activate"); }

esp_err_t Ota::Activate() {
  if (!has_activation_challenge_) {
    ESP_LOGW(kTag, "no activation challenge found");
    return ESP_FAIL;
  }

  int status_code = 0;
  std::string response;
  const std::string payload =
      SystemIdentity::BuildActivationPayload(activation_challenge_);
  const esp_err_t err = PerformJsonRequest("POST", BuildActivateUrl(), payload,
                                           &status_code, &response);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "activation request failed: %s", esp_err_to_name(err));
    return err;
  }
  if (status_code == 202) {
    return ESP_ERR_TIMEOUT;
  }
  if (status_code != 200) {
    ESP_LOGE(kTag, "activation status=%d body=%s", status_code,
             response.c_str());
    return ESP_FAIL;
  }
  return ESP_OK;
}

void Ota::MarkCurrentVersionValid() {
  const esp_partition_t *partition = esp_ota_get_running_partition();
  if (partition == nullptr || std::strcmp(partition->label, "factory") == 0) {
    return;
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
    return;
  }
  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

bool Ota::Upgrade(const std::string &firmware_url,
                  std::function<void(int progress, size_t speed)> callback) {
  esp_http_client_config_t config = {};
  config.url = firmware_url.c_str();
  config.timeout_ms = 30000;
  config.buffer_size = kHttpBufferSize;
  config.buffer_size_tx = kHttpBufferSize;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return false;
  }
  esp_http_client_set_method(client, HTTP_METHOD_GET);
  if (esp_http_client_open(client, 0) != ESP_OK) {
    esp_http_client_cleanup(client);
    return false;
  }
  if (esp_http_client_fetch_headers(client) < 0 ||
      esp_http_client_get_status_code(client) != 200) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
  if (partition == nullptr) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  esp_ota_handle_t update_handle = 0;
  if (esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle) !=
      ESP_OK) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  const int content_length = esp_http_client_get_content_length(client);
  std::unique_ptr<char, decltype(&heap_caps_free)> buffer(
      static_cast<char *>(heap_caps_malloc(kHttpBufferSize, MALLOC_CAP_INTERNAL)),
      &heap_caps_free);
  if (!buffer) {
    esp_ota_abort(update_handle);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  size_t total_read = 0;
  size_t recent_read = 0;
  int64_t last_report_time = esp_timer_get_time();
  while (true) {
    const int read = esp_http_client_read(client, buffer.get(), kHttpBufferSize);
    if (read < 0) {
      esp_ota_abort(update_handle);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    if (read == 0) {
      break;
    }
    if (esp_ota_write(update_handle, buffer.get(), read) != ESP_OK) {
      esp_ota_abort(update_handle);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    total_read += read;
    recent_read += read;
    const int64_t now = esp_timer_get_time();
    if (now - last_report_time >= 1000000 || total_read == content_length) {
      const int progress =
          content_length > 0 ? static_cast<int>((total_read * 100) / content_length)
                             : 0;
      if (callback) {
        callback(progress, recent_read);
      }
      last_report_time = now;
      recent_read = 0;
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (esp_ota_end(update_handle) != ESP_OK) {
    return false;
  }
  if (esp_ota_set_boot_partition(partition) != ESP_OK) {
    return false;
  }
  return true;
}

bool Ota::StartUpgrade(
    std::function<void(int progress, size_t speed)> callback) {
  return Upgrade(firmware_url_, std::move(callback));
}

}  // namespace official_chat
