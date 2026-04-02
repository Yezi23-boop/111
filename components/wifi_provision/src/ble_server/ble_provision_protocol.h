#ifndef BLE_PROVISION_PROTOCOL_H
#define BLE_PROVISION_PROTOCOL_H

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLE_PROV_CMD_INVALID = 0,
    BLE_PROV_CMD_HELLO,
    BLE_PROV_CMD_STATUS,
    BLE_PROV_CMD_SET_WIFI,
    BLE_PROV_CMD_START_AP_FALLBACK,
} ble_prov_cmd_t;

typedef struct {
    ble_prov_cmd_t cmd;
    char ssid[33];
    char password[65];
} ble_prov_request_t;

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

#ifdef __cplusplus
}
#endif

#endif // BLE_PROVISION_PROTOCOL_H
