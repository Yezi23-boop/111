#ifndef APP_ALERT_MANAGER_H
#define APP_ALERT_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        APP_ALERT_SOURCE_NONE = 0,
        APP_ALERT_SOURCE_TRAFFIC_AUDIO = 1, // 来自危险声音识别链路
    } app_alert_source_t;

    typedef enum
    {
        APP_ALERT_SEVERITY_NONE = 0,
        APP_ALERT_SEVERITY_DANGER = 1, // 危险级告警（需要红色覆盖层 + 提示音）
    } app_alert_severity_t;

    typedef enum
    {
        APP_ALERT_LABEL_NONE = 0,
        APP_ALERT_LABEL_HORN = 1,  // 喇叭类危险音
        APP_ALERT_LABEL_SIREN = 2, // 警笛类危险音
    } app_alert_label_t;

    typedef struct
    {
        app_alert_source_t source;     // 告警来源模块
        app_alert_severity_t severity; // 告警严重级别
        app_alert_label_t label;       // 告警类别标签
    } app_alert_request_t;

    esp_err_t app_alert_manager_init(void);
    esp_err_t app_alert_manager_raise(const app_alert_request_t *request);
    esp_err_t app_alert_manager_clear(app_alert_source_t source);
    esp_err_t app_alert_manager_set_traffic_audio_overlay_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // APP_ALERT_MANAGER_H
