#ifndef WIFI_MANAGER_PRIVATE_H
#define WIFI_MANAGER_PRIVATE_H

#include "esp_netif.h"
#include "wifi_manager.h"

/**
 * @brief 内部配置结构体
 */
typedef struct
{
    char ap_ssid[32];     // AP 门户模式 SSID
    char ap_password[64]; // AP 门户模式密码
    char ap_ip[16];       // AP 网关 IPv4 字符串
    int max_retry;        // STA 断线自动重连最大次数
} wifi_manager_config_internal_t;

/**
 * @brief 默认内部配置
 */
#define WIFI_MANAGER_DEFAULT_CONFIG() { \
    .ap_ssid = "ESP32_wifi",            \
    .ap_password = "12345678",          \
    .ap_ip = "192.168.100.1",           \
    .max_retry = 10}

#endif // WIFI_MANAGER_PRIVATE_H
