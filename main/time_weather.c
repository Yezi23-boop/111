#include "time_weather.h"
#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "get_time.h"
#include "clock_functions.h"
void time_and_weather(void *pvParameters)
{
    /* 将任务添加到看门狗监控 */
    esp_task_wdt_add(NULL);

    esp_wait_sntp_sync(); // 初始SNTP同步，确保时间准确

    /* 重置看门狗 */
    esp_task_wdt_reset();

    uint32_t time_update_counter = 0; // 时间更新计数器

    while (1)
    {
        /* 重置看门狗计时器 */
        esp_task_wdt_reset();

        // 每5秒更新一次时间显示
        if (time_update_counter % 100 == 0) // 每5次循环（每5秒）更新时间
        {
            my_time_t now_time;
            get_local_time(&now_time);
            ESP_LOGI("TIME", "time: %d-%d-%d %d:%d:%d", now_time.year, now_time.month, now_time.day, now_time.hour, now_time.min, now_time.sec);

            // update_digital_clock(now_time.hour, now_time.min, now_time.sec);
        }

        /* 重置看门狗计时器 */
        esp_task_wdt_reset();
        time_update_counter++;

        /* 使用较短的延时，每秒检查一次，避免长时间阻塞 */
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒延时
    }
}