#ifndef DANGER_DETECTION_VIEW_H
#define DANGER_DETECTION_VIEW_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct danger_detection_view danger_detection_view_t;

typedef void (*danger_detection_view_action_cb_t)(void *user_data);

typedef enum {
    DANGER_DETECTION_VIEW_VISUAL_STATE_IDLE = 0,
    DANGER_DETECTION_VIEW_VISUAL_STATE_ALERT,
} danger_detection_view_visual_state_t;

typedef struct {
    danger_detection_view_action_cb_t back_action_cb;
    void *user_data;
} danger_detection_view_config_t;

danger_detection_view_t *danger_detection_view_create(
    const danger_detection_view_config_t *config);
void danger_detection_view_destroy(danger_detection_view_t *view);
lv_obj_t *danger_detection_view_get_screen(danger_detection_view_t *view);
void danger_detection_view_set_visual_state(
    danger_detection_view_t *view,
    danger_detection_view_visual_state_t state);

#ifdef __cplusplus
}
#endif

#endif // DANGER_DETECTION_VIEW_H
