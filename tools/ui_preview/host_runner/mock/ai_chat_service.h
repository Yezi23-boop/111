#pragma once
#include <stdbool.h>
typedef enum {
    AI_CHAT_STATE_IDLE,
    AI_CHAT_STATE_LISTENING
} ai_chat_state_t;
ai_chat_state_t ai_chat_service_get_state(void);
