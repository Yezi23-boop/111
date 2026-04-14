#ifndef WIFI_PROVISION_H
#define WIFI_PROVISION_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Wi-Fi 状态枚举
     */
    typedef enum
    {
        WIFI_PROVISION_STATE_CONNECTED,    // Wi-Fi 已连接并获取 IP
        WIFI_PROVISION_STATE_DISCONNECTED, // Wi-Fi 已断开
        WIFI_PROVISION_STATE_CONNECT_FAIL  // Wi-Fi 连接失败（重试耗尽或认证失败）
    } wifi_provision_state_t;

    /**
     * @brief Wi-Fi 状态变化回调函数
     */
    typedef void (*wifi_provision_cb_t)(wifi_provision_state_t state);

    /**
     * @brief 初始化 Wi-Fi 配网组件
     *
     * @param callback Wi-Fi 状态变化回调
     */
    esp_err_t wifi_provision_init(wifi_provision_cb_t callback);

    /**
     * @brief 根据本地凭据状态自动启动 Wi-Fi
     *
     * - 有凭据时优先连接 STA
     * - 无凭据时进入当前 AP 网页配网（作为兜底路径）
     */
    esp_err_t wifi_provision_start_auto(void);

    /**
     * @brief 启动 BLE 配网广播
     */
    esp_err_t wifi_provision_start_blecfg(void);

    /**
     * @brief 停止 BLE 配网广播
     */
    esp_err_t wifi_provision_stop_blecfg(void);

    /**
     * @brief 启动 AP 配网模式
     */
    esp_err_t wifi_provision_start_apcfg(void);

    /**
     * @brief 当前 BLE 配网是否处于活动状态
     */
    bool wifi_provision_is_ble_active(void);

    /**
     * @brief 当前 AP 网页配网是否处于活动状态
     */
    bool wifi_provision_is_ap_active(void);

    /**
     * @brief 获取 BLE 配网广播名称
     *
     * @param service_name 输出缓冲区
     * @param service_name_len 输出缓冲区长度（建议 >= 20）
     */
    esp_err_t wifi_provision_get_ble_service_name(char *service_name,
                                                  size_t service_name_len);

    /**
     * @brief 当前是否已连接 STA
     */
    bool wifi_provision_is_connected(void);

    /**
     * @brief 获取当前 STA IP
     *
     * @param ip_str 输出缓冲区
     * @param ip_str_len 输出缓冲区长度，至少 16 字节
     */
    esp_err_t wifi_provision_get_ip(char *ip_str, size_t ip_str_len);

    /**
     * @brief 设置 Wi-Fi 省电模式
     */
    esp_err_t wifi_provision_set_power_save(bool enable);

    /**
     * @brief 保存 Wi-Fi 凭据
     */
    esp_err_t wifi_provision_set_credentials(const char *ssid, const char *password);

    /**
     * @brief 是否已有可用 Wi-Fi 凭据
     */
    bool wifi_provision_has_credentials(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_PROVISION_H
