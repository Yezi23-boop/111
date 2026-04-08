#include "danger_detection_controller.h"

#include <stdio.h>
#include <string.h>

#include "features/alerts/app_alert_manager.h"
#include "features/danger_detection/danger_detection_service.h"
#include "danger_detection_view.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "danger_ui";
static const uint32_t DANGER_ALERT_DURATION_MS = 2000U;

static lv_ui *s_ui = NULL;
static danger_detection_view_t *s_view = NULL;
static lv_timer_t *s_alert_timer = NULL;
static uint32_t s_last_alert_sequence = 0U;
static bool s_alert_visible = false;

typedef struct {
    bool valid;
    bool alert_visible;
    char status_text[16];
    char category_text[32];
    char primary_result_text[16];
    char horn_confidence_text[16];
    char siren_confidence_text[16];
} danger_detection_render_cache_t;

static danger_detection_render_cache_t s_render_cache = {0};

static void danger_detection_refresh_status(void);
static void danger_detection_alert_timer_cb(lv_timer_t *timer);

static const char *danger_detection_status_text(
    danger_detection_state_t state)
{
    if (state == DANGER_DETECTION_STATE_ERROR) {
        return "ERROR";
    }
    if (state == DANGER_DETECTION_STATE_STOPPING) {
        return "STOPPING";
    }
    if (state == DANGER_DETECTION_STATE_STARTING) {
        return "STARTING";
    }
    if (state == DANGER_DETECTION_STATE_RUNNING) {
        return "LISTENING";
    }
    return "IDLE";
}

static const char *danger_detection_label_text(
    danger_detection_label_t label)
{
    switch (label) {
        case DANGER_DETECTION_LABEL_HORN:
            return "HORN";
        case DANGER_DETECTION_LABEL_SIREN:
            return "SIREN";
        case DANGER_DETECTION_LABEL_NONE:
        default:
            return "NONE";
    }
}

static void danger_detection_format_confidence(float confidence,
                                               char *buffer,
                                               size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return;
    }

    if (confidence <= 0.0f) {
        snprintf(buffer, buffer_size, "--");
        return;
    }

    int confidence_tenths = (int)(confidence * 1000.0f + 0.5f);
    if (confidence_tenths < 0) {
        confidence_tenths = 0;
    }
    if (confidence_tenths > 1000) {
        confidence_tenths = 1000;
    }

    snprintf(buffer, buffer_size, "%d.%d%%", confidence_tenths / 10,
             confidence_tenths % 10);
}

static void danger_detection_copy_text(char *dst,
                                       size_t dst_size,
                                       const char *src)
{
    if (dst == NULL || dst_size == 0U) {
        return;
    }

    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
}

static bool danger_detection_render_cache_matches(
    const danger_detection_render_cache_t *cache,
    const danger_detection_view_model_t *model)
{
    if (cache == NULL || model == NULL || !cache->valid) {
        return false;
    }

    return cache->alert_visible == model->alert_visible &&
           strcmp(cache->status_text, model->status_text) == 0 &&
           strcmp(cache->category_text, model->category_text) == 0 &&
           strcmp(cache->primary_result_text, model->primary_result_text) == 0 &&
           strcmp(cache->horn_confidence_text, model->horn_confidence_text) == 0 &&
           strcmp(cache->siren_confidence_text, model->siren_confidence_text) == 0;
}

static void danger_detection_render_cache_store(
    danger_detection_render_cache_t *cache,
    const danger_detection_view_model_t *model)
{
    if (cache == NULL || model == NULL) {
        return;
    }

    cache->valid = true;
    cache->alert_visible = model->alert_visible;
    danger_detection_copy_text(cache->status_text, sizeof(cache->status_text),
                               model->status_text);
    danger_detection_copy_text(cache->category_text, sizeof(cache->category_text),
                               model->category_text);
    danger_detection_copy_text(cache->primary_result_text,
                               sizeof(cache->primary_result_text),
                               model->primary_result_text);
    danger_detection_copy_text(cache->horn_confidence_text,
                               sizeof(cache->horn_confidence_text),
                               model->horn_confidence_text);
    danger_detection_copy_text(cache->siren_confidence_text,
                               sizeof(cache->siren_confidence_text),
                               model->siren_confidence_text);
}

static void danger_detection_alert_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_alert_visible = false;
    danger_detection_refresh_status();
}

static void danger_detection_back_event(void *user_data)
{
    (void)user_data;

    if (s_ui == NULL || s_ui->screen_main == NULL) {
        ESP_LOGW(TAG, "screen_main not ready when leaving danger page");
        return;
    }

    (void)danger_detection_service_stop(2000U);
    (void)app_alert_manager_set_traffic_audio_overlay_enabled(true);
    s_last_alert_sequence = 0U;
    s_alert_visible = false;
    s_render_cache.valid = false;
    if (s_alert_timer != NULL) {
        lv_timer_pause(s_alert_timer);
    }
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
        return;
    }

    if (s_alert_timer == NULL) {
        s_alert_timer = lv_timer_create(danger_detection_alert_timer_cb,
                                        DANGER_ALERT_DURATION_MS, NULL);
        if (s_alert_timer != NULL) {
            lv_timer_set_repeat_count(s_alert_timer, 1);
            lv_timer_set_auto_delete(s_alert_timer, false);
            lv_timer_pause(s_alert_timer);
        }
    }
}

static void danger_detection_refresh_status(void)
{
    if (s_view == NULL) {
        return;
    }

    const danger_detection_snapshot_t snapshot =
        danger_detection_service_get_snapshot();
    if (snapshot.alert_sequence != 0U &&
        snapshot.alert_sequence != s_last_alert_sequence) {
        s_last_alert_sequence = snapshot.alert_sequence;
        s_alert_visible = true;
        if (s_alert_timer != NULL) {
            lv_timer_set_period(s_alert_timer, DANGER_ALERT_DURATION_MS);
            lv_timer_set_repeat_count(s_alert_timer, 1);
            lv_timer_resume(s_alert_timer);
            lv_timer_reset(s_alert_timer);
        }
    }
    const char *last_result_text =
        danger_detection_label_text(snapshot.last_detected_label);

    char category_text[32];
    char horn_confidence_text[16];
    char siren_confidence_text[16];
    snprintf(category_text, sizeof(category_text), "CURRENT: %s",
             danger_detection_label_text(snapshot.stable_label));

    danger_detection_format_confidence(snapshot.horn_confidence,
                                       horn_confidence_text,
                                       sizeof(horn_confidence_text));
    danger_detection_format_confidence(snapshot.siren_confidence,
                                       siren_confidence_text,
                                       sizeof(siren_confidence_text));

    const danger_detection_view_model_t model = {
        .status_text = danger_detection_status_text(snapshot.state),
        .category_text = category_text,
        .primary_result_text = last_result_text,
        .horn_confidence_text = horn_confidence_text,
        .siren_confidence_text = siren_confidence_text,
        .alert_visible = s_alert_visible,
    };

    if (s_alert_visible && s_render_cache.valid && s_render_cache.alert_visible) {
        return;
    }

    if (danger_detection_render_cache_matches(&s_render_cache, &model)) {
        return;
    }

    danger_detection_view_apply_model(s_view, &model);
    danger_detection_render_cache_store(&s_render_cache, &model);
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

    (void)app_alert_manager_set_traffic_audio_overlay_enabled(false);
    s_last_alert_sequence = 0U;
    s_alert_visible = false;
    s_render_cache.valid = false;
    if (s_alert_timer != NULL) {
        lv_timer_pause(s_alert_timer);
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
