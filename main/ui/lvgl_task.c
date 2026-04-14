#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "button_gpio.h"
#include "driver/gpio.h"
#include "esp_freertos_hooks.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "events_init.h"
#include "features/alerts/display_alert_adapter.h"
#include "features/audio/audio_app.h"
#include "features/weather/hptts.h"
#include "features/weather/time_weather.h"
#include "get_time.h"
#include "gui_guider.h"
#include "iot_button.h"
#include "lv_demos.h"
#include "lv_port.h"
#include "lvgl.h"
#include "lvgl_task.h"
#include "nvs_flash.h"
#include "printf_esp32.h"
#include "ui/custom/ai_ui_controller.h"
#include "ui/custom/danger_detection_controller.h"
#include "ui_refresh_policy.h"

void lvgl_bottomr_init(void);

static const char *TAG = "lvgl_task";
int next_call = 0;
lv_ui guider_ui;
static TaskHandle_t cpu_monitor_task_handle = NULL;

static void cpu_monitor_task(void *arg)
{
    (void)arg;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
        // printf_esp32_memory_stats();
        // ESP_LOGI(TAG, "next_call:%d", next_call);
    }
}

void lvgl_task(void *pvParameter)
{
    (void)pvParameter;

    ESP_LOGI(TAG, "Starting application");
    lv_port_init_small();
    // lv_demo_benchmark();
    // lv_demo_stress();

    setup_ui(&guider_ui);
    ai_ui_controller_init(&guider_ui);
    danger_detection_controller_init(&guider_ui);
    events_init(&guider_ui);
    ui_refresh_policy_init();

    xTaskCreatePinnedToCore(
        cpu_monitor_task,
        "cpu_monitor",
        4096,
        NULL,
        1,
        &cpu_monitor_task_handle,
        0);

    while (1)
    {
        display_alert_adapter_process_ui();
        danger_detection_controller_poll_ui();

        next_call = lv_timer_handler();
        ui_refresh_policy_poll();

        uint32_t next_call_ms = next_call > 0 ? (uint32_t)next_call : 0U;
        uint32_t delay_ms = ui_refresh_policy_adjust_delay(next_call_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

static void record_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(btn, 0);

    if (code == LV_EVENT_CLICKED)
    {
        if (audio_app_is_recording())
        {
            audio_app_stop_record();
            lv_label_set_text(label, "start");
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x3B82F6), LV_PART_MAIN);
            ESP_LOGI(TAG, "用户点击: 停止录音");
        }
        else
        {
            char filename[64];
            snprintf(filename, sizeof(filename), "/sdcard/record/%04d%02d%02d_%02d%02d%02d.wav",
                     now_time.year, now_time.month, now_time.day,
                     now_time.hour, now_time.min, now_time.sec);

            if (audio_app_start_record(filename) == ESP_OK)
            {
                lv_label_set_text(label, "stop");
                lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF0000), LV_PART_MAIN);
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
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 180, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3B82F6), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, "开始录音");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);

    lv_obj_add_event_cb(btn, record_btn_event_handler, LV_EVENT_CLICKED, NULL);
}
