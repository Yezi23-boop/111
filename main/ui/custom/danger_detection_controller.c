#include "danger_detection_controller.h"

#include <stdio.h>
#include <string.h>

#include "features/alerts/app_alert_manager.h"
#include "features/danger_detection/danger_detection_service.h"
#include "danger_detection_view.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "danger_ui";

static lv_ui *s_ui = NULL;
static danger_detection_view_t *s_view = NULL;

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

static const char *danger_detection_status_text(
    const danger_detection_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return "IDLE";
    }

    const danger_detection_state_t state = snapshot->state;
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
        switch (snapshot->risk_state) {
            case DANGER_DETECTION_RISK_ALERTING:
                return "ALERTING";
            case DANGER_DETECTION_RISK_SUSPICIOUS:
                return "CHECKING";
            case DANGER_DETECTION_RISK_COOLDOWN:
                return "COOLDOWN";
            case DANGER_DETECTION_RISK_MONITORING:
            case DANGER_DETECTION_RISK_OFF:
            default:
                break;
        }
        return "LISTENING";
    }
    return "IDLE";
}

/**
 * @brief 将服务层危险标签转换成页面展示文本。
 *
 * @param[in] label 服务层发布的稳定标签或最近触发标签。
 * @return 静态字符串；未知值按 NONE 处理，避免 UI 显示未初始化文本。
 */
static const char *danger_detection_label_text(danger_detection_label_t label)
{
    switch (label) {
        case DANGER_DETECTION_LABEL_HORN:
            return "HORN";
        case DANGER_DETECTION_LABEL_SIREN:
            return "SIREN";
        case DANGER_DETECTION_LABEL_DANGER:
            return "DANGER";
        case DANGER_DETECTION_LABEL_NONE:
        default:
            return "NONE";
    }
}

static bool danger_detection_page_alert_visible(
    const danger_detection_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    /*
     * 专页上的红色危险态跟随服务层风险状态机，而不是本地 2 秒计时器。
     * 这样页面显示、全局提醒和后处理状态使用同一个生命周期。
     */
    return snapshot->state == DANGER_DETECTION_STATE_RUNNING &&
           snapshot->risk_state == DANGER_DETECTION_RISK_ALERTING;
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

static void danger_detection_back_event(void *user_data)
{
    (void)user_data;

    if (s_ui == NULL || s_ui->screen_main == NULL) {
        ESP_LOGW(TAG, "screen_main not ready when leaving danger page");
        return;
    }

    (void)danger_detection_service_stop(2000U);
    (void)app_alert_manager_set_traffic_audio_overlay_enabled(true);
    s_render_cache.valid = false;
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
}

static void danger_detection_refresh_status(void)
{
    if (s_view == NULL) {
        return;
    }

    const danger_detection_snapshot_t snapshot =
        danger_detection_service_get_snapshot();
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
        .status_text = danger_detection_status_text(&snapshot),
        .category_text = category_text,
        .primary_result_text = last_result_text,
        .horn_confidence_text = horn_confidence_text,
        .siren_confidence_text = siren_confidence_text,
        .alert_visible = danger_detection_page_alert_visible(&snapshot),
    };

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

/**
 * @brief 打开危险识别页面并启动 ESP-DL 单模型后端。
 *
 * 页面入口属于 UI 语义层，只表达“用户要开始危险识别”；实际音频资源、模型
 * 和告警状态由 danger_detection_service 统一编排。这里显式选择 ESP-DL 后端，
 * 避免误走旧 Edge Impulse 调试链路。
 */
void danger_detection_ui_open(void)
{
    danger_detection_ensure_screen_created();
    if (s_view == NULL) {
        return;
    }

    (void)app_alert_manager_set_traffic_audio_overlay_enabled(false);
    s_render_cache.valid = false;
    (void)danger_detection_service_start_with_backend(
        DANGER_DETECTION_BACKEND_ESPDL);
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
