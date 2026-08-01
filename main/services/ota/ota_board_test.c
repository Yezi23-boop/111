#include "services/ota/ota_board_test.h"

#include "sdkconfig.h"

#if CONFIG_OTA_SERVICE_BOARD_TEST

#include <stdint.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "services/network/network_service.h"
#include "services/ota/ota_service.h"

static const char *TAG = "ota_board_test";
static const uint32_t kTaskStackBytes = 4096U;
static const uint32_t kNetworkReadyTimeoutMs = 90000U;
/* 云端 Range 下载受公网 RTT 影响；这是仅测试镜像的等待上限，不改变产品 OTA
 * transport 的连接超时或取消语义。 */
static const uint32_t kStagingTimeoutMs = 900000U;

static TaskHandle_t s_task_handle = NULL;

/**
 * @brief 等待网络 owner 与 SNTP 完成，不在测试任务中直接碰 Wi-Fi 或时间服务。
 */
static bool ota_board_test_wait_network_ready(void)
{
    const TickType_t deadline = xTaskGetTickCount() +
                                 pdMS_TO_TICKS(kNetworkReadyTimeoutMs);
    while (xTaskGetTickCount() < deadline)
    {
        if (network_service_is_service_ready())
        {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
    return false;
}

/**
 * @brief 等待 OTA owner 公布 manifest 校验结果，避免测试任务轮询网络或 Flash。
 */
static bool ota_board_test_wait_manifest_result(void)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(30000U);
    while (xTaskGetTickCount() < deadline)
    {
        ota_service_snapshot_t snapshot = {0};
        if (ota_service_get_snapshot(&snapshot) == ESP_OK)
        {
            if (snapshot.manifest_valid)
            {
                return true;
            }
            if (snapshot.state == OTA_SERVICE_STATE_FAILED)
            {
                ESP_LOGE(TAG, "manifest rejected: %s",
                         esp_err_to_name(snapshot.last_error));
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200U));
    }
    ESP_LOGE(TAG, "manifest result timeout");
    return false;
}

#if CONFIG_OTA_SERVICE_BOARD_TEST_ONENET
/**
 * @brief 等待 OneNET CHECK 结果，确认任务已进入可下载状态。
 *
 * OneNET 的 task/tid 由 OTA owner 保存在其快照和上下文中；测试任务只读
 * 快照，避免越过 owner 直接创建 HTTP client 或操作 Flash。
 */
static bool ota_board_test_wait_onenet_result(void)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(30000U);
    while (xTaskGetTickCount() < deadline)
    {
        ota_service_snapshot_t snapshot = {0};
        if (ota_service_get_snapshot(&snapshot) == ESP_OK)
        {
            if (snapshot.state == OTA_SERVICE_STATE_READY &&
                snapshot.onenet_task_valid)
            {
                ESP_LOGI(TAG, "OneNET check ready: target=%s size=%u md5=%s",
                         snapshot.onenet_target_version,
                         (unsigned int)snapshot.onenet_image_size,
                         snapshot.onenet_md5);
                return true;
            }
            if (snapshot.state == OTA_SERVICE_STATE_NO_UPDATE ||
                snapshot.state == OTA_SERVICE_STATE_FAILED)
            {
                ESP_LOGE(TAG, "OneNET check rejected: state=%s err=%s",
                         ota_service_state_text(snapshot.state),
                         esp_err_to_name(snapshot.last_error));
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200U));
    }
    ESP_LOGE(TAG, "OneNET check timeout");
    return false;
}
#endif

/**
 * @brief 等待第二步写入和 SHA 校验完成，不把 STAGED 与启动槽切换混为一谈。
 */
static bool ota_board_test_wait_staged_result(void)
{
    const TickType_t deadline = xTaskGetTickCount() +
                                 pdMS_TO_TICKS(kStagingTimeoutMs);
    ota_service_snapshot_t last_snapshot = {0};
    uint8_t last_logged_bucket = 0U;
    while (xTaskGetTickCount() < deadline)
    {
        ota_service_snapshot_t snapshot = {0};
        if (ota_service_get_snapshot(&snapshot) == ESP_OK)
        {
            last_snapshot = snapshot;
            const uint8_t progress_bucket = snapshot.progress_percent / 10U;
            if (progress_bucket > last_logged_bucket)
            {
                last_logged_bucket = progress_bucket;
                ESP_LOGI(TAG, "download progress: %u%% (%u/%u bytes)",
                         (unsigned int)snapshot.progress_percent,
                         (unsigned int)snapshot.bytes_received,
                         (unsigned int)snapshot.image_size);
            }
            if (snapshot.state == OTA_SERVICE_STATE_STAGED)
            {
                return true;
            }
            if (snapshot.state == OTA_SERVICE_STATE_FAILED)
            {
                ESP_LOGE(TAG, "download rejected: %s",
                         esp_err_to_name(snapshot.last_error));
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200U));
    }
    ESP_LOGE(TAG, "download staging timeout: progress=%u%% bytes=%u/%u",
             (unsigned int)last_snapshot.progress_percent,
             (unsigned int)last_snapshot.bytes_received,
             (unsigned int)last_snapshot.image_size);
    return false;
}

/** @brief 串行提交板端专用检查、下载与激活命令。 */
static void ota_board_test_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(CONFIG_OTA_SERVICE_BOARD_TEST_START_DELAY_MS));

    if (!ota_board_test_wait_network_ready())
    {
        ESP_LOGE(TAG, "network/TLS time not ready; OTA test not started");
        goto done;
    }

#if CONFIG_OTA_SERVICE_BOARD_TEST_ONENET
    esp_err_t ret = ota_service_request_onenet_check();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "OneNET check command rejected: %s",
                 esp_err_to_name(ret));
        goto done;
    }
    if (!ota_board_test_wait_onenet_result())
    {
        goto done;
    }
#else
    /* 板测也只复用产品云端 manifest；设备不再支持局域网 OTA。 */
    esp_err_t ret = ota_service_request_remote_manifest();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "manifest command rejected: %s", esp_err_to_name(ret));
        goto done;
    }
    if (!ota_board_test_wait_manifest_result())
    {
        goto done;
    }
#endif

    ret = ota_service_request_start();
    ESP_LOGI(TAG, "download command submitted: %s", esp_err_to_name(ret));
    if (ret != ESP_OK || !ota_board_test_wait_staged_result())
    {
        /* 下载超时也必须让 owner 主动退出维护会话，避免测试 task 消失后留下
         * 未完成的 HTTPS/Flash handle。 */
        (void)ota_service_request_cancel();
        goto done;
    }

    ret = ota_service_request_activate();
    ESP_LOGI(TAG, "activate command submitted: %s", esp_err_to_name(ret));

done:
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t ota_board_test_start(void)
{
    if (s_task_handle != NULL)
    {
        return ESP_OK;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL || running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0)
    {
        ESP_LOGI(TAG,
                 "skip standalone OTA board test: running partition is not ota_0");
        return ESP_OK;
    }

    const BaseType_t created = xTaskCreateWithCaps(
        ota_board_test_task, "ota_board_test", kTaskStackBytes, NULL, 3,
        &s_task_handle, MALLOC_CAP_SPIRAM);
    if (created != pdPASS)
    {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "standalone OTA board test task created");
    return ESP_OK;
}

#else

esp_err_t ota_board_test_start(void)
{
    return ESP_OK;
}

#endif
