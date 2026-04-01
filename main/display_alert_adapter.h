#ifndef DISPLAY_ALERT_ADAPTER_H
#define DISPLAY_ALERT_ADAPTER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_alert_adapter_init(void);
esp_err_t display_alert_adapter_show_danger_overlay(void);
esp_err_t display_alert_adapter_hide_danger_overlay(void);
void display_alert_adapter_process_ui(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_ALERT_ADAPTER_H
