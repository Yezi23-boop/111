#ifndef DISPLAY_ALERT_ADAPTER_H
#define DISPLAY_ALERT_ADAPTER_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t display_alert_adapter_init(void);
    // 请求显示危险覆盖层（实际创建/显示在 UI 线程侧处理）。
    esp_err_t display_alert_adapter_show_danger_overlay(void);
    // 请求隐藏危险覆盖层。
    esp_err_t display_alert_adapter_hide_danger_overlay(void);
    // 抑制开关：置 true 后即使有 show 请求也不会真正显示。
    esp_err_t display_alert_adapter_set_suppressed(bool suppressed);
    // 在 LVGL 线程中周期调用，执行 pending_show/pending_hide。
    void display_alert_adapter_process_ui(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_ALERT_ADAPTER_H
