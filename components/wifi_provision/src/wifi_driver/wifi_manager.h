#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_wifi.h"

typedef enum
{
    WIFI_STATE_CONNECTED,    /**< Wi-Fi 已连接并获取 IP。 */
    WIFI_STATE_DISCONNECTED, /**< Wi-Fi 已断开。 */
    WIFI_STATE_CONNECT_FAIL, /**< Wi-Fi 连接失败，例如重试耗尽。 */
} WIFI_STATE;

/** 状态回调在 Wi-Fi 事件上下文或事件转发路径中触发，调用方不应长时间阻塞。 */
typedef void (*p_wifi_state_callback)(WIFI_STATE state);
/** 扫描回调在扫描任务上下文中触发，结果数组生命周期仅在回调期间有效。 */
typedef void (*p_wifi_scan_callback)(wifi_ap_record_t *ap, int ap_count,
                                     esp_err_t scan_result);

/**
 * @brief 初始化 Wi-Fi 管理器。
 * @param[in] callback 状态变化回调。
 * @return 无返回值。
 */
void wifi_manager_init(p_wifi_state_callback callback);

/**
 * @brief 按显式 SSID/密码发起 STA 连接。
 * @param[in] ssid 目标 SSID。
 * @param[in] password 目标密码。
 * @return `ESP_OK` 表示连接请求已下发；其他错误表示参数非法或底层配置失败。
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief 使用已保存凭据发起连接。
 * @return `ESP_OK` 表示连接请求已下发；`ESP_ERR_INVALID_STATE` 表示当前没有可用凭据。
 */
esp_err_t wifi_manager_connect_saved(void);

/**
 * @brief 进入 APSTA 并启动 AP 门户侧配置。
 * @return `ESP_OK` 表示 AP 门户已就绪；其他错误表示 AP 或 DHCP 配置失败。
 */
esp_err_t wifi_manager_ap(void);

/**
 * @brief 触发一次异步 Wi-Fi 扫描。
 * @param[in] callback 扫描结果回调。
 * @return `ESP_OK` 表示扫描任务已创建；`ESP_ERR_INVALID_STATE` 表示已有扫描在进行。
 */
esp_err_t wifi_manager_scan(p_wifi_scan_callback callback);

/**
 * @brief 获取当前 STA IP。
 * @param[out] ip_str 输出缓冲区。
 * @param[in] ip_str_len 输出缓冲区长度，至少 16 字节。
 * @return `ESP_OK` 表示成功；其他错误表示当前未连接或参数非法。
 */
esp_err_t wifi_manager_get_ip(char *ip_str, size_t ip_str_len);

/**
 * @brief 停止 AP 模式并回到 STA。
 * @return 底层 `esp_wifi_set_mode()` 返回值。
 */
esp_err_t wifi_manager_stop_ap(void);

/**
 * @brief 查询 STA 是否已连接。
 * @return true 表示已拿到 STA IP。
 */
bool wifi_manager_is_connected(void);

/**
 * @brief 设置 Wi-Fi 省电模式。
 * @param[in] enable true 表示开启省电。
 * @return 底层 `esp_wifi_set_ps()` 返回值。
 */
esp_err_t wifi_manager_set_power_save(bool enable);

/**
 * @brief 保存 Wi-Fi 凭据到内部缓存和 NVS。
 * @param[in] ssid 目标 SSID。
 * @param[in] password 目标密码。
 * @return `ESP_OK` 表示保存成功；其他错误表示参数非法或 NVS 写入失败。
 */
esp_err_t wifi_manager_set_credentials(const char *ssid, const char *password);

/**
 * @brief 查询当前是否已有可用凭据。
 * @return true 表示缓存或 fallback 配置中存在可用凭据。
 */
bool wifi_manager_has_credentials(void);

#endif
