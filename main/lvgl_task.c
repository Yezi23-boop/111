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
#include "audio_app.h" // 引入音频应用头文件
#include "get_time.h"
// 前置声明
void lvgl_bottomr_init(void);

static const char *TAG = "lvgl_task";
int next_call = 0;
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
        // printf_esp32_memory_stats();
        ESP_LOGI(TAG, "next_call:%d", next_call);
    }
}
void lvgl_task(void *pvParameter)
{

    ESP_LOGI(TAG, "Starting application");
    lv_port_init_small();
    // lv_demo_benchmark();
    // lv_demo_stress();
    setup_ui(&guider_ui);
    events_init(&guider_ui);

    // 初始化自定义底部按钮
    // lvgl_bottomr_init();

    // 创建CPU使用率监控任务
    xTaskCreatePinnedToCore(
        cpu_monitor_task,         // 任务函数
        "cpu_monitor",            // 任务名称
        4096,                     // 栈大小
        NULL,                     // 参数
        1,                        // 优先级 (低优先级，避免影响其他任务)
        &cpu_monitor_task_handle, // 任务句柄
        1                         // 在CPU1上运行
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

// 录音按钮事件回调
static void record_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0); // 获取按钮的第一个子对象(标签)

    if (code == LV_EVENT_CLICKED)
    {
        if (audio_app_is_recording())
        {
            // 正在录音 -> 停止录音
            audio_app_stop_record();

            // 更新UI
            lv_label_set_text(label, "start");
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x3B82F6), LV_PART_MAIN); // 恢复蓝色
            ESP_LOGI(TAG, "用户点击: 停止录音");
        }
        else
        {
            // 未录音 -> 开始录音
            // 生成带时间戳的文件名，防止覆盖
            char filename[64];
            // 使用全局 now_time 构造文件名，避免重复计算
            snprintf(filename, sizeof(filename), "/sdcard/record/%04d%02d%02d_%02d%02d%02d.wav",
                     now_time.year, now_time.month, now_time.day,
                     now_time.hour, now_time.min, now_time.sec);

            if (audio_app_start_record(filename) == ESP_OK)
            {
                // 更新UI
                lv_label_set_text(label, "stop");
                lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF0000), LV_PART_MAIN); // 变为红色
                ESP_LOGI(TAG, "用户点击: 开始录音 -> %s", filename);
            }
            else
            {
                ESP_LOGE(TAG, "启动录音失败");
            }
        }
    }
}

void lvgl_bottomr_init(void)
{
    // 获取当前活动屏幕
    lv_obj_t *scr = lv_screen_active();

    // 创建按钮
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 180, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);

    // 设置按钮初始样式 (蓝色)
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3B82F6), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);

    // 创建按钮标签
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "开始录音");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0); // 设置字体
    lv_obj_center(label);

    // 添加事件处理
    lv_obj_add_event_cb(btn, record_btn_event_handler, LV_EVENT_CLICKED, NULL);
}
