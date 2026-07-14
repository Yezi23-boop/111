#pragma once
#include <stdbool.h>
typedef enum {
    DANGER_DETECTION_STATE_IDLE,
    DANGER_DETECTION_STATE_ACTIVE
} danger_detection_state_t;
danger_detection_state_t danger_detection_service_get_state(void);
void danger_detection_service_set_state(danger_detection_state_t state);
