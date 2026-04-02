#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ai_experiment_ui.h"
#include "network_service.h"
#include "official_chat_service.h"
#include "wifi_provision.h"

#include "audio_codec.h"

static const char *TAG = "AI_CHAT_EXPERIMENT";

static esp_err_t ai_chat_nvs_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS init failed, erasing and retrying");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

void app_main(void) {
    ESP_ERROR_CHECK(ai_chat_nvs_init());

    ESP_ERROR_CHECK(wifi_provision_init(NULL));
    ESP_ERROR_CHECK(network_service_start());
    ESP_ERROR_CHECK(audio_codec_init());
    audio_codec_set_volume(60);
    ESP_ERROR_CHECK(official_chat_service_init());
    ESP_ERROR_CHECK(ai_experiment_ui_start());

    while (1) {
        ESP_LOGI(
            TAG, "network=%d official_chat_service=%s",
            (int)network_service_get_state(),
            official_chat_service_state_to_string(
                official_chat_service_get_state()));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
