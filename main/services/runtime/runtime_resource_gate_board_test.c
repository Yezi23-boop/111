#include "services/runtime/runtime_resource_gate_board_test.h"

#include "sdkconfig.h"

#if CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "services/memory_watch/memory_watch_service.h"
#include "services/ota/ota_service.h"
#include "services/runtime/runtime_coordinator.h"

static const char *TAG = "runtime_coord_test";
static const uint32_t kTaskStackBytes = 4096U;
static TaskHandle_t s_test_task_handle = NULL;

typedef enum
{
    TEST_RESPONSE_ACK = 0,
    TEST_RESPONSE_REJECT,
    TEST_RESPONSE_SILENT,
} runtime_coordinator_test_response_t;

static runtime_coordinator_test_response_t s_owner_quiesce_response =
    TEST_RESPONSE_ACK;
static runtime_coordinator_test_response_t s_owner_grant_response =
    TEST_RESPONSE_ACK;
static runtime_coordinator_test_response_t s_background_response =
    TEST_RESPONSE_ACK;

static esp_err_t runtime_coordinator_test_owner_quiesce(
    uint32_t generation, void *user_ctx)
{
    (void)user_ctx;
    if (s_owner_quiesce_response == TEST_RESPONSE_REJECT)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_owner_quiesce_response == TEST_RESPONSE_ACK)
    {
        return runtime_coordinator_report_quiesce_result(
            RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER,
            generation, ESP_OK);
    }
    return ESP_OK;
}

static esp_err_t runtime_coordinator_test_owner_grant(
    uint32_t generation, void *user_ctx)
{
    (void)user_ctx;
    if (s_owner_grant_response == TEST_RESPONSE_REJECT)
    {
        return runtime_coordinator_report_start_result(
            RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER,
            generation, ESP_FAIL);
    }
    if (s_owner_grant_response == TEST_RESPONSE_ACK)
    {
        return runtime_coordinator_report_start_result(
            RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER,
            generation, ESP_OK);
    }
    return ESP_OK;
}

static esp_err_t runtime_coordinator_test_owner_cancel(
    uint32_t generation, esp_err_t reason, void *user_ctx)
{
    (void)user_ctx;
    ESP_LOGI(TAG, "cancel: request=%u reason=%s",
             (unsigned)generation, esp_err_to_name(reason));
    return ESP_OK;
}

static esp_err_t runtime_coordinator_test_background_quiesce(
    uint32_t generation, void *user_ctx)
{
    (void)user_ctx;
    if (s_background_response == TEST_RESPONSE_REJECT)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_background_response == TEST_RESPONSE_ACK)
    {
        return runtime_coordinator_report_quiesce_result(
            RUNTIME_COORDINATOR_PARTICIPANT_TEST_BACKGROUND,
            generation, ESP_OK);
    }
    return ESP_OK;
}

static esp_err_t runtime_coordinator_test_background_reevaluate(
    uint32_t generation, void *user_ctx)
{
    (void)user_ctx;
    ESP_LOGI(TAG, "background reevaluate: transition=%u",
             (unsigned)generation);
    return ESP_OK;
}

static void runtime_coordinator_test_log_snapshot(const char *step)
{
    const runtime_coordinator_snapshot_t snapshot =
        runtime_coordinator_get_snapshot();
    ESP_LOGI(TAG,
             "sequence=%s state=%s current=%s target=%s request=%u transition=%u waiting=0x%08x error_owner=%s error=%s",
             step, runtime_coordinator_state_text(snapshot.state),
             runtime_coordinator_participant_text(snapshot.current_owner),
             runtime_coordinator_participant_text(snapshot.target_owner),
             (unsigned)snapshot.request_generation,
             (unsigned)snapshot.transition_generation,
             (unsigned)snapshot.waiting_mask,
             runtime_coordinator_participant_text(snapshot.error_participant),
             esp_err_to_name(snapshot.last_error));
}

static void runtime_coordinator_test_wait(uint32_t delay_ms,
                                          const char *step)
{
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    runtime_coordinator_test_log_snapshot(step);
}

static void runtime_coordinator_test_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(
        CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_START_DELAY_MS));

    uint32_t first = 0U;
    uint32_t latest = 0U;
    (void)runtime_coordinator_request_foreground(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, &first);
    (void)runtime_coordinator_request_foreground(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, &latest);
    runtime_coordinator_test_wait(500U, "latest_request_wins");
    (void)runtime_coordinator_report_quiesce_result(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, first, ESP_OK);
    runtime_coordinator_test_log_snapshot("stale_ack_ignored");

    s_owner_grant_response = TEST_RESPONSE_SILENT;
    uint32_t provisional = 0U;
    (void)runtime_coordinator_request_foreground(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, &provisional);
    runtime_coordinator_test_wait(300U, "provisional_holder_waiting");
    s_owner_grant_response = TEST_RESPONSE_ACK;
    uint32_t covered = 0U;
    (void)runtime_coordinator_request_foreground(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, &covered);
    runtime_coordinator_test_wait(500U, "provisional_request_covered");
    latest = covered;

    s_owner_quiesce_response = TEST_RESPONSE_REJECT;
    uint32_t rejected = 0U;
    (void)runtime_coordinator_request_foreground(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, &rejected);
    runtime_coordinator_test_wait(300U, "owner_reject");
    s_owner_quiesce_response = TEST_RESPONSE_ACK;

    s_owner_quiesce_response = TEST_RESPONSE_SILENT;
    uint32_t owner_timeout = 0U;
    (void)runtime_coordinator_request_foreground(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, &owner_timeout);
    runtime_coordinator_test_wait(5300U, "current_owner_timeout");
    s_owner_quiesce_response = TEST_RESPONSE_ACK;
    (void)runtime_coordinator_report_active(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER);
    runtime_coordinator_test_wait(300U, "current_owner_late_active");

    (void)runtime_coordinator_cancel_request(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, latest);
    runtime_coordinator_test_wait(300U, "owner_release");

    s_background_response = TEST_RESPONSE_SILENT;
    uint32_t background_timeout = 0U;
    (void)runtime_coordinator_request_foreground(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, &background_timeout);
    runtime_coordinator_test_wait(2800U, "background_timeout");
    s_background_response = TEST_RESPONSE_ACK;

    s_owner_grant_response = TEST_RESPONSE_REJECT;
    uint32_t grant_failed = 0U;
    (void)runtime_coordinator_request_foreground(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, &grant_failed);
    runtime_coordinator_test_wait(500U, "grant_failed");

    s_owner_grant_response = TEST_RESPONSE_SILENT;
    s_owner_quiesce_response = TEST_RESPONSE_SILENT;
    uint32_t rollback_timeout = 0U;
    (void)runtime_coordinator_request_foreground(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, &rollback_timeout);
    runtime_coordinator_test_wait(10300U, "rollback_timeout_degraded_target");

    s_owner_quiesce_response = TEST_RESPONSE_ACK;
    s_owner_grant_response = TEST_RESPONSE_ACK;
    (void)runtime_coordinator_report_active(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER);
    runtime_coordinator_test_wait(300U, "degraded_target_late_active");
    (void)runtime_coordinator_cancel_request(
        RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER, rollback_timeout);
    runtime_coordinator_test_wait(300U, "degraded_target_released");

    esp_err_t ret = memory_watch_service_set_foreground(true);
    ESP_LOGI(TAG, "real Hermes enter submitted: %s", esp_err_to_name(ret));
    runtime_coordinator_test_wait(1200U, "real_hermes_active");
    ret = memory_watch_service_set_foreground(false);
    ESP_LOGI(TAG, "real Hermes leave submitted: %s", esp_err_to_name(ret));
    runtime_coordinator_test_wait(1200U, "real_hermes_released");

    ret = ota_service_request_prepare();
    ESP_LOGI(TAG, "real OTA prepare submitted: %s", esp_err_to_name(ret));
    runtime_coordinator_test_wait(1200U, "real_ota_active");
    ret = ota_service_request_cancel();
    ESP_LOGI(TAG, "real OTA cancel submitted: %s", esp_err_to_name(ret));
    runtime_coordinator_test_wait(1200U, "real_ota_released");

    ESP_LOGI(TAG,
             "runtime coordinator board sequence done internal_free=%u largest=%u psram_free=%u",
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

    const runtime_coordinator_participant_config_t owner = {
        .id = RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER,
        .name = "test_owner",
        .capabilities = RUNTIME_COORDINATOR_CAPABILITY_FOREGROUND_EXCLUSIVE,
        .request_quiesce = runtime_coordinator_test_owner_quiesce,
        .grant_foreground = runtime_coordinator_test_owner_grant,
        .cancel_pending_request = runtime_coordinator_test_owner_cancel,
    };
    const runtime_coordinator_participant_config_t background = {
        .id = RUNTIME_COORDINATOR_PARTICIPANT_TEST_BACKGROUND,
        .name = "test_background",
        .capabilities =
            RUNTIME_COORDINATOR_CAPABILITY_BACKGROUND_PREEMPTIBLE,
        .request_quiesce = runtime_coordinator_test_background_quiesce,
        .request_reevaluate =
            runtime_coordinator_test_background_reevaluate,
    };
    esp_err_t ret = runtime_coordinator_register(&owner);
    if (ret == ESP_OK)
    {
        ret = runtime_coordinator_register(&background);
    }
    if (ret != ESP_OK)
    {
        return ret;
    }

    const BaseType_t created = xTaskCreateWithCaps(
        runtime_coordinator_test_task, "rt_coord_test",
        kTaskStackBytes, NULL, 3, &s_test_task_handle,
        MALLOC_CAP_SPIRAM);
    if (created != pdPASS)
    {
        s_test_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#else

esp_err_t runtime_resource_gate_board_test_start(void)
{
    return ESP_OK;
}

#endif
