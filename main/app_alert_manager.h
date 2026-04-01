#ifndef APP_ALERT_MANAGER_H
#define APP_ALERT_MANAGER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_ALERT_SOURCE_NONE = 0,
    APP_ALERT_SOURCE_TRAFFIC_AUDIO = 1,
} app_alert_source_t;

typedef enum {
    APP_ALERT_SEVERITY_NONE = 0,
    APP_ALERT_SEVERITY_DANGER = 1,
} app_alert_severity_t;

typedef enum {
    APP_ALERT_LABEL_NONE = 0,
    APP_ALERT_LABEL_HORN = 1,
    APP_ALERT_LABEL_SIREN = 2,
} app_alert_label_t;

typedef struct {
    app_alert_source_t source;
    app_alert_severity_t severity;
    app_alert_label_t label;
} app_alert_request_t;

esp_err_t app_alert_manager_init(void);
esp_err_t app_alert_manager_raise(const app_alert_request_t *request);
esp_err_t app_alert_manager_clear(app_alert_source_t source);

#ifdef __cplusplus
}
#endif

#endif // APP_ALERT_MANAGER_H
