#include "memory_watch_controller.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "memory_watch_view.h"
#include "services/memory_watch_service.h"
#include "ui_chinese_fonts.h"

static const char *TAG = "memory_watch_ui";

typedef struct
{
    bool valid;
    bool voice_button_enabled;
    bool cancel_visible;
    bool cancel_is_clarification;
    char top_status_text[40];
    char state_text[40];
    char user_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES];
    char reply_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES];
    char voice_button_text[24];
} memory_watch_render_cache_t;

static lv_ui *s_ui = NULL;
static memory_watch_view_t *s_view = NULL;
static lv_obj_t *s_entry_label = NULL;
static lv_obj_t *s_entry_subtitle = NULL;
static memory_watch_render_cache_t s_render_cache = {0};

static void memory_watch_controller_refresh(void);

static void memory_watch_copy_text(char *dst, size_t dst_size,
                                   const char *src)
{
    if (dst == NULL || dst_size == 0U)
    {
        return;
    }
    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
}

static bool memory_watch_is_busy_state(memory_watch_service_state_t state)
{
    return state == MEMORY_WATCH_SERVICE_STATE_RECORDING ||
           state == MEMORY_WATCH_SERVICE_STATE_ENCODING ||
           state == MEMORY_WATCH_SERVICE_STATE_UPLOADING ||
           state == MEMORY_WATCH_SERVICE_STATE_THINKING;
}

static const char *memory_watch_state_text(
    const memory_watch_service_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return "待检测";
    }

    switch (snapshot->state)
    {
    case MEMORY_WATCH_SERVICE_STATE_READY:
        return "待命";
    case MEMORY_WATCH_SERVICE_STATE_WAITING_NETWORK:
        return "等待网络";
    case MEMORY_WATCH_SERVICE_STATE_RECORDING:
        return "正在聆听";
    case MEMORY_WATCH_SERVICE_STATE_ENCODING:
    case MEMORY_WATCH_SERVICE_STATE_UPLOADING:
        return "上传中";
    case MEMORY_WATCH_SERVICE_STATE_THINKING:
        return "思考中";
    case MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION:
        return "需要补充说明";
    case MEMORY_WATCH_SERVICE_STATE_DONE:
        return "已完成";
    case MEMORY_WATCH_SERVICE_STATE_TIMEOUT:
        return "超时";
    case MEMORY_WATCH_SERVICE_STATE_ERROR:
        return "异常";
    case MEMORY_WATCH_SERVICE_STATE_CANCELED:
        return "已取消";
    default:
        return "未知状态";
    }
}

static const char *memory_watch_top_status_text(
    const memory_watch_service_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return "Hermes 待检测";
    }
    if (!snapshot->network_ready)
    {
        return "Hermes 未联网";
    }
    if (!snapshot->endpoint_configured)
    {
        return "Hermes 未配置";
    }
    if (snapshot->hermes_online)
    {
        return "Hermes 在线";
    }
    return "Hermes 待检测";
}

static bool memory_watch_can_use_voice_button(
    const memory_watch_service_snapshot_t *snapshot)
{
    if (snapshot == NULL || !snapshot->network_ready ||
        !snapshot->endpoint_configured)
    {
        return false;
    }

    switch (snapshot->state)
    {
    case MEMORY_WATCH_SERVICE_STATE_READY:
    case MEMORY_WATCH_SERVICE_STATE_RECORDING:
    case MEMORY_WATCH_SERVICE_STATE_DONE:
    case MEMORY_WATCH_SERVICE_STATE_TIMEOUT:
    case MEMORY_WATCH_SERVICE_STATE_ERROR:
    case MEMORY_WATCH_SERVICE_STATE_CANCELED:
    case MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION:
        return true;
    case MEMORY_WATCH_SERVICE_STATE_WAITING_NETWORK:
    case MEMORY_WATCH_SERVICE_STATE_ENCODING:
    case MEMORY_WATCH_SERVICE_STATE_UPLOADING:
    case MEMORY_WATCH_SERVICE_STATE_THINKING:
    default:
        return false;
    }
}

static const char *memory_watch_voice_button_text(
    const memory_watch_service_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return "等待";
    }
    if (!snapshot->network_ready)
    {
        return "等待网络";
    }
    if (!snapshot->endpoint_configured)
    {
        return "未配置";
    }
    if (snapshot->state == MEMORY_WATCH_SERVICE_STATE_RECORDING)
    {
        return "松开发送";
    }
    if (snapshot->state == MEMORY_WATCH_SERVICE_STATE_ENCODING ||
        snapshot->state == MEMORY_WATCH_SERVICE_STATE_UPLOADING ||
        snapshot->state == MEMORY_WATCH_SERVICE_STATE_THINKING)
    {
        return "处理中";
    }
    return "按住说话";
}

static const char *memory_watch_default_reply_text(
    const memory_watch_service_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return "按住按钮说话";
    }
    if (!snapshot->network_ready)
    {
        return "等待 Wi-Fi 后再交给 Hermes";
    }
    if (!snapshot->endpoint_configured)
    {
        return "请先配置 watch endpoint";
    }

    switch (snapshot->state)
    {
    case MEMORY_WATCH_SERVICE_STATE_RECORDING:
        return "松手后发送给 Hermes";
    case MEMORY_WATCH_SERVICE_STATE_ENCODING:
    case MEMORY_WATCH_SERVICE_STATE_UPLOADING:
        return "正在上传语音";
    case MEMORY_WATCH_SERVICE_STATE_THINKING:
        return "Hermes 正在处理";
    case MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION:
        return "Hermes 需要你补充一句";
    case MEMORY_WATCH_SERVICE_STATE_DONE:
        return "已完成";
    case MEMORY_WATCH_SERVICE_STATE_TIMEOUT:
        return "这次等待超时";
    case MEMORY_WATCH_SERVICE_STATE_ERROR:
        return "请求失败";
    case MEMORY_WATCH_SERVICE_STATE_CANCELED:
        return "已取消";
    case MEMORY_WATCH_SERVICE_STATE_READY:
    case MEMORY_WATCH_SERVICE_STATE_WAITING_NETWORK:
    default:
        return "按住按钮说话";
    }
}

static void memory_watch_fill_user_text(
    const memory_watch_service_snapshot_t *snapshot,
    char *buffer,
    size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U)
    {
        return;
    }
    if (snapshot == NULL)
    {
        buffer[0] = '\0';
        return;
    }
    if (snapshot->asr_text[0] != '\0')
    {
        memory_watch_copy_text(buffer, buffer_size, snapshot->asr_text);
        return;
    }

    switch (snapshot->state)
    {
    case MEMORY_WATCH_SERVICE_STATE_RECORDING:
        memory_watch_copy_text(buffer, buffer_size, "正在聆听...");
        break;
    case MEMORY_WATCH_SERVICE_STATE_ENCODING:
        memory_watch_copy_text(buffer, buffer_size, "正在整理语音...");
        break;
    case MEMORY_WATCH_SERVICE_STATE_UPLOADING:
    case MEMORY_WATCH_SERVICE_STATE_THINKING:
        memory_watch_copy_text(buffer, buffer_size, "语音已发送");
        break;
    default:
        buffer[0] = '\0';
        break;
    }
}

static bool memory_watch_render_cache_matches(
    const memory_watch_render_cache_t *cache,
    const memory_watch_view_model_t *model)
{
    if (cache == NULL || model == NULL || !cache->valid)
    {
        return false;
    }

    return cache->voice_button_enabled == model->voice_button_enabled &&
           cache->cancel_visible == model->cancel_visible &&
           cache->cancel_is_clarification == model->cancel_is_clarification &&
           strcmp(cache->top_status_text, model->top_status_text) == 0 &&
           strcmp(cache->state_text, model->state_text) == 0 &&
           strcmp(cache->user_text, model->user_text) == 0 &&
           strcmp(cache->reply_text, model->reply_text) == 0 &&
           strcmp(cache->voice_button_text, model->voice_button_text) == 0;
}

static void memory_watch_render_cache_store(
    memory_watch_render_cache_t *cache,
    const memory_watch_view_model_t *model)
{
    if (cache == NULL || model == NULL)
    {
        return;
    }

    cache->valid = true;
    cache->voice_button_enabled = model->voice_button_enabled;
    cache->cancel_visible = model->cancel_visible;
    cache->cancel_is_clarification = model->cancel_is_clarification;
    memory_watch_copy_text(cache->top_status_text,
                           sizeof(cache->top_status_text),
                           model->top_status_text);
    memory_watch_copy_text(cache->state_text, sizeof(cache->state_text),
                           model->state_text);
    memory_watch_copy_text(cache->user_text, sizeof(cache->user_text),
                           model->user_text);
    memory_watch_copy_text(cache->reply_text, sizeof(cache->reply_text),
                           model->reply_text);
    memory_watch_copy_text(cache->voice_button_text,
                           sizeof(cache->voice_button_text),
                           model->voice_button_text);
}

static void memory_watch_controller_back(void *user_data)
{
    (void)user_data;

    if (s_ui == NULL || s_ui->screen_main == NULL || s_view == NULL)
    {
        return;
    }

    memory_watch_service_snapshot_t snapshot = {0};
    if (memory_watch_service_get_snapshot(&snapshot) == ESP_OK)
    {
        if (snapshot.state == MEMORY_WATCH_SERVICE_STATE_RECORDING ||
            snapshot.state == MEMORY_WATCH_SERVICE_STATE_ENCODING)
        {
            (void)memory_watch_service_cancel_recording();
        }
        else if (snapshot.state == MEMORY_WATCH_SERVICE_STATE_UPLOADING ||
                 snapshot.state == MEMORY_WATCH_SERVICE_STATE_THINKING)
        {
            (void)memory_watch_service_cancel_waiting();
        }
        else if (snapshot.state ==
                 MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION)
        {
            (void)memory_watch_service_cancel_clarification();
        }
    }

    s_render_cache.valid = false;
    lv_screen_load_anim(s_ui->screen_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,
                        false);
}

static void memory_watch_controller_press_start(void *user_data)
{
    (void)user_data;
    (void)memory_watch_service_begin_recording();
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
}

static void memory_watch_controller_release_send(void *user_data)
{
    (void)user_data;
    (void)memory_watch_service_send_recording();
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
}

static void memory_watch_controller_slide_cancel(void *user_data)
{
    (void)user_data;
    (void)memory_watch_service_cancel_recording();
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
}

static void memory_watch_controller_cancel_waiting(void *user_data)
{
    (void)user_data;
    (void)memory_watch_service_cancel_waiting();
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
}

static void memory_watch_controller_cancel_clarification(void *user_data)
{
    (void)user_data;
    (void)memory_watch_service_cancel_clarification();
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
}

static void memory_watch_controller_entry_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        memory_watch_controller_open();
    }
}

static void memory_watch_controller_bind_entry(lv_ui *ui)
{
    if (ui == NULL || ui->screen_main_option_8 == NULL)
    {
        return;
    }

    lv_obj_add_flag(ui->screen_main_option_8, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(ui->screen_main_option_8, lv_color_hex(0x0f766e),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(ui->screen_main_option_8,
                                  lv_color_hex(0x0d9488),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(ui->screen_main_option_8,
                        memory_watch_controller_entry_event,
                        LV_EVENT_CLICKED, NULL);

    if (ui->screen_main_user != NULL)
    {
        lv_obj_add_event_cb(ui->screen_main_user,
                            memory_watch_controller_entry_event,
                            LV_EVENT_CLICKED, NULL);
    }

    s_entry_label = lv_label_create(ui->screen_main_option_8);
    lv_label_set_text(s_entry_label, "Hermes");
    lv_obj_set_pos(s_entry_label, 96, 18);
    lv_obj_set_style_text_color(s_entry_label, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_entry_label, &lv_font_montserratMedium_27,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_entry_label, LV_OBJ_FLAG_CLICKABLE);

    s_entry_subtitle = lv_label_create(ui->screen_main_option_8);
    lv_label_set_text(s_entry_subtitle, "记忆手表");
    lv_obj_set_pos(s_entry_subtitle, 98, 52);
    lv_obj_set_style_text_color(s_entry_subtitle, lv_color_hex(0xccfbf1),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(
        s_entry_subtitle, &lv_font_montserrat_lxgw_tghz_level1_3500_16_4,
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(s_entry_subtitle, LV_OBJ_FLAG_CLICKABLE);
}

static bool memory_watch_controller_is_foreground(void)
{
    return s_view != NULL &&
           lv_screen_active() == memory_watch_view_get_screen(s_view);
}

static void memory_watch_controller_ensure_view_created(void)
{
    if (s_view != NULL)
    {
        return;
    }

    static const memory_watch_view_config_t kConfig = {
        .back_cb = memory_watch_controller_back,
        .press_start_cb = memory_watch_controller_press_start,
        .release_send_cb = memory_watch_controller_release_send,
        .slide_cancel_cb = memory_watch_controller_slide_cancel,
        .cancel_waiting_cb = memory_watch_controller_cancel_waiting,
        .cancel_clarification_cb = memory_watch_controller_cancel_clarification,
        .user_data = NULL,
    };

    s_view = memory_watch_view_create(&kConfig);
    if (s_view == NULL)
    {
        ESP_LOGE(TAG, "memory_watch_view_create failed");
    }
}

static void memory_watch_controller_refresh(void)
{
    if (s_view == NULL)
    {
        return;
    }

    memory_watch_service_snapshot_t snapshot = {0};
    const esp_err_t err = memory_watch_service_get_snapshot(&snapshot);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "get memory watch snapshot failed: %s",
                 esp_err_to_name(err));
        return;
    }

    char user_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES];
    char reply_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES];
    memory_watch_fill_user_text(&snapshot, user_text, sizeof(user_text));
    memory_watch_copy_text(reply_text, sizeof(reply_text),
                           snapshot.reply_text[0] != '\0'
                               ? snapshot.reply_text
                               : memory_watch_default_reply_text(&snapshot));

    const bool busy = memory_watch_is_busy_state(snapshot.state);
    const memory_watch_view_model_t model = {
        .top_status_text = memory_watch_top_status_text(&snapshot),
        .state_text = memory_watch_state_text(&snapshot),
        .user_text = user_text,
        .reply_text = reply_text,
        .voice_button_text = memory_watch_voice_button_text(&snapshot),
        .voice_button_enabled = memory_watch_can_use_voice_button(&snapshot),
        .cancel_visible =
            busy ||
            snapshot.state == MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION,
        .cancel_is_clarification =
            snapshot.state == MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION,
    };

    if (memory_watch_render_cache_matches(&s_render_cache, &model))
    {
        return;
    }
    memory_watch_view_apply_model(s_view, &model);
    memory_watch_render_cache_store(&s_render_cache, &model);
}

void memory_watch_controller_init(lv_ui *ui)
{
    s_ui = ui;
    memory_watch_controller_bind_entry(ui);
}

void memory_watch_controller_open(void)
{
    memory_watch_controller_ensure_view_created();
    if (s_view == NULL)
    {
        return;
    }

    (void)memory_watch_service_check_health();
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
    lv_screen_load_anim(memory_watch_view_get_screen(s_view),
                        LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void memory_watch_controller_poll_ui(void)
{
    if (!memory_watch_controller_is_foreground())
    {
        return;
    }
    memory_watch_controller_refresh();
}
