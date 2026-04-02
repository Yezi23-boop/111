/**
 * @file lv_port_tick.c
 * @brief LVGL tick 定时器实现
 */

#include "esp_timer.h"
#include "lv_port_internal.h"

static void lv_port_tick_cb(void *arg)
{
    uint32_t tick_interval = *((uint32_t *)arg);
    lv_tick_inc(tick_interval);
}

void lv_port_tick_init(void)
{
    static uint32_t tick_interval = 5;

    const esp_timer_create_args_t arg = {
        .arg = &tick_interval,
        .callback = lv_port_tick_cb,
        .name = "lvgl",
        .dispatch_method = ESP_TIMER_TASK,
        .skip_unhandled_events = true,
    };

    esp_timer_handle_t timer_handle;
    esp_timer_create(&arg, &timer_handle);
    esp_timer_start_periodic(timer_handle, tick_interval * 1000);
}
