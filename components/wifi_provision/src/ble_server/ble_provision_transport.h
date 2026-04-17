#ifndef BLE_PROVISION_TRANSPORT_H
#define BLE_PROVISION_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* 收到一帧完整 JSON 请求后的回调。 */
    typedef void (*ble_provision_transport_rx_cb_t)(const char *data, size_t len,
                                                    void *user_data);
    /* BLE 连接状态变化回调。 */
    typedef void (*ble_provision_transport_state_cb_t)(bool connected,
                                                       void *user_data);

    /**
     * @brief 启动 BLE GATT 配网传输层并开始广播。
     * @param[in] device_name BLE 广播名。
     * @param[in] rx_cb 收到一帧完整 JSON 请求后的回调。
     * @param[in] state_cb 连接状态变化回调，可为 NULL。
     * @param[in] user_data 用户透传上下文。
     * @return `ESP_OK` 表示成功；其他错误表示 NimBLE、GATT 或广播初始化失败。
     */
    esp_err_t ble_provision_transport_start(
        const char *device_name, ble_provision_transport_rx_cb_t rx_cb,
        ble_provision_transport_state_cb_t state_cb, void *user_data);

    /**
     * @brief 停止广播并断开当前连接。
     * @return `ESP_OK` 表示停止成功。
     */
    esp_err_t ble_provision_transport_stop(void);

    /**
     * @brief 查询传输层是否处于活动状态。
     * @return true 表示当前允许广播或维持连接。
     */
    bool ble_provision_transport_is_active(void);

    /**
     * @brief 查询当前是否已有 BLE 客户端连接。
     * @return true 表示存在有效 BLE 连接。
     */
    bool ble_provision_transport_is_connected(void);

    /**
     * @brief 发送一条 JSON 通知。
     * @param[in] json_payload JSON 文本。
     * @return `ESP_OK` 表示成功或当前无需发送；
     *         `ESP_ERR_INVALID_ARG` 表示参数非法；
     *         `ESP_ERR_INVALID_SIZE` 表示消息过长；
     *         其他错误表示通知发送失败。
     *
     * @note 当前实现会按固定分片长度发送，以兼容默认 ATT MTU 下的小包通知路径。
     */
    esp_err_t ble_provision_transport_notify_json(const char *json_payload);

    /**
     * @brief 获取当前 BLE 广播名。
     * @param[out] device_name 输出缓冲区。
     * @param[in] device_name_len 输出缓冲区长度，单位为字节。
     * @return `ESP_OK` 表示成功；参数非法时返回错误。
     */
    esp_err_t ble_provision_transport_get_device_name(char *device_name,
                                                      size_t device_name_len);

#ifdef __cplusplus
}
#endif

#endif // BLE_PROVISION_TRANSPORT_H
