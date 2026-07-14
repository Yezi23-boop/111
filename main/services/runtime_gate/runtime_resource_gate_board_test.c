#include "services/runtime_gate/runtime_resource_gate_board_test.h"

#include "sdkconfig.h"

#if CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "services/safety/background_service_manager.h"
#include "services/runtime_gate/foreground_runtime_gate.h"
#include "services/memory_watch/memory_watch_service.h"
#include "services/network/network_service.h"

static const char *TAG = "runtime_gate_test";
static const uint32_t kTaskStackBytes = 4096;
static TaskHandle_t s_test_task_handle = NULL;

#if CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_ENABLE_REAL_BLE_TOGGLE
static const UBaseType_t kTaskStackCaps = MALLOC_CAP_INTERNAL;
#else
static const UBaseType_t kTaskStackCaps = MALLOC_CAP_SPIRAM;
#endif

/**
 * @brief 打印后台管理器对 Safety Monitor / ESP-DL 的当前裁决。
 */
static void runtime_resource_gate_board_test_log_background_snapshot(
    const char *label)
{
    const background_service_manager_snapshot_t snapshot =
        background_service_manager_get_snapshot();
    ESP_LOGI(TAG,
             "%s: danger_should_run=%d running=%d block=%d fg_runtime=%d fg_audio=%d last_error=%s",
             label,
             snapshot.danger_should_run,
             snapshot.danger_runtime_running,
             snapshot.danger_block_reason,
             snapshot.danger_blocked_by_foreground_runtime,
             snapshot.danger_blocked_by_foreground_audio,
             esp_err_to_name(snapshot.last_error));
}

/**
 * @brief 记录一次 gate 调用结果，便于串口日志 grep。
 */
static void runtime_resource_gate_board_test_log_result(const char *step,
                                                        esp_err_t err)
{
    ESP_LOGI(TAG, "%s: result=%s", step, esp_err_to_name(err));
}

/**
 * @brief 等待网络 service ready，但超时后仍继续测试本地 gate。
 */
static void runtime_resource_gate_board_test_wait_network(void)
{
    const TickType_t deadline =
        xTaskGetTickCount() + pdMS_TO_TICKS(60000);
    while (xTaskGetTickCount() < deadline)
    {
        if (network_service_is_service_ready())
        {
            ESP_LOGI(TAG, "network ready before stress sequence");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGW(TAG, "network not ready, continue local gate stress only");
}

/**
 * @brief 验证 Hermes 强前台 owner 会让 Safety Monitor / ESP-DL 让路。
 */
static void runtime_resource_gate_board_test_exercise_hermes_foreground(void)
{
    ESP_LOGI(TAG, "step hermes foreground begin");
    runtime_resource_gate_board_test_log_result(
        "memory_watch_set_foreground_true",
        memory_watch_service_set_foreground(true));
    vTaskDelay(pdMS_TO_TICKS(800));
    runtime_resource_gate_board_test_log_background_snapshot(
        "after hermes foreground acquire");

    runtime_resource_gate_board_test_log_result(
        "memory_watch_health_during_hermes",
        memory_watch_service_check_health());
    runtime_resource_gate_board_test_log_result(
        "memory_watch_inbox_poll_during_hermes",
        memory_watch_service_inbox_poll_now("runtime_gate_test_hermes"));
    vTaskDelay(pdMS_TO_TICKS(1500));

    runtime_resource_gate_board_test_log_result(
        "memory_watch_set_foreground_false",
        memory_watch_service_set_foreground(false));
    vTaskDelay(pdMS_TO_TICKS(800));
    runtime_resource_gate_board_test_log_background_snapshot(
        "after hermes foreground release");
}

/**
 * @brief 验证 BLE 强前台 owner；真实 BLE toggle 由单独 Kconfig 控制。
 */
static void runtime_resource_gate_board_test_exercise_ble_owner(void)
{
    ESP_LOGI(TAG, "step ble foreground owner begin");
    esp_err_t err = foreground_runtime_gate_acquire(
        FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING, 0);
    runtime_resource_gate_board_test_log_result("ble_owner_acquire", err);
    if (err == ESP_OK)
    {
        (void)background_service_manager_notify_foreground_runtime_changed();
        vTaskDelay(pdMS_TO_TICKS(800));
        runtime_resource_gate_board_test_log_background_snapshot(
            "during ble owner");

#if CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_ENABLE_REAL_BLE_TOGGLE
        /*
         * 真实 BLE toggle 会写 BLE NVS 偏好。NVS/flash 写入期间 cache 可能关闭，
         * 因此本测试模式必须使用 internal stack 创建任务，不能跑在 PSRAM stack 上。
         */
        runtime_resource_gate_board_test_log_result(
            "real_ble_enable",
            network_service_set_ble_enabled(true));
        vTaskDelay(pdMS_TO_TICKS(800));
        runtime_resource_gate_board_test_log_result(
            "real_ble_disable",
            network_service_set_ble_enabled(false));
#else
        ESP_LOGI(TAG, "real BLE toggle skipped by Kconfig");
#endif

        runtime_resource_gate_board_test_log_result(
            "ble_owner_release",
            foreground_runtime_gate_release(
                FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING));
        (void)background_service_manager_notify_foreground_runtime_changed();
    }
    vTaskDelay(pdMS_TO_TICKS(1200));
    runtime_resource_gate_board_test_log_background_snapshot(
        "after ble owner release");
}

static void runtime_resource_gate_board_test_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG,
             "runtime gate board stress start internal_free=%u largest=%u psram_free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    vTaskDelay(pdMS_TO_TICKS(CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_START_DELAY_MS));
    runtime_resource_gate_board_test_wait_network();
    runtime_resource_gate_board_test_log_background_snapshot("before stress");
    runtime_resource_gate_board_test_exercise_hermes_foreground();
    runtime_resource_gate_board_test_exercise_ble_owner();

    ESP_LOGI(TAG,
             "runtime gate board stress done internal_free=%u largest=%u psram_free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    s_test_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t runtime_resource_gate_board_test_start(void)
{
    if (s_test_task_handle != NULL)
    {
        return ESP_OK;
    }

    const BaseType_t ok = xTaskCreateWithCaps(
        runtime_resource_gate_board_test_task,
        "rt_gate_test",
        kTaskStackBytes,
        NULL,
        3,
        &s_test_task_handle,
        kTaskStackCaps);
    if (ok != pdPASS)
    {
        s_test_task_handle = NULL;
        ESP_LOGW(TAG, "runtime gate board test task create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "runtime gate board test task created");
    return ESP_OK;
}

#else

esp_err_t runtime_resource_gate_board_test_start(void)
{
    return ESP_OK;
}

#endif
