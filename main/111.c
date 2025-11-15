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

TaskHandle_t lvgl_task_handle = NULL;
TaskHandle_t lvgl_time_handle = NULL;
static EventGroupHandle_t s_wifi_ev = NULL; // WiFi事件组句柄，用于线程间通信
#define WIFI_CONNECT_BIT BIT0               // WiFi连接成功标志位（第0位）

void wifi_event_handler(WIFI_EV_e ev);
/**
 * @brief 应用程序主入口函数
 * @details 初始化LVGL系统，创建主页界面，运行主循环
 */

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    /* 创建WiFi事件组用于线程同步 */
    s_wifi_ev = xEventGroupCreate();
    EventBits_t ev = 0; // 用于接收事件组状态

    /* 初始化WiFi连接，传入事件回调函数 */
    wifi_sta_init(wifi_event_handler);
    /* 等待WiFi连接成功 */
    // 阻塞等待WiFi连接成功事件，超时时间为永久等待
    ev = xEventGroupWaitBits(s_wifi_ev, WIFI_CONNECT_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    if (ev) // 检查WiFi是否连接成功
    {
        ESP_LOGI("MAIN", "WiFi connected successfully, starting HTTP request...");
        // 先创建lvgl任务，确保LVGL端口初始化完成
        xTaskCreatePinnedToCore(lvgl_task, "lvgl_task", 1024 * 20, NULL, 10, &lvgl_task_handle, 1);

        // 延迟一段时间，确保LVGL初始化完成
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 创建时间和天气更新任务
        // 增加栈大小到4KB，避免SNTP和LVGL操作导致的栈溢出
        xTaskCreatePinnedToCore(time_and_weather, "time", 1024 * 4, NULL, 6, &lvgl_time_handle, 0);

        // printf_esp32_task_stack_stats(lvgl_time_handle, 1024 * 4, "time");
        // printf_esp32_task_stack_stats(lvgl_task_handle, 1024 * 4, "lvgl_task");
    }
    else
    {
        ESP_LOGE("MAIN", "WiFi connection failed");
    }
}

void wifi_event_handler(WIFI_EV_e ev)
{
    if (ev == WIFI_CONNECTED) // 如果WiFi连接成功
    {
        // 设置事件组中的WiFi连接位，通知主任务
        xEventGroupSetBits(s_wifi_ev, WIFI_CONNECT_BIT);
    }
}