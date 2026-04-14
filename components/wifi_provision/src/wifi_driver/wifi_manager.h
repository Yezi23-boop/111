#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_wifi.h"

typedef enum
{
    WIFI_STATE_CONNECTED,    // Wi-Fi 已连接并获取 IP
    WIFI_STATE_DISCONNECTED, // Wi-Fi 已断开
    WIFI_STATE_CONNECT_FAIL, // Wi-Fi 连接失败（重试耗尽）
} WIFI_STATE;

typedef void (*p_wifi_state_callback)(WIFI_STATE state);
typedef void (*p_wifi_scan_callback)(wifi_ap_record_t *ap, int ap_count,
                                     esp_err_t scan_result);

void wifi_manager_init(p_wifi_state_callback callback);

// 按显式 SSID/密码发起 STA 连接。
esp_err_t wifi_manager_connect(const char *ssid, const char *password);
// 使用已保存凭据发起连接。
esp_err_t wifi_manager_connect_saved(void);
// 进入 APSTA 并启动 AP 门户侧配置。
esp_err_t wifi_manager_ap(void);
// 触发一次同步扫描，结果通过回调返回。
esp_err_t wifi_manager_scan(p_wifi_scan_callback callback);
esp_err_t wifi_manager_get_ip(char *ip_str, size_t ip_str_len);
esp_err_t wifi_manager_stop_ap(void);

bool wifi_manager_is_connected(void);
esp_err_t wifi_manager_set_power_save(bool enable);
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);
bool wifi_manager_has_credentials(void);

#endif
