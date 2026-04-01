#include "danger_detection_controller.h"

#include "danger_detection_service.h"
#include "danger_detection_view.h"
#include "esp_log.h"

static const char *TAG = "danger_ui";

static lv_ui *s_ui = NULL;
static danger_detection_view_t *s_view = NULL;

static void danger_detection_refresh_status(void);

static void danger_detection_back_event(void *user_data)
{
    (void)user_data;

    if (s_ui == NULL || s_ui->screen_main == NULL) {
        ESP_LOGW(TAG, "screen_main not ready when leaving danger page");
        return;
    }

    (void)danger_detection_service_stop(2000U);
    lv_screen_load_anim(s_ui->screen_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,
                        false);
}

static void danger_detection_ensure_screen_created(void)
{
    if (s_view != NULL) {
        return;
    }

    static const danger_detection_view_config_t kConfig = {
        .back_action_cb = danger_detection_back_event,
        .user_data = NULL,
    };

    s_view = danger_detection_view_create(&kConfig);
    if (s_view == NULL) {
        ESP_LOGE(TAG, "danger_detection_view_create failed");
    }
}

static void danger_detection_refresh_status(void)
{
    if (s_view == NULL) {
        return;
    }

    danger_detection_snapshot_t snapshot =
        danger_detection_service_get_snapshot();
    if (snapshot.state == DANGER_DETECTION_STATE_RUNNING &&
        snapshot.stable_label != DANGER_DETECTION_LABEL_NONE) {
        danger_detection_view_set_visual_state(
            s_view, DANGER_DETECTION_VIEW_VISUAL_STATE_ALERT);
    } else {
        danger_detection_view_set_visual_state(
            s_view, DANGER_DETECTION_VIEW_VISUAL_STATE_IDLE);
    }
}

void danger_detection_controller_init(lv_ui *ui)
{
    s_ui = ui;
    (void)danger_detection_service_init();
}

void danger_detection_ui_open(void)
{
    danger_detection_ensure_screen_created();
    if (s_view == NULL) {
        return;
    }

    (void)danger_detection_service_start();
    danger_detection_refresh_status();
    lv_screen_load_anim(danger_detection_view_get_screen(s_view),
                        LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void danger_detection_controller_poll_ui(void)
{
    if (s_view == NULL ||
        lv_screen_active() != danger_detection_view_get_screen(s_view)) {
        return;
    }

    danger_detection_refresh_status();
}
