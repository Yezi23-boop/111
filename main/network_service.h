#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NETWORK_SERVICE_STATE_OFFLINE = 0,
    NETWORK_SERVICE_STATE_CONNECTING,
    NETWORK_SERVICE_STATE_WIFI_READY,
    NETWORK_SERVICE_STATE_SERVICE_READY,
    NETWORK_SERVICE_STATE_PORTAL_REQUIRED,
    NETWORK_SERVICE_STATE_ERROR,
} network_service_state_t;

esp_err_t network_service_start(void);

network_service_state_t network_service_get_state(void);

bool network_service_is_service_ready(void);

esp_err_t network_service_get_ip(char *ip_str, size_t ip_str_len);

void network_service_request_portal(void);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_SERVICE_H
