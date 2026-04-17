#ifndef BLE_PROVISION_PROTOCOL_H
#define BLE_PROVISION_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* BLE 配网协议命令类型。 */
    typedef enum
    {
        BLE_PROV_CMD_INVALID = 0,
        BLE_PROV_CMD_HELLO,
        BLE_PROV_CMD_STATUS,
        BLE_PROV_CMD_SET_WIFI,
        BLE_PROV_CMD_SCAN_WIFI,
        BLE_PROV_CMD_START_AP_FALLBACK,
    } ble_prov_cmd_t;

    typedef struct
    {
        ble_prov_cmd_t cmd; /**< 请求命令类型。 */
        char ssid[33];      /**< `set_wifi` 请求中的 SSID。 */
        char password[65];  /**< `set_wifi` 请求中的密码。 */
    } ble_prov_request_t;

    typedef struct
    {
        char ssid[33];  /**< AP 名称。 */
        int rssi;       /**< 信号强度，单位为 dBm。 */
        bool encrypted; /**< true 表示该 AP 需要密码。 */
    } ble_prov_wifi_scan_item_t;

    /**
     * @brief 解析一帧 BLE 配网 JSON 请求。
     * @param[in] data JSON 文本。
     * @param[out] request 解析后的请求结构。
     * @return `ESP_OK` 表示解析成功；其他错误表示字段缺失、类型错误或 JSON 非法。
     */
    esp_err_t ble_provision_protocol_parse_request(const char *data,
                                                   ble_prov_request_t *request);

    /**
     * @brief 格式化 BLE hello 响应。
     * @param[out] buffer 输出缓冲区。
     * @param[in] buffer_len 输出缓冲区长度，单位为字节。
     * @param[in] device_name 当前设备名。
     * @return `ESP_OK` 表示成功；其他错误表示参数非法或缓冲区不足。
     */
    esp_err_t ble_provision_protocol_format_hello(char *buffer, size_t buffer_len,
                                                  const char *device_name);

    /**
     * @brief 格式化 BLE 状态响应。
     * @param[out] buffer 输出缓冲区。
     * @param[in] buffer_len 输出缓冲区长度，单位为字节。
     * @param[in] state 状态字符串。
     * @param[in] ssid 当前 SSID，可为 NULL。
     * @param[in] ip 当前 IP，可为 NULL。
     * @param[in] reason 失败原因，可为 NULL。
     * @param[in] url AP 兜底 URL，可为 NULL。
     * @return `ESP_OK` 表示成功；其他错误表示参数非法或缓冲区不足。
     */
    esp_err_t ble_provision_protocol_format_status(char *buffer, size_t buffer_len,
                                                   const char *state,
                                                   const char *ssid,
                                                   const char *ip,
                                                   const char *reason,
                                                   const char *url);

    /**
     * @brief 格式化“开始扫描 Wi-Fi”响应。
     * @param[out] buffer 输出缓冲区。
     * @param[in] buffer_len 输出缓冲区长度，单位为字节。
     * @return `ESP_OK` 表示成功；其他错误表示参数非法或缓冲区不足。
     */
    esp_err_t ble_provision_protocol_format_wifi_scan_started(char *buffer,
                                                              size_t buffer_len);

    /**
     * @brief 格式化一批 Wi-Fi 扫描结果。
     * @param[out] buffer 输出缓冲区。
     * @param[in] buffer_len 输出缓冲区长度，单位为字节。
     * @param[in] items 扫描结果数组。
     * @param[in] item_count 当前批次条目数。
     * @param[in] more true 表示后续仍有更多批次。
     * @return `ESP_OK` 表示成功；其他错误表示参数非法或缓冲区不足。
     */
    esp_err_t ble_provision_protocol_format_wifi_scan_batch(
        char *buffer, size_t buffer_len, const ble_prov_wifi_scan_item_t *items,
        size_t item_count, bool more);

    /**
     * @brief 格式化 Wi-Fi 扫描完成响应。
     * @param[out] buffer 输出缓冲区。
     * @param[in] buffer_len 输出缓冲区长度，单位为字节。
     * @param[in] total 扫描结果总数。
     * @return `ESP_OK` 表示成功；其他错误表示参数非法或缓冲区不足。
     */
    esp_err_t ble_provision_protocol_format_wifi_scan_done(char *buffer,
                                                           size_t buffer_len,
                                                           size_t total);

    /**
     * @brief 格式化 Wi-Fi 扫描失败响应。
     * @param[out] buffer 输出缓冲区。
     * @param[in] buffer_len 输出缓冲区长度，单位为字节。
     * @param[in] reason 失败原因字符串。
     * @return `ESP_OK` 表示成功；其他错误表示参数非法或缓冲区不足。
     */
    esp_err_t ble_provision_protocol_format_wifi_scan_failed(char *buffer,
                                                             size_t buffer_len,
                                                             const char *reason);

#ifdef __cplusplus
}
#endif

#endif // BLE_PROVISION_PROTOCOL_H
