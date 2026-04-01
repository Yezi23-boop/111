#ifndef DANGER_DETECTION_SERVICE_H
#define DANGER_DETECTION_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DANGER_DETECTION_STATE_IDLE = 0,
    DANGER_DETECTION_STATE_STARTING,
    DANGER_DETECTION_STATE_RUNNING,
    DANGER_DETECTION_STATE_STOPPING,
    DANGER_DETECTION_STATE_ERROR,
} danger_detection_state_t;

typedef enum {
    DANGER_DETECTION_LABEL_NONE = 0,
    DANGER_DETECTION_LABEL_HORN,
    DANGER_DETECTION_LABEL_SIREN,
} danger_detection_label_t;

typedef struct {
    danger_detection_state_t state;
    danger_detection_label_t stable_label;
    esp_err_t last_error;
    bool danger_overlay_active;
} danger_detection_snapshot_t;

esp_err_t danger_detection_service_init(void);
esp_err_t danger_detection_service_start(void);
esp_err_t danger_detection_service_stop(uint32_t timeout_ms);
danger_detection_snapshot_t danger_detection_service_get_snapshot(void);

#ifdef __cplusplus
}
#endif

#endif // DANGER_DETECTION_SERVICE_H
