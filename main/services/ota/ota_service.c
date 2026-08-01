#include "services/ota/ota_service.h"

#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "services/power/power_policy.h"
#include "services/ota/onenet_ota_provider.h"
#include "services/network/network_service.h"
#include "services/runtime/runtime_coordinator.h"
#include "services/runtime/startup_readiness.h"
#include "services/time/system_time_service.h"

static const char *TAG = "ota_service";

enum
{
    kCommandQueueLength = 4,
    /* OneNET HTTPS/TLS plus the maintenance-window handoff can exceed 8 KiB. */
    kTaskStackBytes = 16384,
};

typedef enum
{
    OTA_SERVICE_COMMAND_PREPARE = 0,
    OTA_SERVICE_COMMAND_CANCEL,
    OTA_SERVICE_COMMAND_FETCH_MANIFEST,
    OTA_SERVICE_COMMAND_CHECK_ONENET,
    OTA_SERVICE_COMMAND_START_DOWNLOAD,
    OTA_SERVICE_COMMAND_ACTIVATE,
    OTA_SERVICE_COMMAND_COORDINATOR_GRANTED,
    OTA_SERVICE_COMMAND_COORDINATOR_QUIESCE,
    OTA_SERVICE_COMMAND_COORDINATOR_CANCELLED,
} ota_service_command_t;

typedef struct
{
    ota_service_command_t type;
    uint32_t generation;
    esp_err_t result;
} ota_service_command_message_t;

typedef struct
{
    StaticQueue_t queue_buffer;
    uint8_t queue_storage[sizeof(ota_service_command_message_t) *
                          kCommandQueueLength];
    QueueHandle_t queue;
    TaskHandle_t task;
    portMUX_TYPE lock;
    ota_service_snapshot_t snapshot;
    bool coordinator_granted;
    bool start_after_grant;
    bool power_maintenance_active;
    bool use_cert_bundle;
    bool staged;
    bool cancel_requested;
    char manifest_url[OTA_TRANSPORT_URL_MAX];
    char allowed_host[96];
    char current_version[OTA_TRANSPORT_VERSION_MAX];
    char onenet_authorization[ONENET_OTA_AUTHORIZATION_MAX];
    const char *root_ca_pem;
    ota_transport_manifest_t manifest;
    bool manifest_valid;
    bool onenet_task_valid;
    onenet_ota_task_t onenet_task;
    ota_transport_fault_mode_t fault_mode;
    uint32_t restart_hold_ms;
    uint32_t coordinator_request_generation;
    uint32_t coordinator_quiesce_generation;
    bool initialized;
} ota_service_context_t;

static ota_service_context_t s_ota = {
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

const char *ota_service_state_text(ota_service_state_t state)
{
    switch (state)
    {
    case OTA_SERVICE_STATE_IDLE:
        return "IDLE";
    case OTA_SERVICE_STATE_PREPARING:
        return "PREPARING";
    case OTA_SERVICE_STATE_QUIESCING:
        return "QUIESCING";
    case OTA_SERVICE_STATE_QUIESCED:
        return "QUIESCED";
    case OTA_SERVICE_STATE_READY:
        return "READY";
    case OTA_SERVICE_STATE_NO_UPDATE:
        return "NO_UPDATE";
    case OTA_SERVICE_STATE_DOWNLOADING:
        return "DOWNLOADING";
    case OTA_SERVICE_STATE_STAGED:
        return "STAGED";
    case OTA_SERVICE_STATE_VERIFYING:
        return "VERIFYING";
    case OTA_SERVICE_STATE_RESTARTING:
        return "RESTARTING";
    case OTA_SERVICE_STATE_FAILED:
        return "FAILED";
    case OTA_SERVICE_STATE_PENDING_VERIFY:
        return "PENDING_VERIFY";
    case OTA_SERVICE_STATE_VALID:
        return "VALID";
    case OTA_SERVICE_STATE_ROLLED_BACK:
        return "ROLLED_BACK";
    default:
        return "UNKNOWN";
    }
}

static void ota_service_publish_state(ota_service_state_t state,
                                      esp_err_t error)
{
    taskENTER_CRITICAL(&s_ota.lock);
    s_ota.snapshot.state = state;
    s_ota.snapshot.last_error = error;
    s_ota.snapshot.maintenance_active = s_ota.coordinator_granted;
    taskEXIT_CRITICAL(&s_ota.lock);
    ESP_LOGI(TAG, "state=%s err=%s maintenance=%d",
             ota_service_state_text(state), esp_err_to_name(error),
             s_ota.coordinator_granted ? 1 : 0);
}

static void ota_service_cleanup_maintenance(void)
{
    if (s_ota.staged || ota_transport_has_staged_image())
    {
        const esp_err_t abort_ret = ota_transport_abort_staging();
        if (abort_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "staged OTA abort failed: %s",
                     esp_err_to_name(abort_ret));
        }
        s_ota.staged = false;
    }

    if (s_ota.power_maintenance_active)
    {
        const esp_err_t power_ret =
            power_policy_set_maintenance_window(false, "ota");
        if (power_ret != ESP_OK)
        {
            ESP_LOGW(TAG, "power maintenance cleanup failed: err=%s",
                     esp_err_to_name(power_ret));
        }
        s_ota.power_maintenance_active = false;
    }

    taskENTER_CRITICAL(&s_ota.lock);
    s_ota.snapshot.maintenance_active = false;
    taskEXIT_CRITICAL(&s_ota.lock);
}

static esp_err_t ota_service_prepare(void)
{
    ota_service_publish_state(OTA_SERVICE_STATE_PREPARING, ESP_OK);

    esp_err_t ret = power_policy_set_maintenance_window(true, "ota");
    if (ret != ESP_OK)
    {
        ota_service_cleanup_maintenance();
        ota_service_publish_state(OTA_SERVICE_STATE_FAILED, ret);
        return ret;
    }
    s_ota.power_maintenance_active = true;

    ota_service_publish_state(OTA_SERVICE_STATE_QUIESCED, ESP_OK);

    /* OTA 只发布强前台 owner 事实；业务 owner 观测该事实后自行让路。 */
    ota_service_publish_state(OTA_SERVICE_STATE_READY, ESP_OK);
    return ESP_OK;
}

static void ota_service_progress_cb(size_t received, size_t total,
                                    void *user_ctx)
{
    (void)user_ctx;
    taskENTER_CRITICAL(&s_ota.lock);
    s_ota.snapshot.bytes_received = received;
    s_ota.snapshot.image_size = total;
    s_ota.snapshot.progress_percent =
        total == 0U ? 0U : (uint8_t)((received * 100U) / total);
    taskEXIT_CRITICAL(&s_ota.lock);
}

static bool ota_service_cancel_cb(void *user_ctx)
{
    (void)user_ctx;
    ota_service_command_message_t command = {
        .type = OTA_SERVICE_COMMAND_CANCEL,
    };
    while (xQueueReceive(s_ota.queue, &command, 0) == pdTRUE)
    {
        if (command.type == OTA_SERVICE_COMMAND_CANCEL)
        {
            s_ota.cancel_requested = true;
        }
    }
    return s_ota.cancel_requested;
}

static void ota_service_report_pending(void);

static esp_err_t ota_service_fetch_manifest(void)
{
    taskENTER_CRITICAL(&s_ota.lock);
    s_ota.onenet_task_valid = false;
    s_ota.snapshot.onenet_task_valid = false;
    s_ota.snapshot.onenet_target_version[0] = '\0';
    s_ota.snapshot.onenet_image_size = 0U;
    s_ota.snapshot.onenet_md5[0] = '\0';
    taskEXIT_CRITICAL(&s_ota.lock);
    ota_service_publish_state(OTA_SERVICE_STATE_PREPARING, ESP_OK);
    const ota_transport_manifest_request_t request = {
        .manifest_url = s_ota.manifest_url,
        .root_ca_pem = s_ota.root_ca_pem,
        .use_cert_bundle = s_ota.use_cert_bundle,
        .allowed_host = s_ota.allowed_host,
        .current_version = s_ota.current_version,
    };
    ota_transport_manifest_t manifest = {0};
    const esp_err_t ret =
        ota_transport_fetch_manifest(&request, &manifest);
    if (ret != ESP_OK)
    {
        taskENTER_CRITICAL(&s_ota.lock);
        s_ota.manifest_valid = false;
        s_ota.snapshot.manifest_valid = false;
        taskEXIT_CRITICAL(&s_ota.lock);
        ota_service_publish_state(OTA_SERVICE_STATE_FAILED, ret);
        return ret;
    }

    manifest.checksum_type = OTA_TRANSPORT_CHECKSUM_SHA256;
    taskENTER_CRITICAL(&s_ota.lock);
    s_ota.snapshot.onenet_task_valid = false;
    s_ota.snapshot.onenet_target_version[0] = '\0';
    s_ota.snapshot.onenet_image_size = 0U;
    s_ota.snapshot.onenet_md5[0] = '\0';
    s_ota.manifest = manifest;
    s_ota.manifest_valid = true;
    s_ota.snapshot.manifest_valid = true;
    s_ota.snapshot.manifest_size = manifest.size;
    strncpy(s_ota.snapshot.manifest_version, manifest.version,
            sizeof(s_ota.snapshot.manifest_version) - 1U);
    strncpy(s_ota.snapshot.manifest_sha256, manifest.sha256,
            sizeof(s_ota.snapshot.manifest_sha256) - 1U);
    taskEXIT_CRITICAL(&s_ota.lock);
    ota_service_publish_state(OTA_SERVICE_STATE_READY, ESP_OK);
    return ESP_OK;
}

static esp_err_t ota_service_check_onenet(void)
{
    ota_service_report_pending();
    const esp_app_desc_t *description = esp_app_get_description();
    if (description == NULL || description->version[0] == '\0')
    {
        ota_service_publish_state(OTA_SERVICE_STATE_FAILED,
                                  ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&s_ota.lock);
    s_ota.manifest_valid = false;
    s_ota.onenet_task_valid = false;
    s_ota.onenet_authorization[0] = '\0';
    s_ota.snapshot.manifest_valid = false;
    s_ota.snapshot.onenet_task_valid = false;
    s_ota.snapshot.onenet_target_version[0] = '\0';
    s_ota.snapshot.onenet_image_size = 0U;
    s_ota.snapshot.onenet_md5[0] = '\0';
    taskEXIT_CRITICAL(&s_ota.lock);
    ota_service_publish_state(OTA_SERVICE_STATE_PREPARING, ESP_OK);
    onenet_ota_task_t task = {0};
    esp_err_t ret = onenet_ota_provider_report_version(description->version);
    if (ret == ESP_OK)
    {
        ret = onenet_ota_provider_check(description->version, &task);
    }
    if (ret == ESP_ERR_NOT_FOUND)
    {
        taskENTER_CRITICAL(&s_ota.lock);
        s_ota.manifest_valid = false;
        s_ota.onenet_task_valid = false;
        s_ota.snapshot.manifest_valid = false;
        s_ota.snapshot.onenet_task_valid = false;
        taskEXIT_CRITICAL(&s_ota.lock);
        ota_service_publish_state(OTA_SERVICE_STATE_NO_UPDATE, ret);
        return ret;
    }
    if (ret != ESP_OK)
    {
        taskENTER_CRITICAL(&s_ota.lock);
        s_ota.snapshot.onenet_task_valid = false;
        s_ota.onenet_task_valid = false;
        taskEXIT_CRITICAL(&s_ota.lock);
        ota_service_publish_state(OTA_SERVICE_STATE_FAILED, ret);
        return ret;
    }

    char download_url[OTA_TRANSPORT_URL_MAX] = {0};
    char authorization[ONENET_OTA_AUTHORIZATION_MAX] = {0};
    ret = onenet_ota_provider_prepare_download(
        &task, download_url, sizeof(download_url), authorization,
        sizeof(authorization));
    if (ret != ESP_OK)
    {
        ota_service_publish_state(OTA_SERVICE_STATE_FAILED, ret);
        return ret;
    }

    ota_transport_manifest_t manifest = {0};
    strncpy(manifest.version, task.target, sizeof(manifest.version) - 1U);
    strncpy(manifest.url, download_url, sizeof(manifest.url) - 1U);
    manifest.size = task.size;
    manifest.checksum_type = OTA_TRANSPORT_CHECKSUM_MD5;
    strncpy(manifest.md5, task.md5, sizeof(manifest.md5) - 1U);

    taskENTER_CRITICAL(&s_ota.lock);
    s_ota.use_cert_bundle = true;
    s_ota.root_ca_pem = NULL;
    s_ota.manifest = manifest;
    s_ota.manifest_valid = true;
    s_ota.onenet_task_valid = true;
    s_ota.onenet_task = task;
    s_ota.snapshot.manifest_valid = true;
    s_ota.snapshot.manifest_size = task.size;
    strncpy(s_ota.snapshot.manifest_version, task.target,
            sizeof(s_ota.snapshot.manifest_version) - 1U);
    s_ota.snapshot.manifest_sha256[0] = '\0';
    strncpy(s_ota.onenet_authorization, authorization,
            sizeof(s_ota.onenet_authorization) - 1U);
    s_ota.snapshot.onenet_task_valid = true;
    strncpy(s_ota.snapshot.onenet_target_version, task.target,
            sizeof(s_ota.snapshot.onenet_target_version) - 1U);
    s_ota.snapshot.onenet_image_size = task.size;
    strncpy(s_ota.snapshot.onenet_md5, task.md5,
            sizeof(s_ota.snapshot.onenet_md5) - 1U);
    taskEXIT_CRITICAL(&s_ota.lock);
    ota_service_publish_state(OTA_SERVICE_STATE_READY, ESP_OK);
    ESP_LOGI(TAG, "OneNET task ready: target=%s size=%u tid=%u",
             task.target, (unsigned int)task.size,
             (unsigned int)task.task_id);
    return ESP_OK;
}

static esp_err_t ota_service_download_to_staging(void)
{
    if (!s_ota.manifest_valid)
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_ota.cancel_requested = false;
    ota_service_publish_state(OTA_SERVICE_STATE_DOWNLOADING, ESP_OK);
    const ota_transport_download_config_t config = {
        .root_ca_pem = s_ota.root_ca_pem,
        .use_cert_bundle = s_ota.use_cert_bundle,
        .authorization = s_ota.onenet_task_valid
                             ? s_ota.onenet_authorization
                             : NULL,
        .progress_cb = ota_service_progress_cb,
        .cancel_cb = ota_service_cancel_cb,
        .user_ctx = NULL,
        .fault_mode = s_ota.fault_mode,
    };
    esp_err_t ret = ota_transport_download_to_staging(&s_ota.manifest, &config);
    if (ret != ESP_OK)
    {
        ota_service_cleanup_maintenance();
        ota_service_publish_state(
            s_ota.cancel_requested ? OTA_SERVICE_STATE_IDLE
                                    : OTA_SERVICE_STATE_FAILED,
            ret);
        return ret;
    }

    s_ota.staged = true;
    ota_service_publish_state(OTA_SERVICE_STATE_STAGED, ESP_OK);
    return ESP_OK;
}

static void ota_service_report_pending(void)
{
    onenet_ota_pending_t pending = {0};
    esp_err_t ret = onenet_ota_provider_load_pending(&pending);
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        return;
    }
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "OneNET pending load failed: %s", esp_err_to_name(ret));
        return;
    }

    for (unsigned int attempt = 0U;
         attempt < 30U && !network_service_is_service_ready(); ++attempt)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
    if (!network_service_is_service_ready() ||
        system_time_service_ensure_valid_for_tls(5000U) != ESP_OK)
    {
        ESP_LOGW(TAG, "OneNET pending deferred: network or TLS time unavailable");
        return;
    }

    const esp_app_desc_t *description = esp_app_get_description();
    if (description == NULL || description->version[0] == '\0')
    {
        return;
    }
    const bool booted_target = strcmp(description->version, pending.target) == 0;
    ret = onenet_ota_provider_report_version(description->version);
    if (ret == ESP_OK)
    {
        ret = onenet_ota_provider_report_status(pending.task_id,
                                                booted_target ? 100 : 0);
    }
    if (ret == ESP_OK && booted_target)
    {
        ret = onenet_ota_provider_clear_pending();
    }
    ESP_LOGI(TAG, "OneNET pending report: tid=%u target=%s booted=%d result=%s",
             (unsigned int)pending.task_id, pending.target,
             booted_target ? 1 : 0, esp_err_to_name(ret));
}

static esp_err_t ota_service_activate_staging(void)
{
    if (!s_ota.staged || !ota_transport_has_staged_image())
    {
        ota_service_publish_state(OTA_SERVICE_STATE_FAILED, ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    ota_service_publish_state(OTA_SERVICE_STATE_VERIFYING, ESP_OK);
    if (s_ota.onenet_task_valid)
    {
        const esp_err_t pending_ret =
            onenet_ota_provider_store_pending(&s_ota.onenet_task);
        if (pending_ret != ESP_OK)
        {
            ota_service_cleanup_maintenance();
            ota_service_publish_state(OTA_SERVICE_STATE_FAILED, pending_ret);
            return pending_ret;
        }
    }
    const esp_err_t ret = ota_transport_activate_staging();
    s_ota.staged = false;
    if (ret != ESP_OK)
    {
        ota_service_cleanup_maintenance();
        ota_service_publish_state(OTA_SERVICE_STATE_FAILED, ret);
        return ret;
    }

    ota_service_publish_state(OTA_SERVICE_STATE_RESTARTING, ESP_OK);
    ESP_LOGW(TAG, "fault_window: finish_succeeded_before_restart hold_ms=%u",
             (unsigned int)s_ota.restart_hold_ms);
    if (s_ota.restart_hold_ms > 0U)
    {
        vTaskDelay(pdMS_TO_TICKS(s_ota.restart_hold_ms));
    }
    esp_restart();
    return ESP_FAIL;
}

static esp_err_t ota_service_send_coordinator_command(
    ota_service_command_t type, uint32_t generation, esp_err_t result)
{
    if (s_ota.queue == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    const ota_service_command_message_t command = {
        .type = type,
        .generation = generation,
        .result = result,
    };
    return xQueueSend(s_ota.queue, &command, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

static esp_err_t ota_service_coordinator_grant(uint32_t generation,
                                                void *user_ctx)
{
    (void)user_ctx;
    return ota_service_send_coordinator_command(
        OTA_SERVICE_COMMAND_COORDINATOR_GRANTED, generation, ESP_OK);
}

static esp_err_t ota_service_coordinator_cancel(uint32_t generation,
                                                 esp_err_t reason,
                                                 void *user_ctx)
{
    (void)user_ctx;
    return ota_service_send_coordinator_command(
        OTA_SERVICE_COMMAND_COORDINATOR_CANCELLED, generation, reason);
}

static esp_err_t ota_service_coordinator_quiesce(uint32_t generation,
                                                  void *user_ctx)
{
    (void)user_ctx;
    ota_service_state_t state = OTA_SERVICE_STATE_IDLE;
    taskENTER_CRITICAL(&s_ota.lock);
    state = s_ota.snapshot.state;
    taskEXIT_CRITICAL(&s_ota.lock);

    /* 自动交接不能冒充用户取消正在写 Flash 的 OTA 会话。 */
    if (state == OTA_SERVICE_STATE_DOWNLOADING ||
        state == OTA_SERVICE_STATE_VERIFYING ||
        state == OTA_SERVICE_STATE_RESTARTING)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return ota_service_send_coordinator_command(
        OTA_SERVICE_COMMAND_COORDINATOR_QUIESCE, generation, ESP_OK);
}

static void ota_service_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "wait: ui_first_frame_ready");
    (void)startup_readiness_wait_ui_first_frame(portMAX_DELAY);
    ESP_LOGI(TAG, "ready: ui_first_frame_ready");

    /* 只初始化并持久化 OneNET 默认凭据，不在启动阶段发起网络请求。 */
    const esp_err_t onenet_init_ret = onenet_ota_provider_init();
    if (onenet_init_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "OneNET credentials unavailable: %s",
                 esp_err_to_name(onenet_init_ret));
    }
    ota_service_report_pending();

    while (true)
    {
        ota_service_command_message_t command = {
            .type = OTA_SERVICE_COMMAND_CANCEL,
        };
        if (xQueueReceive(s_ota.queue, &command, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        if (command.type == OTA_SERVICE_COMMAND_PREPARE ||
            command.type == OTA_SERVICE_COMMAND_START_DOWNLOAD)
        {
            if (s_ota.coordinator_granted ||
                s_ota.coordinator_request_generation != 0U)
            {
                ESP_LOGW(TAG, "prepare ignored: maintenance already active");
                continue;
            }
            if (command.type == OTA_SERVICE_COMMAND_START_DOWNLOAD &&
                !s_ota.manifest_valid)
            {
                ota_service_publish_state(OTA_SERVICE_STATE_FAILED,
                                          ESP_ERR_INVALID_STATE);
                continue;
            }
            s_ota.start_after_grant =
                command.type == OTA_SERVICE_COMMAND_START_DOWNLOAD;
            uint32_t request_generation = 0U;
            const esp_err_t request_ret =
                runtime_coordinator_request_foreground(
                    RUNTIME_COORDINATOR_PARTICIPANT_OTA,
                    &request_generation);
            if (request_ret != ESP_OK)
            {
                ota_service_publish_state(OTA_SERVICE_STATE_FAILED,
                                          request_ret);
                continue;
            }
            s_ota.coordinator_request_generation = request_generation;
            ota_service_publish_state(OTA_SERVICE_STATE_QUIESCING, ESP_OK);
        }
        else if (command.type == OTA_SERVICE_COMMAND_COORDINATOR_GRANTED)
        {
            if (command.generation !=
                s_ota.coordinator_request_generation)
            {
                (void)runtime_coordinator_report_start_result(
                    RUNTIME_COORDINATOR_PARTICIPANT_OTA,
                    command.generation, ESP_ERR_INVALID_STATE);
                continue;
            }
            s_ota.coordinator_granted = true;
            const esp_err_t prepare_ret = ota_service_prepare();
            (void)runtime_coordinator_report_start_result(
                RUNTIME_COORDINATOR_PARTICIPANT_OTA,
                command.generation, prepare_ret);
            if (prepare_ret == ESP_OK && s_ota.start_after_grant)
            {
                (void)ota_service_download_to_staging();
                if (!s_ota.staged)
                {
                    (void)runtime_coordinator_cancel_request(
                        RUNTIME_COORDINATOR_PARTICIPANT_OTA,
                        s_ota.coordinator_request_generation);
                }
            }
        }
        else if (command.type == OTA_SERVICE_COMMAND_COORDINATOR_QUIESCE)
        {
            s_ota.coordinator_quiesce_generation = command.generation;
            ota_service_cleanup_maintenance();
            s_ota.coordinator_granted = false;
            s_ota.coordinator_request_generation = 0U;
            s_ota.start_after_grant = false;
            ota_service_publish_state(OTA_SERVICE_STATE_IDLE, ESP_OK);
            (void)runtime_coordinator_report_quiesce_result(
                RUNTIME_COORDINATOR_PARTICIPANT_OTA,
                s_ota.coordinator_quiesce_generation, ESP_OK);
            s_ota.coordinator_quiesce_generation = 0U;
        }
        else if (command.type == OTA_SERVICE_COMMAND_COORDINATOR_CANCELLED)
        {
            if (command.generation ==
                s_ota.coordinator_request_generation)
            {
                s_ota.coordinator_request_generation = 0U;
                s_ota.start_after_grant = false;
                ota_service_publish_state(OTA_SERVICE_STATE_IDLE,
                                          command.result);
            }
        }
        else if (command.type == OTA_SERVICE_COMMAND_FETCH_MANIFEST)
        {
            (void)ota_service_fetch_manifest();
        }
        else if (command.type == OTA_SERVICE_COMMAND_CHECK_ONENET)
        {
            (void)ota_service_check_onenet();
        }
        else if (command.type == OTA_SERVICE_COMMAND_ACTIVATE)
        {
            (void)ota_service_activate_staging();
        }
        else if (command.type == OTA_SERVICE_COMMAND_CANCEL)
        {
            if (s_ota.coordinator_request_generation != 0U)
            {
                (void)runtime_coordinator_cancel_request(
                    RUNTIME_COORDINATOR_PARTICIPANT_OTA,
                    s_ota.coordinator_request_generation);
            }
            else if (s_ota.coordinator_granted ||
                     s_ota.power_maintenance_active)
            {
                ota_service_cleanup_maintenance();
                s_ota.coordinator_granted = false;
                ota_service_publish_state(OTA_SERVICE_STATE_IDLE, ESP_OK);
            }
        }
    }
}

esp_err_t ota_service_start(void)
{
    taskENTER_CRITICAL(&s_ota.lock);
    if (s_ota.initialized)
    {
        taskEXIT_CRITICAL(&s_ota.lock);
        return ESP_OK;
    }
    taskEXIT_CRITICAL(&s_ota.lock);

    if (startup_readiness_init() != ESP_OK)
    {
        return ESP_FAIL;
    }

    s_ota.queue = xQueueCreateStatic(
        kCommandQueueLength, sizeof(ota_service_command_message_t),
        s_ota.queue_storage, &s_ota.queue_buffer);
    if (s_ota.queue == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    const runtime_coordinator_participant_config_t participant = {
        .id = RUNTIME_COORDINATOR_PARTICIPANT_OTA,
        .name = "ota",
        .capabilities = RUNTIME_COORDINATOR_CAPABILITY_FOREGROUND_EXCLUSIVE,
        .request_quiesce = ota_service_coordinator_quiesce,
        .grant_foreground = ota_service_coordinator_grant,
        .cancel_pending_request = ota_service_coordinator_cancel,
    };
    const esp_err_t register_ret = runtime_coordinator_register(&participant);
    if (register_ret != ESP_OK)
    {
        return register_ret;
    }

    /* Flash 写入期间 cache 会被冻结；执行 OTA 下载的任务栈必须留在
     * internal RAM，不能像普通网络后台任务一样放到 PSRAM。 */
    const BaseType_t task_ret = xTaskCreateWithCaps(
        ota_service_task, "ota_service", kTaskStackBytes, NULL, 5,
        &s_ota.task, MALLOC_CAP_INTERNAL);
    if (task_ret != pdPASS)
    {
        s_ota.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    taskENTER_CRITICAL(&s_ota.lock);
    s_ota.initialized = true;
    s_ota.snapshot.state = OTA_SERVICE_STATE_IDLE;
    s_ota.snapshot.last_error = ESP_OK;
    s_ota.snapshot.task_started = true;
    taskEXIT_CRITICAL(&s_ota.lock);
#if CONFIG_OTA_SERVICE_TEST_FAULT_MODE > 0
    s_ota.fault_mode = (ota_transport_fault_mode_t)
        CONFIG_OTA_SERVICE_TEST_FAULT_MODE;
#endif
#if CONFIG_OTA_SERVICE_TEST_RESTART_HOLD_MS > 0
    s_ota.restart_hold_ms = CONFIG_OTA_SERVICE_TEST_RESTART_HOLD_MS;
#endif
    ESP_LOGI(TAG, "started");
    return ESP_OK;
}

static esp_err_t ota_service_send_command(ota_service_command_t command)
{
    if (s_ota.queue == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    const ota_service_command_message_t message = {
        .type = command,
    };
    return xQueueSend(s_ota.queue, &message, 0) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t ota_service_request_prepare(void)
{
    return ota_service_send_command(OTA_SERVICE_COMMAND_PREPARE);
}

esp_err_t ota_service_request_manifest(
    const ota_transport_manifest_request_t *config)
{
    if (config == NULL || config->manifest_url == NULL ||
        (!config->use_cert_bundle &&
         (config->root_ca_pem == NULL || config->root_ca_pem[0] == '\0')) ||
        config->allowed_host == NULL ||
        config->current_version == NULL ||
        strlen(config->manifest_url) >= sizeof(s_ota.manifest_url) ||
        strlen(config->allowed_host) >= sizeof(s_ota.allowed_host) ||
        strlen(config->current_version) >= sizeof(s_ota.current_version))
    {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_ota.manifest_url, config->manifest_url,
            sizeof(s_ota.manifest_url) - 1U);
    strncpy(s_ota.allowed_host, config->allowed_host,
            sizeof(s_ota.allowed_host) - 1U);
    strncpy(s_ota.current_version, config->current_version,
            sizeof(s_ota.current_version) - 1U);
    s_ota.root_ca_pem = config->root_ca_pem;
    s_ota.use_cert_bundle = config->use_cert_bundle;
    return ota_service_send_command(OTA_SERVICE_COMMAND_FETCH_MANIFEST);
}

esp_err_t ota_service_request_remote_manifest(void)
{
#if CONFIG_OTA_SERVICE_REMOTE_ENABLED
    const esp_app_desc_t *description = esp_app_get_description();
    if (description == NULL || description->version[0] == '\0')
    {
        return ESP_ERR_INVALID_STATE;
    }
    const ota_transport_manifest_request_t config = {
        .manifest_url = CONFIG_OTA_SERVICE_REMOTE_MANIFEST_URL,
        .root_ca_pem = NULL,
        .use_cert_bundle = true,
        .allowed_host = CONFIG_OTA_SERVICE_REMOTE_ALLOWED_HOST,
        .current_version = description->version,
    };
    return ota_service_request_manifest(&config);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t ota_service_request_onenet_check(void)
{
    return ota_service_send_command(OTA_SERVICE_COMMAND_CHECK_ONENET);
}

esp_err_t ota_service_request_start(void)
{
    return ota_service_send_command(OTA_SERVICE_COMMAND_START_DOWNLOAD);
}

esp_err_t ota_service_request_activate(void)
{
    return ota_service_send_command(OTA_SERVICE_COMMAND_ACTIVATE);
}

esp_err_t ota_service_set_fault_mode(ota_transport_fault_mode_t mode)
{
    if (mode > OTA_TRANSPORT_FAULT_ABORT_AT_90_PERCENT ||
        s_ota.coordinator_granted)
    {
        return ESP_ERR_INVALID_ARG;
    }
    s_ota.fault_mode = mode;
    return ESP_OK;
}

esp_err_t ota_service_set_restart_hold_ms(uint32_t hold_ms)
{
    if (hold_ms > 30000U || s_ota.coordinator_granted)
    {
        return ESP_ERR_INVALID_ARG;
    }
    s_ota.restart_hold_ms = hold_ms;
    return ESP_OK;
}

esp_err_t ota_service_request_cancel(void)
{
    return ota_service_send_command(OTA_SERVICE_COMMAND_CANCEL);
}

esp_err_t ota_service_get_snapshot(ota_service_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_ota.lock);
    *out_snapshot = s_ota.snapshot;
    out_snapshot->maintenance_active = s_ota.coordinator_granted;
    taskEXIT_CRITICAL(&s_ota.lock);
    return ESP_OK;
}
