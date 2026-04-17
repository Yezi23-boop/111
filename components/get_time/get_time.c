#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "get_time.h"
static const char *SNTP_TAG = "sntp"; // SNTP 相关日志标签。
struct tm timeinfo = {0};             // 全局时间结构体，保存本地时间。
my_time_t now_time;                   // 全局当前时间缓存。

/**
 * @brief 初始化 SNTP。
 * @return 无返回值。
 */
static void esp_initialize_sntp(void)
{
    ESP_LOGI(SNTP_TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp1.aliyun.com");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_setservername(2, "ntp2.aliyun.com");
    esp_sntp_init();
}

/**
 * @brief 获取当前本地时间并填充到自定义结构体。
 * @param[out] out_time 指向 `my_time_t` 结构体的指针。
 * @return 无返回值。
 */
void get_local_time(my_time_t *out_time)
{
    struct tm t;
    time_t now;
    time(&now);
    localtime_r(&now, &t);

    out_time->year = t.tm_year + 1900;
    out_time->month = t.tm_mon + 1;
    out_time->day = t.tm_mday;
    out_time->hour = t.tm_hour;
    out_time->min = t.tm_min;
    out_time->sec = t.tm_sec;
    char time_str[64];
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d",
             out_time->year, out_time->month, out_time->day,
             out_time->hour, out_time->min, out_time->sec);
    strcpy(out_time->time_str, time_str);
}

/**
 * @brief 刷新全局当前时间缓存。
 * @return 无返回值。
 */
void update_now_time(void)
{
    get_local_time(&now_time);
}

/**
 * @brief 等待 SNTP 时间同步完成。
 * @return 无返回值。
 *
 * @note 该函数会阻塞等待系统时间有效，不适合放在对启动时延敏感的高优先级任务中。
 */
void esp_wait_sntp_sync(void)
{
    char strftime_buf[64];
    esp_initialize_sntp();

    /* 以年份阈值判断是否已完成首次授时。
     * 冷启动时 RTC 常落在 1970 附近，因此这里等待年份进入有效区间。 */
    time_t now = 0;
    int retry = 0;
    while (timeinfo.tm_year < (2019 - 1900))
    {
        ESP_LOGD(SNTP_TAG, "Waiting for system time to be set... (%d)", ++retry);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    setenv("TZ", "CST-8", 1);
    tzset();

    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(SNTP_TAG, "The current date/time in guangzhoou is: %s", strftime_buf);
    ESP_LOGI(SNTP_TAG, "详细时间: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec);
}
