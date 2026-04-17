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
     * @brief 配网服务对外暴露的 Wi-Fi 状态枚举。
     */
    typedef enum
    {
        WIFI_PROVISION_STATE_CONNECTED,    /**< Wi-Fi 已连接并获取 IP。 */
        WIFI_PROVISION_STATE_DISCONNECTED, /**< Wi-Fi 已断开。 */
        WIFI_PROVISION_STATE_CONNECT_FAIL  /**< Wi-Fi 连接失败，例如重试耗尽或认证失败。 */
    } wifi_provision_state_t;

    /**
     * @brief Wi-Fi 状态变化回调函数类型。
     * @param[in] state 当前配网服务状态。
     */
    typedef void (*wifi_provision_cb_t)(wifi_provision_state_t state);

    /**
     * @brief 初始化 Wi-Fi 配网组件。
     *
     * 初始化后会拉起 `wifi_manager` 并创建配网后台任务，但不会自动开始 AP 或 BLE 配网。
     *
     * @param[in] callback Wi-Fi 状态变化回调。
     * @return `ESP_OK` 表示初始化成功或之前已初始化；其他错误表示事件组或后台任务创建失败。
     */
    esp_err_t wifi_provision_init(wifi_provision_cb_t callback);

    /**
     * @brief 根据本地凭据状态自动启动配网流程。
     *
     * - 有凭据时优先连接 STA
     * - 无凭据时进入当前 AP 网页配网（作为兜底路径）
     *
     * @return `ESP_OK` 表示流程已启动；其他错误表示底层连接或 AP 启动失败。
     */
    esp_err_t wifi_provision_start_auto(void);

    /**
     * @brief 启动 BLE 配网广播。
     * @return `ESP_OK` 表示启动成功；其他错误表示 BLE 传输层启动失败。
     */
    esp_err_t wifi_provision_start_blecfg(void);

    /**
     * @brief 停止 BLE 配网广播。
     * @return `ESP_OK` 表示停止成功；其他错误表示 BLE 传输层停止失败。
     */
    esp_err_t wifi_provision_stop_blecfg(void);

    /**
     * @brief 启动 AP 配网模式。
     * @return `ESP_OK` 表示启动成功；其他错误表示 AP 或 Web 门户启动失败。
     */
    esp_err_t wifi_provision_start_apcfg(void);

    /**
     * @brief 查询 BLE 配网是否处于活动状态。
     * @return true 表示 BLE 配网广播或连接仍然存在。
     */
    bool wifi_provision_is_ble_active(void);

    /**
     * @brief 查询 AP 网页配网是否处于活动状态。
     * @return true 表示当前传输模式为 AP 门户。
     */
    bool wifi_provision_is_ap_active(void);

    /**
     * @brief 获取 BLE 配网广播名称。
     *
     * @param[out] service_name 输出缓冲区。
     * @param[in] service_name_len 输出缓冲区长度，建议不少于 20 字节。
     * @return `ESP_OK` 表示成功；参数非法时返回错误。
     */
    esp_err_t wifi_provision_get_ble_service_name(char *service_name,
                                                  size_t service_name_len);

    /**
     * @brief 查询当前是否已连接 STA。
     * @return true 表示 `wifi_manager` 已确认拿到 STA IP。
     */
    bool wifi_provision_is_connected(void);

    /**
     * @brief 获取当前 STA IP。
     *
     * @param[out] ip_str 输出缓冲区。
     * @param[in] ip_str_len 输出缓冲区长度，至少 16 字节。
     * @return `ESP_OK` 表示成功；其他错误表示当前无有效 IP 或参数非法。
     */
    esp_err_t wifi_provision_get_ip(char *ip_str, size_t ip_str_len);

    /**
     * @brief 设置 Wi-Fi 省电模式。
     * @param[in] enable true 表示开启省电。
     * @return 底层 `wifi_manager` 返回值。
     */
    esp_err_t wifi_provision_set_power_save(bool enable);

    /**
     * @brief 保存 Wi-Fi 凭据。
     * @param[in] ssid 目标 SSID。
     * @param[in] password 密码。
     * @return `ESP_OK` 表示保存成功；其他错误表示参数或 NVS 保存失败。
     */
    esp_err_t wifi_provision_set_credentials(const char *ssid, const char *password);

    /**
     * @brief 查询是否已有可用 Wi-Fi 凭据。
     * @return true 表示本地缓存或 fallback 配置中存在可用凭据。
     */
    bool wifi_provision_has_credentials(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_PROVISION_H
