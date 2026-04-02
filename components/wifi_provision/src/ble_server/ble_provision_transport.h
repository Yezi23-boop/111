#ifndef BLE_PROVISION_TRANSPORT_H
#define BLE_PROVISION_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_provision_transport_rx_cb_t)(const char *data, size_t len,
                                                void *user_data);
typedef void (*ble_provision_transport_state_cb_t)(bool connected,
                                                   void *user_data);

esp_err_t ble_provision_transport_start(
    const char *device_name, ble_provision_transport_rx_cb_t rx_cb,
    ble_provision_transport_state_cb_t state_cb, void *user_data);
esp_err_t ble_provision_transport_stop(void);
bool ble_provision_transport_is_active(void);
bool ble_provision_transport_is_connected(void);
esp_err_t ble_provision_transport_notify_json(const char *json_payload);
esp_err_t ble_provision_transport_get_device_name(char *device_name,
                                                  size_t device_name_len);

#ifdef __cplusplus
}
#endif

#endif // BLE_PROVISION_TRANSPORT_H
