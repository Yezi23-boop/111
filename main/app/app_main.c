#include <stdbool.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui_guider.h"
#include "events_init.h"
#include "esp_timer.h"
#include "esp_freertos_hooks.h"
#include "features/weather/time_weather.h"
#include "features/alerts/app_alert_manager.h"
#include "ui/lvgl_task.h"
#include "hardware_init.h"
#include "services/network_service.h"
#include "services/official_chat_service.h"
#include "services/power_service.h"
#include "services/power_policy.h"
#include "services/sleep_coordinator.h"
#include "services/wakeup_evidence_service.h"
#include "services/system_time_service.h"
#include "services/imu_service.h"
#include "services/background_service_manager.h"
#include "services/startup_readiness.h"

static const char *TAG = "MAIN";

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
 * @brief 启动 Board Foundation 阶段。
 * @return true 表示板级基础能力已可继续启动后续阶段。
 */
static bool start_board_foundation(void)
{
    esp_err_t ret = hardware_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "boot_stage: board_foundation_failed err=%s",
                 esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "boot_stage: board_foundation_done");
    return true;
}

/**
 * @brief 创建 Display/UI 前台任务。
 *
 * 这里只负责创建 `lvgl_task`；Display Foundation 和 UI First Frame
 * 的真正 ready 日志由 UI 任务在对应边界打印。
 *
 * @return true 表示 UI 任务已创建，后续阶段可以继续启动。
 */
static bool start_display_and_ui(void)
{
    if (startup_readiness_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "boot_stage: startup_readiness_failed");
        return false;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(lvgl_task, "lvgl_task", 1024 * 10,
                                            NULL, 6, &lvgl_task_handle, 1);
    if (ok != pdPASS)
    {
        ESP_LOGE(TAG, "boot_stage: ui_task_create_failed");
        return false;
    }

    ESP_LOGI(TAG, "boot_stage: ui_task_created");
    return true;
}

/**
 * @brief 启动电源观测和整机资源预算层。
 */
static void start_core_policy(void)
{
    if (power_service_init() != ESP_OK)
    {
        ESP_LOGW(TAG, "Power service init failed");
    }
    else if (power_service_start() != ESP_OK)
    {
        ESP_LOGW(TAG, "Power service start failed");
    }

    if (power_policy_start() != ESP_OK)
    {
        ESP_LOGW(TAG, "Power policy start failed");
        return;
    }

    if (sleep_coordinator_start() != ESP_OK)
    {
        ESP_LOGW(TAG, "Sleep coordinator start failed");
    }

    if (wakeup_evidence_service_start() != ESP_OK)
    {
        ESP_LOGW(TAG, "Wakeup evidence service start failed");
    }

    if (system_time_service_start() != ESP_OK)
    {
        ESP_LOGW(TAG, "System time service start failed");
    }

    ESP_LOGI(TAG, "boot_stage: policy_ready");
}

/**
 * @brief 启动系统级后台功能开关层。
 */
static void start_service_managers(void)
{
    if (app_alert_manager_init() != ESP_OK)
    {
        ESP_LOGW(TAG, "App alert manager init failed");
    }

    /*
     * 后台服务管理器是系统级功能开关层。第一阶段先托管危险识别，
     * 让它脱离“进入专页才运行、离开专页就停止”的页面生命周期。
     */
    if (background_service_manager_start() != ESP_OK)
    {
        ESP_LOGW(TAG, "Background service manager start failed");
        return;
    }

    ESP_LOGI(TAG, "boot_stage: managers_ready");
}

/**
 * @brief 启动可延后的后台服务入口。
 */
static void start_deferred_services(void)
{
    /*
     * 时间天气任务当前保留为可选入口，未默认启用。
     * 若后续恢复，需要重新评估 SNTP 与 UI 调用带来的栈占用。
     */
    // xTaskCreatePinnedToCore(time_and_weather, "time", 1024 * 4, NULL, 5, &lvgl_time_handle, 0);

    // BLE/AP/自动联网都由网络服务层统一调度，主入口不直接处理配网细节。
    if (network_service_start() != ESP_OK)
    {
        ESP_LOGE(TAG, "Background network service start failed");
    }
    else
    {
        ESP_LOGI(TAG, "boot_stage: network_service_ready");
    }

    // 聊天服务只初始化后台任务；真正拉起会话仍取决于网络和前台页面意图。
    if (official_chat_service_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Official chat service init failed");
    }
    else
    {
        ESP_LOGI(TAG, "boot_stage: official_chat_ready");
    }

    /*
     * QMI8658C 内部 WoM/INT1 第一版只做事件日志验证。它不直接点亮屏幕，
     * 不调用 sleep API，也不让 UI getter 访问 QMI8658C。
     */
    if (imu_service_start() != ESP_OK)
    {
        ESP_LOGW(TAG, "IMU service start failed");
    }
    else
    {
        ESP_LOGI(TAG, "boot_stage: imu_service_ready");
    }
}

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
    ESP_LOGI(TAG, "boot_stage: app_start");

    if (!start_board_foundation())
    {
        ESP_LOGE(TAG, "Hardware init failed, halting system");
        // 这里保留停机/重启分支作为后续容错入口，避免半初始化系统继续运行。
        // esp_restart();
        return;
    }

    if (!start_display_and_ui())
    {
        ESP_LOGE(TAG, "UI task create failed, halting startup sequence");
        return;
    }

    start_core_policy();
    start_service_managers();
    start_deferred_services();
    ESP_LOGI(TAG, "boot_stage: startup_sequence_done");
}
