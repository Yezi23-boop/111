#ifndef OFFICIAL_CHAT_SERVICE_H
#define OFFICIAL_CHAT_SERVICE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OFFICIAL_CHAT_SERVICE_STATE_STOPPED = 0,
    OFFICIAL_CHAT_SERVICE_STATE_WAITING_NETWORK,
    OFFICIAL_CHAT_SERVICE_STATE_STARTING,
    OFFICIAL_CHAT_SERVICE_STATE_ACTIVATING,
    OFFICIAL_CHAT_SERVICE_STATE_CONNECTING,
    OFFICIAL_CHAT_SERVICE_STATE_IDLE,
    OFFICIAL_CHAT_SERVICE_STATE_LISTENING,
    OFFICIAL_CHAT_SERVICE_STATE_SPEAKING,
    OFFICIAL_CHAT_SERVICE_STATE_ERROR,
} official_chat_service_state_t;

esp_err_t official_chat_service_init(void);
void official_chat_service_enter_foreground(void);
void official_chat_service_leave_foreground(void);
official_chat_service_state_t official_chat_service_get_state(void);
esp_err_t official_chat_service_get_last_error(void);
const char *official_chat_service_state_to_string(
    official_chat_service_state_t state);

#ifdef __cplusplus
}
#endif

#endif // OFFICIAL_CHAT_SERVICE_H
