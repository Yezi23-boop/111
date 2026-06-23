#include "memory_watch_controller.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "memory_watch_view.h"
#include "services/memory_watch_service.h"
#include "ui_chinese_fonts.h"

static const char *TAG = "memory_watch_ui";
#define MEMORY_WATCH_CONVERSATION_MAX_ITEMS 12U

typedef struct
{
    bool valid;
    bool voice_button_enabled;
    bool cancel_visible;
    bool cancel_is_clarification;
    memory_watch_view_page_t page;
    memory_watch_view_connection_state_t connection_state;
    size_t conversation_item_count;
    uint32_t conversation_revision;
    size_t inbox_item_count;
    size_t selected_inbox_index;
    uint8_t inbox_unread_count;
    char top_status_text[40];
    char state_text[40];
    char user_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES];
    char reply_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES];
    char voice_button_text[24];
} memory_watch_render_cache_t;

typedef struct
{
    memory_watch_view_conversation_role_t role;
    char request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];
    char text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES];
} memory_watch_conversation_entry_t;

static lv_ui *s_ui = NULL;
static memory_watch_view_t *s_view = NULL;
static lv_timer_t *s_destroy_timer = NULL;
static memory_watch_view_t *s_pending_destroy_view = NULL;
static memory_watch_render_cache_t s_render_cache = {0};
static memory_watch_view_page_t s_render_page = MEMORY_WATCH_VIEW_PAGE_VOICE;
static size_t s_selected_inbox_index = 0;
static memory_watch_conversation_entry_t
    s_conversation_entries[MEMORY_WATCH_CONVERSATION_MAX_ITEMS];
static memory_watch_view_conversation_item_t
    s_conversation_view_items[MEMORY_WATCH_CONVERSATION_MAX_ITEMS];
static size_t s_conversation_item_count = 0;
static uint32_t s_conversation_revision = 0;
static char s_last_user_request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];
static char s_last_reply_request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES];

#ifdef AGENT_PREVIEW_HOST
static memory_watch_view_inbox_item_t s_preview_inbox_items[] = {
    {
        .notification_id = "preview-006",
        .created_at = "今天 16:20",
        .text = "Hermes 已把下午三点取快递整理成提醒. 到点前我会把这件事带回手表.",
        .read = false,
    },
    {
        .notification_id = "preview-005",
        .created_at = "今天 11:42",
        .text = "电池日志可以晚饭后再看. Hermes 建议先确认待机电流和屏幕亮度曲线.",
        .read = false,
    },
    {
        .notification_id = "preview-004",
        .created_at = "昨天 22:08",
        .text = "已经记录 UI 优化方向: 米白画布, 浅边框卡片, 低饱和状态色, 以及更克制的 Hermes 入口.",
        .read = true,
    },
    {
        .notification_id = "preview-003",
        .created_at = "昨天 09:15",
        .text = "提醒: 明天上午检查 watch endpoint 的公网 release gate, 确认私有 health 没有暴露.",
        .read = true,
    },
};
#endif

static void memory_watch_controller_refresh(void);

static const memory_watch_view_inbox_item_t *memory_watch_inbox_items(void)
{
#ifdef AGENT_PREVIEW_HOST
    return s_preview_inbox_items;
#else
    return NULL;
#endif
}

static size_t memory_watch_inbox_item_count(void)
{
#ifdef AGENT_PREVIEW_HOST
    return sizeof(s_preview_inbox_items) / sizeof(s_preview_inbox_items[0]);
#else
    return 0;
#endif
}

static uint8_t memory_watch_inbox_unread_count(void)
{
    uint8_t count = 0;
    const memory_watch_view_inbox_item_t *items = memory_watch_inbox_items();
    const size_t item_count = memory_watch_inbox_item_count();
    for (size_t i = 0; i < item_count; ++i)
    {
        if (!items[i].read && count < UINT8_MAX)
        {
            ++count;
        }
    }
    return count;
}

static void memory_watch_controller_flush_pending_destroy(void)
{
    if (s_destroy_timer != NULL)
    {
        lv_timer_delete(s_destroy_timer);
        s_destroy_timer = NULL;
    }

    if (s_pending_destroy_view != NULL)
    {
        memory_watch_view_destroy(s_pending_destroy_view);
        s_pending_destroy_view = NULL;
    }
}

static void memory_watch_controller_destroy_view_cb(lv_timer_t *timer)
{
    if (timer != NULL)
    {
        lv_timer_delete(timer);
    }
    s_destroy_timer = NULL;

    if (s_pending_destroy_view != NULL)
    {
        memory_watch_view_destroy(s_pending_destroy_view);
        s_pending_destroy_view = NULL;
    }
}

static void memory_watch_controller_schedule_view_destroy(void)
{
    if (s_view == NULL)
    {
        return;
    }

    memory_watch_view_t *view_to_destroy = s_view;
    s_view = NULL;
    s_render_cache.valid = false;

    memory_watch_controller_flush_pending_destroy();

    s_pending_destroy_view = view_to_destroy;
    s_destroy_timer =
        lv_timer_create(memory_watch_controller_destroy_view_cb, 350, NULL);
}

static void memory_watch_copy_text(char *dst, size_t dst_size,
                                   const char *src)
{
    if (dst == NULL || dst_size == 0U)
    {
        return;
    }
    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
}

static void memory_watch_conversation_append(
    memory_watch_view_conversation_role_t role,
    const char *request_id,
    const char *text)
{
    if (text == NULL || text[0] == '\0')
    {
        return;
    }

    if (s_conversation_item_count >= MEMORY_WATCH_CONVERSATION_MAX_ITEMS)
    {
        memmove(&s_conversation_entries[0], &s_conversation_entries[1],
                (MEMORY_WATCH_CONVERSATION_MAX_ITEMS - 1U) *
                    sizeof(s_conversation_entries[0]));
        s_conversation_item_count = MEMORY_WATCH_CONVERSATION_MAX_ITEMS - 1U;
    }

    memory_watch_conversation_entry_t *entry =
        &s_conversation_entries[s_conversation_item_count++];
    entry->role = role;
    memory_watch_copy_text(entry->request_id, sizeof(entry->request_id),
                           request_id);
    memory_watch_copy_text(entry->text, sizeof(entry->text), text);
    ++s_conversation_revision;
}

static const memory_watch_view_conversation_item_t *
memory_watch_conversation_view_items(void)
{
    for (size_t i = 0; i < s_conversation_item_count; ++i)
    {
        s_conversation_view_items[i].role = s_conversation_entries[i].role;
        s_conversation_view_items[i].request_id =
            s_conversation_entries[i].request_id;
        s_conversation_view_items[i].text = s_conversation_entries[i].text;
    }
    return s_conversation_view_items;
}

static void memory_watch_controller_sync_conversation(
    const memory_watch_service_snapshot_t *snapshot)
{
    if (snapshot == NULL || snapshot->request_id[0] == '\0')
    {
        return;
    }

    if (snapshot->asr_text[0] != '\0' &&
        strcmp(s_last_user_request_id, snapshot->request_id) != 0)
    {
        memory_watch_conversation_append(
            MEMORY_WATCH_VIEW_CONVERSATION_USER, snapshot->request_id,
            snapshot->asr_text);
        memory_watch_copy_text(s_last_user_request_id,
                               sizeof(s_last_user_request_id),
                               snapshot->request_id);
    }

    if (snapshot->reply_text[0] != '\0' &&
        strcmp(s_last_reply_request_id, snapshot->request_id) != 0)
    {
        memory_watch_conversation_append(
            MEMORY_WATCH_VIEW_CONVERSATION_HERMES, snapshot->request_id,
            snapshot->reply_text);
        memory_watch_copy_text(s_last_reply_request_id,
                               sizeof(s_last_reply_request_id),
                               snapshot->request_id);
    }
}

static memory_watch_view_connection_state_t memory_watch_connection_state(
    const memory_watch_service_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return MEMORY_WATCH_VIEW_CONNECTION_UNKNOWN;
    }
    if (!snapshot->network_ready || !snapshot->endpoint_configured)
    {
        return MEMORY_WATCH_VIEW_CONNECTION_OFFLINE;
    }
    if (snapshot->hermes_online)
    {
        return MEMORY_WATCH_VIEW_CONNECTION_ONLINE;
    }
    return MEMORY_WATCH_VIEW_CONNECTION_UNKNOWN;
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
           cache->page == model->page &&
           cache->connection_state == model->connection_state &&
           cache->conversation_item_count == model->conversation_item_count &&
           cache->conversation_revision == s_conversation_revision &&
           cache->inbox_item_count == model->inbox_item_count &&
           cache->selected_inbox_index == model->selected_inbox_index &&
           cache->inbox_unread_count == model->inbox_unread_count &&
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
    cache->page = model->page;
    cache->connection_state = model->connection_state;
    cache->conversation_item_count = model->conversation_item_count;
    cache->conversation_revision = s_conversation_revision;
    cache->inbox_item_count = model->inbox_item_count;
    cache->selected_inbox_index = model->selected_inbox_index;
    cache->inbox_unread_count = model->inbox_unread_count;
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
    memory_watch_controller_schedule_view_destroy();
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

static void memory_watch_controller_open_inbox(void *user_data)
{
    (void)user_data;
    s_render_page = MEMORY_WATCH_VIEW_PAGE_INBOX;
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
}

static void memory_watch_controller_open_voice(void *user_data)
{
    (void)user_data;
    s_render_page = MEMORY_WATCH_VIEW_PAGE_VOICE;
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
}

static void memory_watch_controller_inbox_back(void *user_data)
{
    (void)user_data;
    s_render_page = MEMORY_WATCH_VIEW_PAGE_INBOX;
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
}

static void memory_watch_controller_open_inbox_item(size_t index,
                                                    void *user_data)
{
    (void)user_data;
    if (index >= memory_watch_inbox_item_count())
    {
        return;
    }

    s_selected_inbox_index = index;
#ifdef AGENT_PREVIEW_HOST
    s_preview_inbox_items[index].read = true;
#endif
    s_render_page = MEMORY_WATCH_VIEW_PAGE_INBOX_DETAIL;
    s_render_cache.valid = false;
    memory_watch_controller_refresh();
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
        .open_inbox_cb = memory_watch_controller_open_inbox,
        .open_voice_cb = memory_watch_controller_open_voice,
        .inbox_back_cb = memory_watch_controller_inbox_back,
        .open_inbox_item_cb = memory_watch_controller_open_inbox_item,
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
    memory_watch_controller_sync_conversation(&snapshot);

    const bool busy = memory_watch_is_busy_state(snapshot.state);
    const memory_watch_view_model_t model = {
        .top_status_text = memory_watch_top_status_text(&snapshot),
        .state_text = memory_watch_state_text(&snapshot),
        .user_text = user_text,
        .reply_text = reply_text,
        .voice_button_text = memory_watch_voice_button_text(&snapshot),
        .page = s_render_page,
        .conversation_items = memory_watch_conversation_view_items(),
        .conversation_item_count = s_conversation_item_count,
        .connection_state = memory_watch_connection_state(&snapshot),
        .inbox_items = memory_watch_inbox_items(),
        .inbox_item_count = memory_watch_inbox_item_count(),
        .selected_inbox_index = s_selected_inbox_index,
        .inbox_unread_count = memory_watch_inbox_unread_count(),
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
}

void memory_watch_controller_open(void)
{
    memory_watch_controller_ensure_view_created();
    if (s_view == NULL)
    {
        return;
    }

    s_render_page = MEMORY_WATCH_VIEW_PAGE_VOICE;
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
