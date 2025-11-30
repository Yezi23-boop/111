#ifndef HPTTS_H
#define HPTTS_H

#include "esp_err.h"
#include "esp_http_client.h"
extern struct tm timeinfo;
typedef struct
{
    char *id;
    char *name;
    char *country;
    char *path;
    char *timezone;
    char *timezone_offset;
    char *weather_text;
    char *weather_code;
    char *temperature;
    char *last_update;
} user_seniverse_now_config_t;
// 函数声明
esp_err_t _http_event_handler(esp_http_client_event_t *evt);
void http_rest_with_url(void);
void esp_wait_sntp_sync(void); // 新增SNTP同步函数声明

#endif // HPTTS_H
