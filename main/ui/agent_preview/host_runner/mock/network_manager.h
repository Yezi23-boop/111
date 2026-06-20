#pragma once
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    NETWORK_MANAGER_STATE_IDLE,
    NETWORK_MANAGER_STATE_CONNECTING_LATEST,
    NETWORK_MANAGER_STATE_PROVISIONING_BLE,
    NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP,
    NETWORK_MANAGER_STATE_DISCONNECTED_BY_USER,
    NETWORK_MANAGER_STATE_ERROR
} network_manager_state_t;

typedef struct {
    bool wifi_connected;
    bool ble_enabled;
    bool ble_active;
    network_manager_state_t state;
    char ip[16];
} network_manager_status_t;
bool network_manager_is_ble_enabled(void);
bool network_manager_is_ble_active(void);
esp_err_t network_manager_set_ble_enabled(bool enabled);
esp_err_t network_manager_get_status(network_manager_status_t *status);
esp_err_t network_manager_use_latest_wifi(void);
esp_err_t network_manager_disconnect(void);
esp_err_t network_manager_start_ble_provisioning(void);
esp_err_t network_manager_start_softap_provisioning(void);
network_manager_state_t network_manager_get_state_cached(void);

