#ifndef DANGER_DETECTION_VIEW_H
#define DANGER_DETECTION_VIEW_H

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct danger_detection_view danger_detection_view_t;

typedef void (*danger_detection_view_action_cb_t)(void *user_data);

typedef struct {
    const char *status_text;
    const char *category_text;
    const char *primary_result_text;
    const char *horn_confidence_text;
    const char *siren_confidence_text;
    bool alert_visible;
} danger_detection_view_model_t;

typedef struct {
    danger_detection_view_action_cb_t back_action_cb;
    void *user_data;
} danger_detection_view_config_t;

danger_detection_view_t *danger_detection_view_create(
    const danger_detection_view_config_t *config);
void danger_detection_view_destroy(danger_detection_view_t *view);
lv_obj_t *danger_detection_view_get_screen(danger_detection_view_t *view);
void danger_detection_view_apply_model(
    danger_detection_view_t *view,
    const danger_detection_view_model_t *model);

#ifdef __cplusplus
}
#endif

#endif // DANGER_DETECTION_VIEW_H
