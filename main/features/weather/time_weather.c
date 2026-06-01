#include "time_weather.h"
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "services/system_time_service.h"
#include "ui/custom/clock_functions.h"

/*
 * 时间后台任务实现说明：
 * - 时间同步由 `system_time_service` 统一负责，本任务只读取本地时间快照；
 * - 以 1s 节拍循环，但只在较低频率下更新 UI，减少不必要刷新；
 * - 当前任务主要服务于数字时钟，天气刷新逻辑尚未接入此循环。
 */

/**
 * @brief 时间与天气后台任务入口。
 * @param pvParameters 未使用，保留任务签名。
 */
void time_and_weather(void *pvParameters)
{
    (void)pvParameters;

    uint32_t time_update_counter = 0; // 循环计数，用于控制刷新节奏
    while (1)
    {
        // 当前按 1s 循环，满足条件时刷新 UI 时钟。
        if (time_update_counter % 120 == 0) // 每120次循环（约2分钟）执行一次更新时间
        {
            system_time_local_t local_time = {0};
            if (system_time_service_get_local_time(&local_time) == ESP_OK)
            {
                update_digital_clock(local_time.hour, local_time.min,
                                     local_time.sec);
            }
        }
        time_update_counter++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // 主循环节拍 1s
    }
}
