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
 * 应用主入口说明：
 * - `app_main()` 只负责系统级启动编排，不承载长期业务循环；
 * - 启动顺序遵循“先硬件基础设施，再 UI，再后台服务”的原则；
 * - 这样可以保证即便联网或聊天服务尚未就绪，设备也能尽快进入可交互状态。
 */

/*
 * 任务句柄说明：
 * - lvgl_task_handle: UI 主任务，负责 LVGL 渲染与事件处理。
 * - lvgl_time_handle: 时间天气任务句柄（当前正式入口保留但未启用）。
 */
TaskHandle_t lvgl_task_handle = NULL; // UI 主任务句柄，仅启动阶段写入。
TaskHandle_t lvgl_time_handle = NULL; // 时间天气任务句柄；当前入口保留但未启用。

/**
 * @brief 应用程序主入口。
 *
 * 该入口只做系统级启动编排，不承载长期业务循环。
 * 启动顺序遵循“先硬件基础能力，再 UI，再后台服务”，
 * 这样即使联网链路暂未就绪，设备也能尽快进入可交互状态。
 *
 * @return 无返回值。
 *
 * @note 运行在 ESP-IDF 应用主任务上下文中；若基础硬件初始化失败，后续任务不会继续创建。
 */
void app_main(void)
{
    // 先拉起基础硬件；联网成功与否由后台状态机后续推进。
    if (hardware_init() == ESP_OK)
    {
        ESP_LOGI("MAIN", "Hardware init success, starting tasks...");

        // UI 需要尽快进入可交互状态，因此优先于后台服务创建。
        xTaskCreatePinnedToCore(lvgl_task, "lvgl_task", 1024 * 10, NULL, 6,
                                &lvgl_task_handle, 1);

        // 电源服务为 UI 亮度策略和电量展示提供快照，不依赖联网链路。
        if (power_service_init() != ESP_OK)
        {
            ESP_LOGW("MAIN", "Power service init failed");
        }
        else if (power_service_start() != ESP_OK)
        {
            ESP_LOGW("MAIN", "Power service start failed");
        }

        /*
         * 时间天气任务当前保留为可选入口，未默认启用。
         * 若后续恢复，需要重新评估 SNTP 与 UI 调用带来的栈占用。
         */
        // xTaskCreatePinnedToCore(time_and_weather, "time", 1024 * 4, NULL, 5, &lvgl_time_handle, 0);

        // BLE/AP/自动联网都由网络服务层统一调度，主入口不直接处理配网细节。
        if (network_service_start() != ESP_OK)
        {
            ESP_LOGE("MAIN", "Background network service start failed");
        }

        // 聊天服务只初始化后台任务；真正拉起会话仍取决于网络和前台页面意图。
        if (official_chat_service_init() != ESP_OK)
        {
            ESP_LOGE("MAIN", "Official chat service init failed");
        }
    }
    else
    {
        ESP_LOGE("MAIN", "Hardware init failed, halting system");
        // 这里保留停机/重启分支作为后续容错入口，避免半初始化系统继续运行。
        // esp_restart();
    }
}
