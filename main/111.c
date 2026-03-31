#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui_guider.h"
#include "events_init.h"
#include "esp_timer.h"
#include "esp_freertos_hooks.h"
#include "time_weather.h"
#include "lvgl_task.h"
#include "hardware_init.h"
#include "network_service.h"
#include "official_chat_service.h"

TaskHandle_t lvgl_task_handle = NULL;
TaskHandle_t lvgl_time_handle = NULL;
TaskHandle_t official_chat_bootstrap_task_handle = NULL;

static void official_chat_bootstrap_task(void *pv_parameter)
{
    (void)pv_parameter;

    while (!network_service_is_service_ready())
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI("MAIN", "Network service ready, entering official chat foreground");
    official_chat_service_enter_foreground();
    vTaskDelete(NULL);
}

/**
 * @brief 应用程序主入口函数
 * @details 初始化LVGL系统，创建主页界面，运行主循环
 */
void app_main(void)
{
    // 1. 硬件初始化
    // 此函数只保证基础硬件可用，联网在后台继续进行
    if (hardware_init() == ESP_OK)
    {
        ESP_LOGI("MAIN", "Hardware init success, starting tasks...");

        // 先创建lvgl任务，确保LVGL端口初始化完成
        // xTaskCreatePinnedToCore(lvgl_task, "lvgl_task", 1024 * 10, NULL, 6, &lvgl_task_handle, 1);

        // 延迟一段时间，确保LVGL初始化完成
        // vTaskDelay(pdMS_TO_TICKS(1000));

        // 创建时间和天气更新任务
        // 增加栈大小到10KB，避免SNTP和LVGL操作导致的栈溢出
        // xTaskCreatePinnedToCore(time_and_weather, "time", 1024 * 4, NULL, 5, &lvgl_time_handle, 0);

        if (network_service_start() != ESP_OK)
        {
            ESP_LOGE("MAIN", "Background network service start failed");
        }

        if (official_chat_service_init() != ESP_OK)
        {
            ESP_LOGE("MAIN", "Official chat service init failed");
        }

        if (xTaskCreatePinnedToCore(official_chat_bootstrap_task,
                                    "official_chat_bootstrap",
                                    1024 * 3, NULL, 5,
                                    &official_chat_bootstrap_task_handle,
                                    0) != pdPASS)
        {
            ESP_LOGE("MAIN", "Official chat bootstrap task create failed");
        }
    }
    else
    {
        ESP_LOGE("MAIN", "Hardware init failed, halting system");
        // 初始化失败，停止运行或重启
        // esp_restart();
    }
}
