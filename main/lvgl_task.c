#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lv_port.h"
#include "lvgl.h"
#include "lv_demos.h"
#include "gui_guider.h"
#include "events_init.h"
#include "printf_esp32.h"
#include "esp_timer.h"
#include "esp_freertos_hooks.h"
#include "simple_wifi_sta.h"
#include "hptts.h"
#include "time_weather.h"
#include "nvs_flash.h"
#include "lvgl_task.h"
static const char *TAG = "lvgl_task";
int next_call=0;
lv_ui guider_ui;
// CPU使用率监控相关变量
static TaskHandle_t cpu_monitor_task_handle = NULL;
// CPU使用率监控任务
static void cpu_monitor_task(void *arg)
{

    while (1)
    {
        // 等待5秒
        vTaskDelay(pdMS_TO_TICKS(5000));
        // 打印内存统计信息
        printf_esp32_memory_stats();
        ESP_LOGI(TAG, "next_call:%d",next_call);
    }
}
void lvgl_task(void *pvParameter)
{

    ESP_LOGI(TAG, "Starting application");
    lv_port_init_small(); // 使用修复后的LVGL 9.2 API初始化函数
    
    
    // lv_demo_benchmark();
    // lv_demo_stress();
    setup_ui(&guider_ui);
    events_init(&guider_ui);

    // 创建CPU使用率监控任务
    xTaskCreatePinnedToCore(
        cpu_monitor_task,         // 任务函数
        "cpu_monitor",            // 任务名称
        4096,                     // 栈大小
        NULL,                     // 参数
        1,                        // 优先级 (低优先级，避免影响其他任务)
        &cpu_monitor_task_handle, // 任务句柄
        1                         // 在CPU0上运行
    );

    // LVGL任务主循环 - 保持任务持续运行
    while (1)
    {
        next_call = lv_timer_handler();

        // 优化延时逻辑，提高滑动时的帧率稳定性
        uint32_t delay_ms;
        if (next_call == 0)
        {
            delay_ms = 0; // 进一步减少强制延时，提高滑动响应性
        }
        else if (next_call > 500)
        {
            delay_ms = 500; // 降低最大延时，保持更高的最低帧率
            }
        else
        {
            delay_ms = next_call;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms)); // 高性能动态延时控制
    }
}