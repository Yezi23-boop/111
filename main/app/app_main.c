#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui_guider.h"
#include "events_init.h"
#include "esp_timer.h"
#include "esp_freertos_hooks.h"
#include "features/weather/time_weather.h"
#include "ui/lvgl_task.h"
#include "hardware_init.h"
#include "services/network_service.h"
#include "services/official_chat_service.h"
#include "services/power_service.h"

/*
 * 任务句柄说明：
 * - lvgl_task_handle: UI 主任务，负责 LVGL 渲染与事件处理。
 * - lvgl_time_handle: 时间天气任务句柄（当前正式入口保留但未启用）。
 */
TaskHandle_t lvgl_task_handle = NULL;
TaskHandle_t lvgl_time_handle = NULL;

/**
 * @brief 应用程序主入口函数
 * @details 初始化LVGL系统，创建主页界面，运行主循环
 */
void app_main(void)
{
    // 1) 初始化基础硬件（不阻塞等待联网完成）。
    if (hardware_init() == ESP_OK)
    {
        ESP_LOGI("MAIN", "Hardware init success, starting tasks...");

        // 2) 先拉起 UI 主任务，确保屏幕与输入尽快进入可交互状态。
        xTaskCreatePinnedToCore(lvgl_task, "lvgl_task", 1024 * 10, NULL, 6,
                                &lvgl_task_handle, 1);

        // 3) 启动电源快照服务（用于 UI 低功耗策略与电量展示）。
        if (power_service_init() != ESP_OK)
        {
            ESP_LOGW("MAIN", "Power service init failed");
        }
        else if (power_service_start() != ESP_OK)
        {
            ESP_LOGW("MAIN", "Power service start failed");
        }

        // 延迟一段时间，确保LVGL初始化完成
        // vTaskDelay(pdMS_TO_TICKS(1000));

        // 创建时间和天气更新任务
        // 增加栈大小到10KB，避免SNTP和LVGL操作导致的栈溢出
        // xTaskCreatePinnedToCore(time_and_weather, "time", 1024 * 4, NULL, 5, &lvgl_time_handle, 0);

        // 4) 后台联网状态机启动（BLE/AP/自动联网都在服务层处理）。
        if (network_service_start() != ESP_OK)
        {
            ESP_LOGE("MAIN", "Background network service start failed");
        }

        // 5) 聊天服务初始化，实际会在网络就绪后按前台请求启动会话。
        if (official_chat_service_init() != ESP_OK)
        {
            ESP_LOGE("MAIN", "Official chat service init failed");
        }
    }
    else
    {
        ESP_LOGE("MAIN", "Hardware init failed, halting system");
        // 初始化失败，停止运行或重启
        // esp_restart();
    }
}
