#ifndef BLE_PROVISION_PROTOCOL_H
#define BLE_PROVISION_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

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
        ble_prov_cmd_t cmd; // 请求命令类型
        char ssid[33];      // set_wifi 请求中的 SSID
        char password[65];  // set_wifi 请求中的密码
    } ble_prov_request_t;

    typedef struct
    {
        char ssid[33];  // AP 名称
        int rssi;       // 信号强度（dBm）
        bool encrypted; // 是否需要密码
    } ble_prov_wifi_scan_item_t;

    esp_err_t ble_provision_protocol_parse_request(const char *data,
                                                   ble_prov_request_t *request);
    esp_err_t ble_provision_protocol_format_hello(char *buffer, size_t buffer_len,
                                                  const char *device_name);
    esp_err_t ble_provision_protocol_format_status(char *buffer, size_t buffer_len,
                                                   const char *state,
                                                   const char *ssid,
                                                   const char *ip,
                                                   const char *reason,
                                                   const char *url);
    esp_err_t ble_provision_protocol_format_wifi_scan_started(char *buffer,
                                                              size_t buffer_len);
    esp_err_t ble_provision_protocol_format_wifi_scan_batch(
        char *buffer, size_t buffer_len, const ble_prov_wifi_scan_item_t *items,
        size_t item_count, bool more);
    esp_err_t ble_provision_protocol_format_wifi_scan_done(char *buffer,
                                                           size_t buffer_len,
                                                           size_t total);
    esp_err_t ble_provision_protocol_format_wifi_scan_failed(char *buffer,
                                                             size_t buffer_len,
                                                             const char *reason);

#ifdef __cplusplus
}
#endif

#endif // BLE_PROVISION_PROTOCOL_H
