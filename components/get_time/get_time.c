#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "get_time.h"
static const char *SNTP_TAG = "sntp"; // SNTP相关日志标签
struct tm timeinfo = {0};             // 全局时间结构体，保存本地时间
my_time_t now_time;                   // 全局当前时间

/**
 * @brief 初始化SNTP
 * @details 配置并启动SNTP客户端，设置多个NTP服务器用于时间同步
 */
static void esp_initialize_sntp(void)
{
    ESP_LOGI(SNTP_TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL); // 设置为主动轮询模式
    esp_sntp_setservername(0, "ntp1.aliyun.com");    // 设置主NTP服务器
    esp_sntp_setservername(1, "cn.pool.ntp.org");    // 设置备用NTP服务器
    esp_sntp_setservername(2, "ntp2.aliyun.com");    // 再加一个备用
    esp_sntp_init();                                 // 启动SNTP服务
}

/**
 * @brief 获取当前本地时间
 * @details 获取系统当前时间并填充到自定义结构体 my_time_t 中，自动处理年份和月份
 * @param out_time 指向 my_time_t 结构体的指针
 */
void get_local_time(my_time_t *out_time)
{
    struct tm t;
    time_t now;
    time(&now);            // 获取当前时间戳
    localtime_r(&now, &t); // 转换为本地时间结构体

    // 填充自定义结构体
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

void update_now_time(void)
{
    get_local_time(&now_time);
}

/**
 * @brief 等待SNTP时间同步
 * @details 阻塞直到SNTP同步时间完成，确保系统时间有效（大于2019年）
 * @details 同步完成后设置时区为中国标准时间（东八区），并打印当前时间
 */
void esp_wait_sntp_sync(void)
{
    char strftime_buf[64];
    esp_initialize_sntp(); // 启动SNTP

    // 等待系统时间被同步（初始时间通常是1970年）
    time_t now = 0;
    int retry = 0;
    while (timeinfo.tm_year < (2019 - 1900)) // 判断年份是否大于2019
    {
        ESP_LOGD(SNTP_TAG, "Waiting for system time to be set... (%d)", ++retry);
        vTaskDelay(100 / portTICK_PERIOD_MS); // 延时100ms
        time(&now);                           // 获取当前时间戳
        localtime_r(&now, &timeinfo);         // 刷新全局时间结构体
    }

    // 设置时区为中国标准时间（东八区）
    setenv("TZ", "CST-8", 1);
    tzset();

    // 格式化并打印当前时间
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
