#ifndef OFFICIAL_CHAT_SERVICE_H
#define OFFICIAL_CHAT_SERVICE_H

#include <stddef.h>

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

typedef enum {
    OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER = 0,
    OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT,
} official_chat_service_message_role_t;

typedef struct {
    official_chat_service_message_role_t role;
    char text[256];
} official_chat_service_message_t;

esp_err_t official_chat_service_init(void);
void official_chat_service_enter_foreground(void);
void official_chat_service_leave_foreground(void);
official_chat_service_state_t official_chat_service_get_state(void);
esp_err_t official_chat_service_get_last_error(void);
size_t official_chat_service_get_message_count(void);
esp_err_t official_chat_service_get_message(size_t index,
                                            official_chat_service_message_t *out_message);
esp_err_t official_chat_service_get_last_user_text(char *buffer, size_t size);
esp_err_t official_chat_service_get_last_assistant_text(char *buffer,
                                                        size_t size);
const char *official_chat_service_state_to_string(
    official_chat_service_state_t state);

#ifdef __cplusplus
}
#endif

#endif // OFFICIAL_CHAT_SERVICE_H
