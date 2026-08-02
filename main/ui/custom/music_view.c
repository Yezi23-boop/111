#include "music_view.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "ui_chinese_fonts.h"

static const lv_coord_t kQrCanvasSize = 220;

/** UI 仅缓存最近三批目录数据；曲目正文放在 PSRAM。 */
#define MUSIC_VIEW_CATALOG_CACHE_CAPACITY (MUSIC_SERVICE_CATALOG_PAGE_SIZE * 3U)
#define MUSIC_VIEW_CATALOG_SCROLL_THRESHOLD 12
#define MUSIC_VIEW_CATALOG_ROW_HEIGHT 44
#define MUSIC_VIEW_CATALOG_ROW_GAP 8

typedef struct
{
    music_view_t *view;
    uint8_t index;
} music_view_source_context_t;

typedef struct
{
    music_view_t *view;
    uint8_t index;
} music_view_track_context_t;

struct music_view
{
    lv_obj_t *screen;
    lv_obj_t *state_label;
    lv_obj_t *track_label;
    lv_obj_t *artist_label;
    lv_obj_t *section_label;
    lv_obj_t *source_buttons[4];
    lv_obj_t *catalog_list;
    lv_obj_t *track_buttons[MUSIC_VIEW_CATALOG_CACHE_CAPACITY];
    lv_obj_t *catalog_back_button;
    lv_obj_t *account_button;
    lv_obj_t *account_status;
    lv_obj_t *qr_canvas;
    lv_obj_t *track_panel;
    lv_obj_t *previous_button;
    lv_obj_t *next_button;
    uint8_t *qr_buffer;
    lv_obj_t *toggle_button;
    lv_obj_t *toggle_label;
    lv_obj_t *mode_button;
    lv_obj_t *mode_label;
    music_view_source_context_t source_context[4];
    music_view_track_context_t track_context[MUSIC_VIEW_CATALOG_CACHE_CAPACITY];
    music_service_catalog_track_t *catalog_tracks;
    char catalog_source_id[MUSIC_SERVICE_SOURCE_ID_MAX_BYTES];
    size_t catalog_count;
    uint32_t catalog_total;
    uint32_t catalog_next_offset;
    uint32_t catalog_generation;
    music_view_config_t config;
    bool catalog_visible;
    bool account_visible;
    bool catalog_snapshot_applied;
    bool catalog_load_pending;
};

static const char *const kSourceLabels[4] = {
    "今日推荐", "我喜欢", "我的歌单", "最近播放",
};

static void music_view_text_style(lv_obj_t *obj, lv_color_t color,
                                  const lv_font_t *font)
{
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void music_view_panel_style(lv_obj_t *obj, lv_color_t background)
{
    lv_obj_set_style_radius(obj, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, background, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *music_view_button(lv_obj_t *parent, const char *text,
                                   lv_coord_t x, lv_coord_t y,
                                   lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_pos(button, x, y);
    music_view_panel_style(button, lv_color_hex(0x20242e));
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text != NULL ? text : "");
    lv_obj_set_width(label, width - 16);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    music_view_text_style(label, lv_color_hex(0xf5f7fa),
                          &lv_font_montserrat_lxgw_tghz_level1_3500_16_4);
    lv_obj_center(label);
    return button;
}

static void music_view_back_event(lv_event_t *event)
{
    music_view_t *view = (music_view_t *)lv_event_get_user_data(event);
    if (view != NULL && view->config.back_cb != NULL)
    {
        view->config.back_cb(view->config.user_data);
    }
}

static void music_view_source_event(lv_event_t *event)
{
    music_view_source_context_t *context =
        (music_view_source_context_t *)lv_event_get_user_data(event);
    if (context != NULL && context->view != NULL &&
        context->view->config.source_cb != NULL)
    {
        context->view->config.source_cb(context->index,
                                        context->view->config.user_data);
    }
}

static void music_view_track_event(lv_event_t *event)
{
    music_view_track_context_t *context =
        (music_view_track_context_t *)lv_event_get_user_data(event);
    if (context != NULL && context->view != NULL &&
        context->view->config.track_cb != NULL &&
        context->index < context->view->catalog_count)
    {
        const music_service_catalog_track_t *track =
            &context->view->catalog_tracks[context->index];
        context->view->config.track_cb(context->view->catalog_source_id,
                                       track->track_id,
                                       context->view->config.user_data);
    }
}

static void music_view_catalog_back_event(lv_event_t *event)
{
    music_view_t *view = (music_view_t *)lv_event_get_user_data(event);
    if (view != NULL && view->config.catalog_back_cb != NULL)
    {
        view->config.catalog_back_cb(view->config.user_data);
    }
}

static void music_view_catalog_scroll_event(lv_event_t *event)
{
    music_view_t *view = (music_view_t *)lv_event_get_user_data(event);
    if (view == NULL || !view->catalog_visible || view->catalog_load_pending ||
        view->config.catalog_load_more_cb == NULL ||
        view->catalog_source_id[0] == '\0' || view->catalog_total == 0U ||
        view->catalog_next_offset >= view->catalog_total ||
        lv_obj_get_scroll_bottom(view->catalog_list) >
            MUSIC_VIEW_CATALOG_SCROLL_THRESHOLD)
    {
        return;
    }
    view->catalog_load_pending = true;
    view->config.catalog_load_more_cb(view->catalog_source_id,
                                      view->catalog_next_offset,
                                      view->config.user_data);
}

static void music_view_account_event(lv_event_t *event)
{
    music_view_t *view = (music_view_t *)lv_event_get_user_data(event);
    if (view != NULL && view->config.account_cb != NULL)
    {
        view->config.account_cb(view->config.user_data);
    }
}

static void music_view_toggle_event(lv_event_t *event)
{
    music_view_t *view = (music_view_t *)lv_event_get_user_data(event);
    if (view != NULL && view->config.toggle_cb != NULL)
    {
        view->config.toggle_cb(view->config.user_data);
    }
}

static void music_view_previous_event(lv_event_t *event)
{
    music_view_t *view = (music_view_t *)lv_event_get_user_data(event);
    if (view != NULL && view->config.previous_cb != NULL)
    {
        view->config.previous_cb(view->config.user_data);
    }
}

static void music_view_next_event(lv_event_t *event)
{
    music_view_t *view = (music_view_t *)lv_event_get_user_data(event);
    if (view != NULL && view->config.next_cb != NULL)
    {
        view->config.next_cb(view->config.user_data);
    }
}

static void music_view_mode_event(lv_event_t *event)
{
    music_view_t *view = (music_view_t *)lv_event_get_user_data(event);
    if (view == NULL || view->config.mode_cb == NULL)
    {
        return;
    }
    /* controller sends the mode command through the service owner. */
    view->config.mode_cb(MUSIC_SERVICE_MODE_REPEAT_ALL,
                         view->config.user_data);
}

static const char *music_view_state_text(const music_service_snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return "状态未知";
    }
    switch (snapshot->state)
    {
    case MUSIC_SERVICE_STATE_BUFFERING:
        return "正在缓冲";
    case MUSIC_SERVICE_STATE_PLAYING:
        return "正在播放";
    case MUSIC_SERVICE_STATE_PAUSED:
        return "已暂停";
    case MUSIC_SERVICE_STATE_ERROR:
        return "播放失败";
    case MUSIC_SERVICE_STATE_STOPPED:
    default:
        return "未播放";
    }
}

static const char *music_view_mode_text(music_service_mode_t mode)
{
    switch (mode)
    {
    case MUSIC_SERVICE_MODE_REPEAT_ONE:
        return "单曲循环";
    case MUSIC_SERVICE_MODE_SHUFFLE:
        return "随机播放";
    case MUSIC_SERVICE_MODE_REPEAT_ALL:
    default:
        return "列表循环";
    }
}

/** 清空滚动目录，保留已经创建的 LVGL 行对象供后续复用。 */
static void music_view_reset_catalog(music_view_t *view)
{
    view->catalog_count = 0U;
    view->catalog_total = 0U;
    view->catalog_next_offset = 0U;
    view->catalog_generation = 0U;
    view->catalog_snapshot_applied = false;
    view->catalog_load_pending = false;
    memset(view->catalog_source_id, 0, sizeof(view->catalog_source_id));
    memset(view->catalog_tracks, 0,
           sizeof(*view->catalog_tracks) * MUSIC_VIEW_CATALOG_CACHE_CAPACITY);
    for (size_t i = 0U; i < MUSIC_VIEW_CATALOG_CACHE_CAPACITY; ++i)
    {
        lv_label_set_text(lv_obj_get_child(view->track_buttons[i], 0), "");
        lv_obj_add_flag(view->track_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_scroll_to_y(view->catalog_list, 0, LV_ANIM_OFF);
}

/** 将 PSRAM 目录缓存映射到固定的可复用歌曲行。 */
static void music_view_render_catalog(music_view_t *view)
{
    for (size_t i = 0U; i < MUSIC_VIEW_CATALOG_CACHE_CAPACITY; ++i)
    {
        if (i < view->catalog_count)
        {
            lv_label_set_text_fmt(
                lv_obj_get_child(view->track_buttons[i], 0), "%s - %s",
                view->catalog_tracks[i].title, view->catalog_tracks[i].artist);
            lv_obj_remove_flag(view->track_buttons[i], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_label_set_text(lv_obj_get_child(view->track_buttons[i], 0), "");
            lv_obj_add_flag(view->track_buttons[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

music_view_t *music_view_create(const music_view_config_t *config)
{
    music_view_t *view = (music_view_t *)heap_caps_calloc(
        1U, sizeof(*view), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (view == NULL)
    {
        return NULL;
    }
    view->catalog_tracks = (music_service_catalog_track_t *)heap_caps_calloc(
        MUSIC_VIEW_CATALOG_CACHE_CAPACITY, sizeof(*view->catalog_tracks),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (view->catalog_tracks == NULL)
    {
        heap_caps_free(view);
        return NULL;
    }
    if (config != NULL)
    {
        view->config = *config;
    }

    view->screen = lv_obj_create(NULL);
    if (view->screen == NULL)
    {
        heap_caps_free(view->catalog_tracks);
        heap_caps_free(view);
        return NULL;
    }
    lv_obj_set_size(view->screen, 410, 502);
    lv_obj_set_style_bg_color(view->screen, lv_color_hex(0x0b0d12),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(view->screen, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(view->screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = music_view_button(view->screen, "返回", 40, 20, 58, 40);
    lv_obj_add_event_cb(back, music_view_back_event, LV_EVENT_CLICKED, view);

    lv_obj_t *title = lv_label_create(view->screen);
    lv_label_set_text(title, "音乐");
    lv_obj_set_pos(title, 112, 24);
    lv_obj_set_size(title, 186, 32);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    music_view_text_style(title, lv_color_hex(0xf5f7fa),
                          &lv_font_montserrat_lxgw_tghz_level1_3500_27_4);

    view->account_button = music_view_button(view->screen, "登录", 304, 20,
                                              66, 40);
    lv_obj_add_event_cb(view->account_button, music_view_account_event,
                        LV_EVENT_CLICKED, view);

    view->account_status = lv_label_create(view->screen);
    lv_label_set_text(view->account_status, "");
    lv_obj_set_pos(view->account_status, 40, 82);
    lv_obj_set_size(view->account_status, 330, 32);
    lv_obj_set_style_text_align(view->account_status, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(view->account_status, LV_LABEL_LONG_DOT);
    music_view_text_style(view->account_status, lv_color_hex(0xa7afbd),
                          &lv_font_montserrat_lxgw_tghz_level1_3500_16_4);

    view->qr_buffer = heap_caps_calloc(
        (size_t)kQrCanvasSize * (size_t)kQrCanvasSize, 2U,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    view->qr_canvas = lv_canvas_create(view->screen);
    if (view->qr_canvas != NULL && view->qr_buffer != NULL)
    {
        lv_canvas_set_buffer(view->qr_canvas, view->qr_buffer, kQrCanvasSize,
                             kQrCanvasSize, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(view->qr_canvas, lv_color_white(), LV_OPA_COVER);
        lv_obj_set_pos(view->qr_canvas, 95, 132);
    }
    else
    {
        if (view->qr_canvas != NULL)
        {
            lv_obj_del(view->qr_canvas);
            view->qr_canvas = NULL;
        }
        heap_caps_free(view->qr_buffer);
        view->qr_buffer = NULL;
    }
    lv_obj_add_flag(view->account_status, LV_OBJ_FLAG_HIDDEN);
    if (view->qr_canvas != NULL)
    {
        lv_obj_add_flag(view->qr_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    view->track_panel = lv_obj_create(view->screen);
    lv_obj_set_size(view->track_panel, 330, 100);
    lv_obj_set_pos(view->track_panel, 40, 76);
    music_view_panel_style(view->track_panel, lv_color_hex(0x171a22));
    lv_obj_set_style_radius(view->track_panel, 20,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(view->track_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *artwork = lv_obj_create(view->track_panel);
    lv_obj_set_size(artwork, 72, 72);
    lv_obj_set_pos(artwork, 14, 14);
    lv_obj_set_style_radius(artwork, LV_RADIUS_CIRCLE,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(artwork, lv_color_hex(0x755cff),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(artwork, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(artwork, 2,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(artwork, lv_color_hex(0x9a8aff),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(artwork, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *artwork_center = lv_obj_create(artwork);
    lv_obj_set_size(artwork_center, 18, 18);
    lv_obj_center(artwork_center);
    lv_obj_set_style_radius(artwork_center, LV_RADIUS_CIRCLE,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(artwork_center, lv_color_hex(0x171a22),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(artwork_center, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(artwork_center, 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(artwork_center, LV_OBJ_FLAG_SCROLLABLE);

    view->track_label = lv_label_create(view->track_panel);
    lv_label_set_text(view->track_label, "未选择歌曲");
    lv_obj_set_pos(view->track_label, 100, 13);
    lv_obj_set_size(view->track_label, 214, 27);
    lv_label_set_long_mode(view->track_label, LV_LABEL_LONG_DOT);
    music_view_text_style(view->track_label, lv_color_hex(0xf5f7fa),
                          &lv_font_montserrat_lxgw_tghz_level1_3500_22_4);

    view->artist_label = lv_label_create(view->track_panel);
    lv_label_set_text(view->artist_label, "");
    lv_obj_set_pos(view->artist_label, 100, 44);
    lv_obj_set_size(view->artist_label, 214, 20);
    lv_label_set_long_mode(view->artist_label, LV_LABEL_LONG_DOT);
    music_view_text_style(view->artist_label, lv_color_hex(0x969ead),
                          &lv_font_montserrat_lxgw_tghz_level1_3500_16_4);

    view->state_label = lv_label_create(view->track_panel);
    lv_label_set_text(view->state_label, "未播放");
    lv_obj_set_pos(view->state_label, 100, 70);
    lv_obj_set_size(view->state_label, 214, 20);
    lv_label_set_long_mode(view->state_label, LV_LABEL_LONG_DOT);
    music_view_text_style(view->state_label, lv_color_hex(0x5de2a5),
                          &lv_font_montserrat_lxgw_tghz_level1_3500_16_4);

    view->section_label = lv_label_create(view->screen);
    lv_label_set_text(view->section_label, "音乐来源");
    lv_obj_set_pos(view->section_label, 40, 284);
    lv_obj_set_size(view->section_label, 120, 30);
    music_view_text_style(view->section_label, lv_color_hex(0xf5f7fa),
                          &lv_font_montserrat_lxgw_tghz_level1_3500_22_4);

    for (uint8_t i = 0; i < 4; ++i)
    {
        view->source_context[i].view = view;
        view->source_context[i].index = i;
        view->source_buttons[i] = music_view_button(
            view->screen, kSourceLabels[i],
            (lv_coord_t)(40 + (i % 2U) * 170),
            (lv_coord_t)(326 + (i / 2U) * 62), 160, 50);
        lv_obj_add_event_cb(view->source_buttons[i], music_view_source_event,
                            LV_EVENT_CLICKED, &view->source_context[i]);
    }

    view->catalog_list = lv_obj_create(view->screen);
    lv_obj_set_size(view->catalog_list, 330, 146);
    lv_obj_set_pos(view->catalog_list, 40, 326);
    music_view_panel_style(view->catalog_list, lv_color_hex(0x0b0d12));
    lv_obj_set_style_bg_opa(view->catalog_list, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(view->catalog_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(view->catalog_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(view->catalog_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view->catalog_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(view->catalog_list, 0,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(view->catalog_list, MUSIC_VIEW_CATALOG_ROW_GAP,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(view->catalog_list, music_view_catalog_scroll_event,
                        LV_EVENT_SCROLL_END, view);

    for (uint8_t i = 0; i < MUSIC_VIEW_CATALOG_CACHE_CAPACITY; ++i)
    {
        view->track_context[i].view = view;
        view->track_context[i].index = i;
        view->track_buttons[i] = music_view_button(
            view->catalog_list, "", 0, 0, 318,
            MUSIC_VIEW_CATALOG_ROW_HEIGHT);
        lv_obj_set_style_text_align(lv_obj_get_child(view->track_buttons[i], 0),
                                    LV_TEXT_ALIGN_LEFT,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(view->track_buttons[i], music_view_track_event,
                            LV_EVENT_CLICKED, &view->track_context[i]);
        lv_obj_add_flag(view->track_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }

    view->catalog_back_button = music_view_button(view->screen, "来源", 276,
                                                   280, 94, 34);
    lv_obj_add_event_cb(view->catalog_back_button, music_view_catalog_back_event,
                        LV_EVENT_CLICKED, view);
    lv_obj_add_flag(view->catalog_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->catalog_back_button, LV_OBJ_FLAG_HIDDEN);

    view->previous_button = music_view_button(view->screen, "<", 40, 196,
                                              64, 64);
    lv_obj_set_style_radius(view->previous_button, LV_RADIUS_CIRCLE,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(view->previous_button, music_view_previous_event,
                        LV_EVENT_CLICKED, view);
    view->toggle_button = music_view_button(view->screen, "播放", 169, 190,
                                            72, 72);
    lv_obj_set_style_radius(view->toggle_button, LV_RADIUS_CIRCLE,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(view->toggle_button, lv_color_hex(0x5de2a5),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    view->toggle_label = lv_obj_get_child(view->toggle_button, 0);
    music_view_text_style(view->toggle_label, lv_color_hex(0x0b0d12),
                          &lv_font_montserrat_lxgw_tghz_level1_3500_16_4);
    lv_obj_add_event_cb(view->toggle_button, music_view_toggle_event,
                        LV_EVENT_CLICKED, view);
    view->next_button = music_view_button(view->screen, ">", 306, 196,
                                          64, 64);
    lv_obj_set_style_radius(view->next_button, LV_RADIUS_CIRCLE,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(view->next_button, music_view_next_event,
                        LV_EVENT_CLICKED, view);

    view->mode_button = music_view_button(view->screen, "列表循环", 174, 280,
                                          92, 34);
    view->mode_label = lv_obj_get_child(view->mode_button, 0);
    lv_obj_add_event_cb(view->mode_button, music_view_mode_event,
                        LV_EVENT_CLICKED, view);

    return view;
}

void music_view_destroy(music_view_t *view)
{
    if (view == NULL)
    {
        return;
    }
    if (view->screen != NULL)
    {
        lv_obj_del(view->screen);
    }
    heap_caps_free(view->qr_buffer);
    heap_caps_free(view->catalog_tracks);
    heap_caps_free(view);
}

lv_obj_t *music_view_get_screen(const music_view_t *view)
{
    return view != NULL ? view->screen : NULL;
}

void music_view_apply_snapshot(music_view_t *view,
                               const music_service_snapshot_t *snapshot)
{
    if (view == NULL || snapshot == NULL)
    {
        return;
    }
    lv_label_set_text(view->state_label, music_view_state_text(snapshot));
    lv_label_set_text(view->track_label,
                      snapshot->title[0] != '\0' ? snapshot->title : "未选择歌曲");
    lv_label_set_text(view->artist_label, snapshot->artist);
    lv_label_set_text(view->toggle_label,
                      snapshot->state == MUSIC_SERVICE_STATE_PLAYING ||
                              snapshot->state == MUSIC_SERVICE_STATE_BUFFERING
                          ? "暂停"
                          : "播放");
    lv_label_set_text(view->mode_label, music_view_mode_text(snapshot->mode));
}

void music_view_show_catalog_loading(music_view_t *view, const char *source_id)
{
    if (view == NULL)
    {
        return;
    }
    music_view_reset_catalog(view);
    snprintf(view->catalog_source_id, sizeof(view->catalog_source_id), "%s",
             source_id != NULL ? source_id : "");
    view->catalog_visible = true;
    view->account_visible = false;
    lv_obj_remove_flag(view->track_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->state_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->previous_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->toggle_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->next_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->account_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->account_status, LV_OBJ_FLAG_HIDDEN);
    if (view->qr_canvas != NULL)
    {
        lv_obj_add_flag(view->qr_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t i = 0; i < 4U; ++i)
    {
        lv_obj_add_flag(view->source_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(view->catalog_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->track_buttons[0], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lv_obj_get_child(view->track_buttons[0], 0),
                      "正在加载歌曲");
    lv_label_set_text(view->section_label, "播放队列");
    lv_obj_remove_flag(view->section_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(view->mode_button, 174, 280);
    lv_obj_set_size(view->mode_button, 92, 34);
    lv_obj_remove_flag(view->mode_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->catalog_back_button, LV_OBJ_FLAG_HIDDEN);
}

void music_view_show_sources(music_view_t *view)
{
    if (view == NULL)
    {
        return;
    }
    view->catalog_visible = false;
    view->account_visible = false;
    lv_obj_remove_flag(view->track_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->state_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->previous_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->toggle_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->next_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view->account_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->account_status, LV_OBJ_FLAG_HIDDEN);
    if (view->qr_canvas != NULL)
    {
        lv_obj_add_flag(view->qr_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    for (size_t i = 0; i < 4U; ++i)
    {
        lv_obj_remove_flag(view->source_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(view->section_label, "音乐来源");
    lv_obj_remove_flag(view->section_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->catalog_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->catalog_back_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(view->mode_button, 245, 280);
    lv_obj_set_size(view->mode_button, 125, 34);
    lv_obj_remove_flag(view->mode_button, LV_OBJ_FLAG_HIDDEN);
}

void music_view_show_account_loading(music_view_t *view)
{
    if (view == NULL)
    {
        return;
    }
    view->account_visible = true;
    view->catalog_visible = false;
    lv_obj_add_flag(view->track_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->state_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->previous_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->next_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->section_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(view->account_status, "正在获取二维码");
    lv_obj_remove_flag(view->account_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->account_button, LV_OBJ_FLAG_HIDDEN);
    if (view->qr_canvas != NULL)
    {
        lv_obj_remove_flag(view->qr_canvas, LV_OBJ_FLAG_HIDDEN);
        lv_canvas_fill_bg(view->qr_canvas, lv_color_white(), LV_OPA_COVER);
    }
    for (size_t i = 0; i < 4U; ++i)
    {
        lv_obj_add_flag(view->source_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(view->catalog_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->catalog_back_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->toggle_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->mode_button, LV_OBJ_FLAG_HIDDEN);
}

void music_view_apply_account(
    music_view_t *view, const music_service_account_snapshot_t *account,
    const uint8_t *qr_data, uint16_t qr_size, size_t qr_bytes)
{
    if (view == NULL || account == NULL || !view->account_visible)
    {
        return;
    }
    const char *text = "登录状态未知";
    switch (account->state)
    {
    case MUSIC_SERVICE_ACCOUNT_QR_PENDING:
        text = "请使用网易云音乐扫描二维码";
        break;
    case MUSIC_SERVICE_ACCOUNT_QR_CONFIRMING:
        text = "已扫码，请在手机上确认";
        break;
    case MUSIC_SERVICE_ACCOUNT_LOGGED_IN:
        text = "登录成功";
        break;
    case MUSIC_SERVICE_ACCOUNT_LOGGED_OUT:
        text = "当前未登录";
        break;
    case MUSIC_SERVICE_ACCOUNT_EXPIRED:
        text = "二维码已过期，请重新获取";
        break;
    case MUSIC_SERVICE_ACCOUNT_ERROR:
        text = "二维码获取失败，请重试";
        break;
    default:
        break;
    }
    lv_label_set_text(view->account_status, text);
    if (view->qr_canvas == NULL || qr_data == NULL || qr_size == 0U ||
        qr_bytes == 0U || qr_size > kQrCanvasSize)
    {
        return;
    }
    lv_canvas_fill_bg(view->qr_canvas, lv_color_white(), LV_OPA_COVER);
    const lv_coord_t scale = kQrCanvasSize / qr_size;
    if (scale == 0)
    {
        return;
    }
    const lv_coord_t drawn = (lv_coord_t)qr_size * scale;
    const lv_coord_t margin = (kQrCanvasSize - drawn) / 2;
    for (uint16_t y = 0; y < qr_size; ++y)
    {
        for (uint16_t x = 0; x < qr_size; ++x)
        {
            const size_t bit = (size_t)y * qr_size + x;
            if (bit / 8U >= qr_bytes ||
                (qr_data[bit / 8U] & (uint8_t)(1U << (7U - bit % 8U))) == 0U)
            {
                continue;
            }
            for (lv_coord_t dy = 0; dy < scale; ++dy)
            {
                for (lv_coord_t dx = 0; dx < scale; ++dx)
                {
                    lv_canvas_set_px(view->qr_canvas, margin + x * scale + dx,
                                     margin + y * scale + dy, lv_color_black(),
                                     LV_OPA_COVER);
                }
            }
        }
    }
}

void music_view_apply_catalog(music_view_t *view,
                              const music_service_catalog_snapshot_t *catalog)
{
    if (view == NULL || catalog == NULL || !view->catalog_visible)
    {
        return;
    }
    if (catalog->loading ||
        (view->catalog_snapshot_applied &&
         catalog->generation == view->catalog_generation))
    {
        return;
    }
    view->catalog_load_pending = false;
    if (!catalog->valid)
    {
        return;
    }
    const bool first_page = catalog->offset == 0U;
    const bool same_source =
        strcmp(view->catalog_source_id, catalog->source_id) == 0;
    if (first_page || !same_source)
    {
        music_view_reset_catalog(view);
        snprintf(view->catalog_source_id, sizeof(view->catalog_source_id), "%s",
                 catalog->source_id);
    }
    else if (catalog->offset != view->catalog_next_offset)
    {
        return;
    }

    const size_t incoming =
        catalog->track_count < MUSIC_SERVICE_CATALOG_PAGE_SIZE
            ? catalog->track_count
            : MUSIC_SERVICE_CATALOG_PAGE_SIZE;
    size_t removed = 0U;
    lv_coord_t old_scroll_y = lv_obj_get_scroll_y(view->catalog_list);
    if (view->catalog_count + incoming > MUSIC_VIEW_CATALOG_CACHE_CAPACITY)
    {
        removed = view->catalog_count < MUSIC_SERVICE_CATALOG_PAGE_SIZE
                      ? view->catalog_count
                      : MUSIC_SERVICE_CATALOG_PAGE_SIZE;
        memmove(view->catalog_tracks, &view->catalog_tracks[removed],
                (view->catalog_count - removed) *
                    sizeof(*view->catalog_tracks));
        view->catalog_count -= removed;
    }
    if (incoming > 0U)
    {
        memcpy(&view->catalog_tracks[view->catalog_count], catalog->tracks,
               incoming * sizeof(*view->catalog_tracks));
        view->catalog_count += incoming;
    }
    view->catalog_total = catalog->total;
    view->catalog_next_offset = catalog->offset + (uint32_t)catalog->track_count;
    view->catalog_generation = catalog->generation;
    view->catalog_snapshot_applied = true;
    view->catalog_load_pending = false;
    music_view_render_catalog(view);

    if (removed > 0U)
    {
        const lv_coord_t removed_height =
            (lv_coord_t)removed *
            (MUSIC_VIEW_CATALOG_ROW_HEIGHT + MUSIC_VIEW_CATALOG_ROW_GAP);
        lv_obj_update_layout(view->catalog_list);
        lv_obj_scroll_to_y(view->catalog_list,
                           old_scroll_y > removed_height
                               ? old_scroll_y - removed_height
                               : 0,
                           LV_ANIM_OFF);
    }
}
