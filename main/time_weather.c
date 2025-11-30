#include "time_weather.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "get_time.h"
#include "clock_functions.h"
#include "mp3_player.h"
static const char *TAG = "audio_example";
void time_and_weather(void *pvParameters)
{
    esp_wait_sntp_sync(); // 初始SNTP同步,确保时间准确

    // 4. 初始化MP3播放器
    esp_err_t ret = mp3_player_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "MP3播放器初始化失败: %d", ret);
    }
    else
    {
        ESP_LOGI(TAG, "MP3播放器初始化成功");
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    mp3_player_play_file("/sdcard/mp3/qing.mp3"); // 播放MP3

    uint32_t time_update_counter = 0; // 时间更新计数器
    while (1)
    {
        // 每5秒更新一次时间显示
        if (time_update_counter % 5 == 0) // 每5次循环（每5秒）更新时间
        {
            update_now_time();
            // ESP_LOGI("TIME", "time: %d-%d-%d %d:%d:%d", now_time.year, now_time.month, now_time.day, now_time.hour, now_time.min, now_time.sec);

            // update_digital_clock(now_time.hour, now_time.min, now_time.sec);
        }
        time_update_counter++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒延时
    }
}
