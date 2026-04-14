#include "audio_alert_player.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_codec.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "assets/tishiyinpin_pcm.h"

#define TAG "audio_alert_player"
#define ALERT_PLAYER_TASK_STACK_SIZE 4096U
#define ALERT_PLAYER_TASK_PRIORITY 4U
#define ALERT_PLAYER_VOLUME_PERCENT 75

typedef struct
{
    TaskHandle_t task_handle; // 当前播放任务句柄
    bool initialized;         // 模块是否已完成初始化
    bool playing;             // 是否有播放任务正在运行
} audio_alert_player_state_t;

static audio_alert_player_state_t s_player_state = {
    .task_handle = NULL,
    .initialized = false,
    .playing = false,
};

static void audio_alert_player_task(void *arg)
{
    (void)arg;

    // 写入长度以字节为单位；样本点数量需乘以单样本位宽。
    const size_t pcm_bytes = kTishiyinpinPcmSampleCount * sizeof(int16_t);

    ESP_LOGI(TAG, "warning playback started bytes=%u rate=%u",
             (unsigned int)pcm_bytes,
             (unsigned int)kTishiyinpinPcmSampleRate);

    (void)audio_codec_set_pa_enable(true);                     // 打开功放
    (void)audio_codec_set_mute(false);                         // 取消静音
    (void)audio_codec_set_volume(ALERT_PLAYER_VOLUME_PERCENT); // 设置告警播报音量

    esp_err_t ret = audio_codec_write(kTishiyinpinPcmData, pcm_bytes);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "warning playback failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ret = audio_codec_flush_output();
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "warning output flush failed: %s",
                     esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "warning playback finished");
    s_player_state.playing = false;
    s_player_state.task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t audio_alert_player_init(void)
{
    s_player_state.initialized = true;
    return ESP_OK;
}

esp_err_t audio_alert_player_play_warning_once(void)
{
    ESP_RETURN_ON_FALSE(s_player_state.initialized, ESP_ERR_INVALID_STATE, TAG,
                        "audio alert player not initialized");

    if (s_player_state.playing)
    {
        ESP_LOGI(TAG, "warning playback already active");
        return ESP_OK;
    }

    s_player_state.playing = true;
    BaseType_t task_created = xTaskCreate(audio_alert_player_task,
                                          "audio_alert",
                                          ALERT_PLAYER_TASK_STACK_SIZE,
                                          NULL,
                                          ALERT_PLAYER_TASK_PRIORITY,
                                          &s_player_state.task_handle);
    if (task_created != pdPASS)
    {
        s_player_state.playing = false;
        s_player_state.task_handle = NULL;
        ESP_LOGE(TAG, "failed to create warning playback task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
