#include "memory_watch_view.h"

#include <stdlib.h>

#include "gui_guider.h"
#include "ui_chinese_fonts.h"

struct memory_watch_view
{
    lv_obj_t *screen;
    lv_obj_t *top_status_label;
    lv_obj_t *state_label;
    lv_obj_t *user_bubble;
    lv_obj_t *user_label;
    lv_obj_t *reply_bubble;
    lv_obj_t *reply_label;
    lv_obj_t *cancel_btn;
    lv_obj_t *cancel_label;
    lv_obj_t *voice_btn;
    lv_obj_t *voice_label;
    memory_watch_view_action_cb_t back_cb;
    memory_watch_view_action_cb_t press_start_cb;
    memory_watch_view_action_cb_t release_send_cb;
    memory_watch_view_action_cb_t slide_cancel_cb;
    memory_watch_view_action_cb_t cancel_waiting_cb;
    memory_watch_view_action_cb_t cancel_clarification_cb;
    void *user_data;
    bool pressing;
    bool press_inside;
    bool cancel_is_clarification;
};

static const lv_coord_t kScreenWidth = 410;
static const lv_coord_t kScreenHeight = 502;
static const lv_coord_t kHeaderHeight = 76;
static const lv_coord_t kBubbleWidth = 294;
static const lv_coord_t kVoiceButtonWidth = 244;
static const lv_coord_t kVoiceButtonHeight = 64;

static void memory_watch_view_call(memory_watch_view_t *view,
                                   memory_watch_view_action_cb_t cb)
{
    if (view == NULL || cb == NULL)
    {
        return;
    }
    cb(view->user_data);
}

static void memory_watch_view_set_label(lv_obj_t *label, const char *text)
{
    if (label == NULL)
    {
        return;
    }
    lv_label_set_text(label, text != NULL ? text : "");
}

static bool memory_watch_view_point_inside(const lv_area_t *area,
                                           const lv_point_t *point)
{
    if (area == NULL || point == NULL)
    {
        return false;
    }

    return point->x >= area->x1 && point->x <= area->x2 &&
           point->y >= area->y1 && point->y <= area->y2;
}

static bool memory_watch_view_voice_touch_inside(memory_watch_view_t *view)
{
    if (view == NULL || view->voice_btn == NULL)
    {
        return false;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL)
    {
        return view->press_inside;
    }

    lv_point_t point = {0};
    lv_area_t area = {0};
    lv_indev_get_point(indev, &point);
    lv_obj_get_coords(view->voice_btn, &area);
    return memory_watch_view_point_inside(&area, &point);
}

static void memory_watch_view_set_voice_label(memory_watch_view_t *view,
                                              const char *text)
{
    if (view == NULL || view->voice_label == NULL)
    {
        return;
    }
    lv_label_set_text(view->voice_label, text != NULL ? text : "");
    lv_obj_center(view->voice_label);
}

static void memory_watch_view_back_event(lv_event_t *e)
{
    memory_watch_view_t *view =
        (memory_watch_view_t *)lv_event_get_user_data(e);
    memory_watch_view_call(view, view != NULL ? view->back_cb : NULL);
}

static void memory_watch_view_cancel_event(lv_event_t *e)
{
    memory_watch_view_t *view =
        (memory_watch_view_t *)lv_event_get_user_data(e);
    if (view == NULL)
    {
        return;
    }

    memory_watch_view_call(
        view, view->cancel_is_clarification ? view->cancel_clarification_cb
                                            : view->cancel_waiting_cb);
}

static void memory_watch_view_voice_event(lv_event_t *e)
{
    memory_watch_view_t *view =
        (memory_watch_view_t *)lv_event_get_user_data(e);
    if (view == NULL)
    {
        return;
    }

    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED)
    {
        view->pressing = true;
        view->press_inside = true;
        memory_watch_view_set_voice_label(view, "松开发送");
        memory_watch_view_call(view, view->press_start_cb);
        return;
    }

    if (code == LV_EVENT_PRESSING && view->pressing)
    {
        view->press_inside = memory_watch_view_voice_touch_inside(view);
        memory_watch_view_set_voice_label(
            view, view->press_inside ? "松开发送" : "松手取消");
        return;
    }

    if (code == LV_EVENT_RELEASED && view->pressing)
    {
        const bool send = memory_watch_view_voice_touch_inside(view);
        view->pressing = false;
        view->press_inside = send;
        memory_watch_view_call(view, send ? view->release_send_cb
                                          : view->slide_cancel_cb);
        return;
    }

    if (code == LV_EVENT_PRESS_LOST && view->pressing)
    {
        view->pressing = false;
        view->press_inside = false;
        memory_watch_view_set_voice_label(view, "按住说话");
        memory_watch_view_call(view, view->slide_cancel_cb);
    }
}

static lv_obj_t *memory_watch_view_create_text_button(lv_obj_t *parent,
                                                      const char *text,
                                                      lv_coord_t width,
                                                      lv_coord_t height)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label,
                               &lv_font_montserrat_lxgw_tghz_level1_3500_16_4,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *memory_watch_view_create_bubble(lv_obj_t *parent,
                                                 lv_coord_t x,
                                                 lv_coord_t y,
                                                 lv_color_t color,
                                                 lv_obj_t **out_label)
{
    lv_obj_t *bubble = lv_obj_create(parent);
    lv_obj_set_size(bubble, kBubbleWidth, 118);
    lv_obj_set_pos(bubble, x, y);
    lv_obj_set_style_radius(bubble, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bubble, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bubble, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bubble, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(bubble);
    lv_obj_set_width(label, kBubbleWidth - 28);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label,
                               &lv_font_montserrat_lxgw_tghz_level1_3500_16_4,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
    if (out_label != NULL)
    {
        *out_label = label;
    }
    return bubble;
}

memory_watch_view_t *memory_watch_view_create(
    const memory_watch_view_config_t *config)
{
    memory_watch_view_t *view =
        (memory_watch_view_t *)calloc(1, sizeof(memory_watch_view_t));
    if (view == NULL)
    {
        return NULL;
    }

    if (config != NULL)
    {
        view->back_cb = config->back_cb;
        view->press_start_cb = config->press_start_cb;
        view->release_send_cb = config->release_send_cb;
        view->slide_cancel_cb = config->slide_cancel_cb;
        view->cancel_waiting_cb = config->cancel_waiting_cb;
        view->cancel_clarification_cb = config->cancel_clarification_cb;
        view->user_data = config->user_data;
    }

    view->screen = lv_obj_create(NULL);
    lv_obj_set_size(view->screen, kScreenWidth, kScreenHeight);
    lv_obj_set_style_bg_color(view->screen, lv_color_hex(0x0b1220),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(view->screen, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(view->screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back_btn =
        memory_watch_view_create_text_button(view->screen, "<", 54, 46);
    lv_obj_set_pos(back_btn, 24, 22);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x1f2937),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(back_btn, memory_watch_view_back_event,
                        LV_EVENT_CLICKED, view);

    lv_obj_t *title = lv_label_create(view->screen);
    lv_label_set_text(title, "Hermes");
    lv_obj_set_pos(title, 88, 20);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &lv_font_montserratMedium_27,
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    view->top_status_label = lv_label_create(view->screen);
    lv_obj_set_pos(view->top_status_label, 90, 48);
    lv_obj_set_size(view->top_status_label, 260, 24);
    lv_label_set_long_mode(view->top_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(view->top_status_label, lv_color_hex(0x5eead4),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(view->top_status_label,
                               &lv_font_montserrat_lxgw_tghz_level1_3500_16_4,
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    view->state_label = lv_label_create(view->screen);
    lv_obj_set_pos(view->state_label, 40, kHeaderHeight + 8);
    lv_obj_set_size(view->state_label, 330, 28);
    lv_label_set_long_mode(view->state_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(view->state_label, lv_color_hex(0xfacc15),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(view->state_label,
                               &lv_font_montserrat_lxgw_tghz_level1_3500_16_4,
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    view->user_bubble = memory_watch_view_create_bubble(
        view->screen, 76, 122, lv_color_hex(0x2563eb), &view->user_label);
    view->reply_bubble = memory_watch_view_create_bubble(
        view->screen, 40, 252, lv_color_hex(0x1f2937), &view->reply_label);

    view->cancel_btn =
        memory_watch_view_create_text_button(view->screen, "取消", 88, 40);
    view->cancel_label = lv_obj_get_child(view->cancel_btn, 0);
    lv_obj_set_pos(view->cancel_btn, 282, 386);
    lv_obj_set_style_bg_color(view->cancel_btn, lv_color_hex(0x475569),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(view->cancel_btn, memory_watch_view_cancel_event,
                        LV_EVENT_CLICKED, view);

    view->voice_btn = memory_watch_view_create_text_button(
        view->screen, "按住说话", kVoiceButtonWidth, kVoiceButtonHeight);
    lv_obj_set_pos(view->voice_btn, 78, 416);
    lv_obj_set_style_bg_color(view->voice_btn, lv_color_hex(0x0d9488),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    view->voice_label = lv_obj_get_child(view->voice_btn, 0);
    lv_obj_add_event_cb(view->voice_btn, memory_watch_view_voice_event,
                        LV_EVENT_ALL, view);

    const memory_watch_view_model_t initial_model = {
        .top_status_text = "Hermes 待检测",
        .state_text = "待命",
        .user_text = "",
        .reply_text = "按住按钮说话",
        .voice_button_text = "按住说话",
        .voice_button_enabled = false,
        .cancel_visible = false,
        .cancel_is_clarification = false,
    };
    memory_watch_view_apply_model(view, &initial_model);
    return view;
}

void memory_watch_view_destroy(memory_watch_view_t *view)
{
    if (view == NULL)
    {
        return;
    }

    if (view->screen != NULL)
    {
        lv_obj_delete(view->screen);
    }
    free(view);
}

lv_obj_t *memory_watch_view_get_screen(const memory_watch_view_t *view)
{
    if (view == NULL)
    {
        return NULL;
    }
    return view->screen;
}

void memory_watch_view_apply_model(memory_watch_view_t *view,
                                   const memory_watch_view_model_t *model)
{
    if (view == NULL || model == NULL)
    {
        return;
    }

    view->cancel_is_clarification = model->cancel_is_clarification;
    memory_watch_view_set_label(view->top_status_label,
                                model->top_status_text);
    memory_watch_view_set_label(view->state_label, model->state_text);
    memory_watch_view_set_label(view->user_label, model->user_text);
    memory_watch_view_set_label(view->reply_label, model->reply_text);

    if (model->cancel_visible)
    {
        lv_obj_remove_flag(view->cancel_btn, LV_OBJ_FLAG_HIDDEN);
        memory_watch_view_set_label(
            view->cancel_label,
            model->cancel_is_clarification ? "取消追问" : "取消");
    }
    else
    {
        lv_obj_add_flag(view->cancel_btn, LV_OBJ_FLAG_HIDDEN);
    }

    if (view->voice_btn != NULL)
    {
        if (model->voice_button_enabled)
        {
            lv_obj_clear_state(view->voice_btn, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_add_state(view->voice_btn, LV_STATE_DISABLED);
        }
    }

    if (!view->pressing)
    {
        memory_watch_view_set_voice_label(view, model->voice_button_text);
    }
}
