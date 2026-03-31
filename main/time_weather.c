#include "time_weather.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "get_time.h"
#include "clock_functions.h"

void time_and_weather(void *pvParameters)
{
    esp_wait_sntp_sync(); // 初始SNTP同步,确保时间准确

    uint32_t time_update_counter = 0; // 时间更新计数器
    while (1)
    {
        // 每5秒更新一次时间显示
        if (time_update_counter % 120 == 0) // 每120次循环（每2分钟）更新时间
        {
            update_now_time();
            // ESP_LOGI("TIME", "time: %d-%d-%d %d:%d:%d", now_time.year, now_time.month, now_time.day, now_time.hour, now_time.min, now_time.sec);

            update_digital_clock(now_time.hour, now_time.min, now_time.sec);
        }
        time_update_counter++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒延时
    }
}
