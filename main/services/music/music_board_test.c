#include "services/music/music_board_test.h"

#include "sdkconfig.h"

#if CONFIG_MUSIC_SERVICE_BOARD_TEST

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "services/music/music_service.h"
#include "services/network/network_service.h"

static const char *TAG = "music_board_test";
static const uint32_t kTaskStackBytes = 4096U;
static const uint32_t kNetworkReadyTimeoutMs = 90000U;
static const uint32_t kPlaybackWaitMs = 30000U;
static TaskHandle_t s_task_handle = NULL;

static bool music_board_test_wait_network_ready(void)
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

static bool music_board_test_wait_playing(void)
{
    const TickType_t deadline = xTaskGetTickCount() +
                                pdMS_TO_TICKS(kPlaybackWaitMs);
    while (xTaskGetTickCount() < deadline)
    {
        music_service_snapshot_t snapshot = {0};
        if (music_service_get_snapshot(&snapshot) == ESP_OK)
        {
            ESP_LOGI(TAG, "state=%d active=%u buffered_bytes=%u err=%s",
                     (int)snapshot.state, snapshot.music_active ? 1U : 0U,
                     (unsigned int)snapshot.buffered_bytes,
                     snapshot.error_code[0] != '\0' ? snapshot.error_code : "none");
            if (snapshot.state == MUSIC_SERVICE_STATE_PLAYING &&
                snapshot.music_active)
            {
                return true;
            }
            if (snapshot.state == MUSIC_SERVICE_STATE_ERROR)
            {
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500U));
    }
    return false;
}

static void music_board_test_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(CONFIG_MUSIC_SERVICE_BOARD_TEST_START_DELAY_MS));

    if (!music_board_test_wait_network_ready())
    {
        ESP_LOGE(TAG, "network service not ready; test skipped");
        goto done;
    }

    ESP_LOGI(TAG, "starting fixed source test");
    if (music_service_start_source("test") != ESP_OK ||
        !music_board_test_wait_playing())
    {
        ESP_LOGE(TAG, "fixed source playback test failed");
        goto done;
    }

    ESP_LOGI(TAG, "fixed source playback test passed");
    vTaskDelay(pdMS_TO_TICKS(5000U));
    (void)music_service_destroy();

done:
    s_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t music_board_test_start(void)
{
    if (s_task_handle != NULL)
    {
        return ESP_OK;
    }
    const BaseType_t created = xTaskCreateWithCaps(
        music_board_test_task, "music_board_test", kTaskStackBytes, NULL, 3,
        &s_task_handle, MALLOC_CAP_SPIRAM);
    if (created != pdPASS)
    {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "fixed-stream board test task created");
    return ESP_OK;
}

#else

esp_err_t music_board_test_start(void)
{
    return ESP_OK;
}

#endif
