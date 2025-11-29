#include "time_weather.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "get_time.h"
#include "clock_functions.h"
#include "audio_codec.h"
#include "esp_spiffs.h"
#include "i2c_manager.h"
#include "mp3_player.h"
#include "sd_manager.h"
static const char *TAG = "audio_example";
#define AUDIO_MOUNT "/spiffs"
#define WAV_FILE_PATH AUDIO_MOUNT "/1.wav"
#define SD_CARD_PATH "/sdcard/"
void audio_spiffs_init(void);
void time_and_weather(void *pvParameters)
{
    esp_wait_sntp_sync(); // 初始SNTP同步,确保时间准确

    // 1. 初始化音频编解码器
    esp_err_t ret = audio_codec_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "音频编解码器初始化失败: %d", ret);
    }
    else
    {
        ESP_LOGI(TAG, "音频系统初始化成功");
        audio_codec_set_volume(60);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // 2. 初始化SPIFFS
    audio_spiffs_init();
    vTaskDelay(pdMS_TO_TICKS(100));
    // 2. 初始化SDK文件系统
    ret = sd_manager_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SD卡初始化失败: %d", ret);
    }
    else
    {
        ESP_LOGI(TAG, "SD卡初始化成功");
        sd_manager_list_dir(SD_CARD_PATH "/mp3");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    // 3. 扫描I2C总线
    i2c_manager_scan();

    // 4. 初始化MP3播放器
    ret = mp3_player_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "MP3播放器初始化失败: %d", ret);
    }
    else
    {
        ESP_LOGI(TAG, "MP3播放器初始化成功");
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    mp3_player_play_file(SD_CARD_PATH "/mp3/qing.mp3"); // 播放MP3

    uint32_t time_update_counter = 0; // 时间更新计数器
    while (1)
    {
        // 每5秒更新一次时间显示
        if (time_update_counter % 5 == 0) // 每5次循环（每5秒）更新时间
        {
            my_time_t now_time;
            get_local_time(&now_time);
            ESP_LOGI("TIME", "time: %d-%d-%d %d:%d:%d", now_time.year, now_time.month, now_time.day, now_time.hour, now_time.min, now_time.sec);

            // update_digital_clock(now_time.hour, now_time.min, now_time.sec);
        }
        time_update_counter++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒延时
    }
}

void audio_spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = AUDIO_MOUNT,
        .partition_label = "audio",
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("audio", &total, &used);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "SPIFFS: %.2fMB/%.2fMB", used / 1024.0 / 1024.0, total / 1024.0 / 1024.0);
    }
}
