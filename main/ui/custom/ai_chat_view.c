#include "ai_chat_view.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "gui_guider.h"
#include "official_chat_service.h"
#include "ui_font_assets.h"

static const char *TAG = "ai_chat_view";

struct ai_chat_view {
    lv_obj_t *screen;
    lv_obj_t *badge;
    lv_obj_t *badge_label;
    lv_obj_t *status_label;
    lv_obj_t *hint_label;
    lv_obj_t *chat_card;
    lv_obj_t *chat_title_label;
    lv_obj_t *chat_scroll;
    lv_obj_t *chat_content;
    lv_obj_t *primary_btn;
    lv_obj_t *primary_label;
    lv_obj_t *secondary_btn;
    lv_obj_t *secondary_label;
    const ai_chat_view_config_t *config;
};

typedef enum {
    AI_CHAT_BUBBLE_SYSTEM = 0,
    AI_CHAT_BUBBLE_USER,
    AI_CHAT_BUBBLE_ASSISTANT,
} ai_chat_bubble_kind_t;

static void ai_chat_view_button_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    ai_chat_view_t *view = lv_event_get_user_data(e);
    if (view == NULL || view->config == NULL) {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    if (target == view->primary_btn && view->config->primary_action_cb != NULL) {
        view->config->primary_action_cb(view->config->user_data);
    } else if (target == view->secondary_btn &&
               view->config->secondary_action_cb != NULL) {
        view->config->secondary_action_cb(view->config->user_data);
    }
}

static void ai_chat_view_style_card(lv_obj_t *card) {
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161b22), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x273449), 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
}

static lv_obj_t *ai_chat_view_create_row(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 0, 0);
    lv_obj_set_style_pad_row(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    return row;
}

static lv_obj_t *ai_chat_view_create_spacer(lv_obj_t *parent) {
    lv_obj_t *spacer = lv_obj_create(parent);
    lv_obj_set_size(spacer, 0, 0);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_shadow_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);
    return spacer;
}

static lv_obj_t *ai_chat_view_create_bubble(lv_obj_t *parent,
                                            ai_chat_bubble_kind_t kind,
                                            const char *role_text,
                                            const char *message_text) {
    lv_obj_t *bubble = lv_obj_create(parent);
    lv_obj_set_width(bubble, 286);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(bubble, 22, 0);
    lv_obj_set_style_pad_all(bubble, 12, 0);
    lv_obj_set_style_pad_row(bubble, 4, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_shadow_width(bubble, 0, 0);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bubble, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    switch (kind) {
        case AI_CHAT_BUBBLE_USER:
            lv_obj_set_style_bg_color(bubble, lv_color_hex(0x2563eb), 0);
            lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
            break;
        case AI_CHAT_BUBBLE_ASSISTANT:
            lv_obj_set_style_bg_color(bubble, lv_color_hex(0x1e293b), 0);
            lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bubble, 1, 0);
            lv_obj_set_style_border_color(bubble, lv_color_hex(0x334155), 0);
            break;
        case AI_CHAT_BUBBLE_SYSTEM:
        default:
            lv_obj_set_style_bg_color(bubble, lv_color_hex(0x0f172a), 0);
            lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(bubble, 1, 0);
            lv_obj_set_style_border_color(bubble, lv_color_hex(0x475569), 0);
            break;
    }

    lv_obj_t *role_label = lv_label_create(bubble);
    lv_label_set_text(role_label, role_text);
    lv_obj_set_style_text_color(role_label, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_font(role_label, ui_font_assets_meta(), 0);

    lv_obj_t *text_label = lv_label_create(bubble);
    lv_label_set_text(text_label, message_text);
    lv_obj_set_width(text_label, 262);
    lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(text_label, lv_color_hex(0xe5e7eb), 0);
    lv_obj_set_style_text_font(text_label, ui_font_assets_body(), 0);
    lv_obj_set_style_text_line_space(text_label, 4, 0);
    if (kind == AI_CHAT_BUBBLE_USER) {
        lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_RIGHT, 0);
    }

    return bubble;
}

static lv_obj_t *ai_chat_view_append_bubble(ai_chat_view_t *view,
                                            ai_chat_bubble_kind_t kind,
                                            const char *role_text,
                                            const char *message_text) {
    lv_obj_t *row = ai_chat_view_create_row(view->chat_content);
    if (kind == AI_CHAT_BUBBLE_USER) {
        ai_chat_view_create_spacer(row);
        ai_chat_view_create_bubble(row, kind, role_text, message_text);
    } else if (kind == AI_CHAT_BUBBLE_ASSISTANT) {
        ai_chat_view_create_bubble(row, kind, role_text, message_text);
        ai_chat_view_create_spacer(row);
    } else {
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        ai_chat_view_create_bubble(row, kind, role_text, message_text);
    }
    return row;
}

static bool ai_chat_view_append_text(ai_chat_view_t *view,
                                     official_chat_service_message_role_t role,
                                     const char *text,
                                     lv_obj_t **last_row_out) {
    if (text == NULL || text[0] == '\0') {
        return false;
    }

    lv_obj_t *row = NULL;
    switch (role) {
        case OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER:
            row = ai_chat_view_append_bubble(view, AI_CHAT_BUBBLE_USER, "你",
                                             text);
            break;
        case OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT:
            row = ai_chat_view_append_bubble(view, AI_CHAT_BUBBLE_ASSISTANT,
                                             "小智", text);
            break;
        default:
            row = ai_chat_view_append_bubble(view, AI_CHAT_BUBBLE_SYSTEM, "提示",
                                             text);
            break;
    }

    if (last_row_out != NULL) {
        *last_row_out = row;
    }
    return true;
}

static lv_obj_t *ai_chat_view_append_placeholder(ai_chat_view_t *view,
                                                 const char *text) {
    lv_obj_t *row = ai_chat_view_create_row(view->chat_content);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 300);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(label, ui_font_assets_body(), 0);
    lv_obj_set_style_text_line_space(label, 4, 0);
    return row;
}

const char *ai_chat_view_network_badge_text(network_service_state_t state) {
    switch (state) {
        case NETWORK_SERVICE_STATE_SERVICE_READY:
            return "已联网";
        case NETWORK_SERVICE_STATE_WIFI_READY:
            return "服务检测中";
        case NETWORK_SERVICE_STATE_CONNECTING:
            return "联网中";
        case NETWORK_SERVICE_STATE_PORTAL_REQUIRED:
        case NETWORK_SERVICE_STATE_OFFLINE:
            return "未联网";
        case NETWORK_SERVICE_STATE_ERROR:
        default:
            return "网络异常";
    }
}

lv_color_t ai_chat_view_network_badge_color(network_service_state_t state) {
    switch (state) {
        case NETWORK_SERVICE_STATE_SERVICE_READY:
            return lv_color_hex(0x22c55e);
        case NETWORK_SERVICE_STATE_WIFI_READY:
        case NETWORK_SERVICE_STATE_CONNECTING:
            return lv_color_hex(0xf59e0b);
        case NETWORK_SERVICE_STATE_PORTAL_REQUIRED:
        case NETWORK_SERVICE_STATE_OFFLINE:
        case NETWORK_SERVICE_STATE_ERROR:
        default:
            return lv_color_hex(0xef4444);
    }
}

const char *ai_chat_view_network_title(network_service_state_t state) {
    switch (state) {
        case NETWORK_SERVICE_STATE_PORTAL_REQUIRED:
        case NETWORK_SERVICE_STATE_OFFLINE:
            return "未联网";
        case NETWORK_SERVICE_STATE_CONNECTING:
            return "正在联网";
        case NETWORK_SERVICE_STATE_WIFI_READY:
            return "正在准备";
        case NETWORK_SERVICE_STATE_SERVICE_READY:
            return "AI 服务已就绪";
        case NETWORK_SERVICE_STATE_ERROR:
        default:
            return "网络异常";
    }
}

const char *ai_chat_view_network_hint(network_service_state_t state) {
    switch (state) {
        case NETWORK_SERVICE_STATE_PORTAL_REQUIRED:
        case NETWORK_SERVICE_STATE_OFFLINE:
            return "手表其余功能可继续使用，点下方按钮可进入本地 AP 配网。";
        case NETWORK_SERVICE_STATE_CONNECTING:
            return "后台正在尝试使用已保存的 Wi-Fi 凭据联网。";
        case NETWORK_SERVICE_STATE_WIFI_READY:
            return "已经拿到 STA 网络，正在检测 AI 服务可用性。";
        case NETWORK_SERVICE_STATE_SERVICE_READY:
            return "网络已经准备完成，语音助手即将进入前台。";
        case NETWORK_SERVICE_STATE_ERROR:
        default:
            return "后台联网遇到异常，可点击下方按钮重新进入本地配网。";
    }
}

const char *ai_chat_view_service_title(official_chat_service_state_t state) {
    switch (state) {
        case OFFICIAL_CHAT_SERVICE_STATE_WAITING_NETWORK:
            return "等待网络";
        case OFFICIAL_CHAT_SERVICE_STATE_STARTING:
        case OFFICIAL_CHAT_SERVICE_STATE_ACTIVATING:
        case OFFICIAL_CHAT_SERVICE_STATE_CONNECTING:
            return "正在启动";
        case OFFICIAL_CHAT_SERVICE_STATE_IDLE:
            return "待唤醒";
        case OFFICIAL_CHAT_SERVICE_STATE_LISTENING:
            return "聆听中";
        case OFFICIAL_CHAT_SERVICE_STATE_SPEAKING:
            return "回答中";
        case OFFICIAL_CHAT_SERVICE_STATE_ERROR:
            return "AI 异常";
        case OFFICIAL_CHAT_SERVICE_STATE_STOPPED:
        default:
            return "正在准备";
    }
}

const char *ai_chat_view_service_hint(official_chat_service_state_t state) {
    switch (state) {
        case OFFICIAL_CHAT_SERVICE_STATE_IDLE:
            return "小智已经进入待唤醒状态，现在可以直接说“你好小智”。";
        case OFFICIAL_CHAT_SERVICE_STATE_LISTENING:
            return "正在认真听你说话，继续问就好。";
        case OFFICIAL_CHAT_SERVICE_STATE_SPEAKING:
            return "小智正在回答，稍等这一句播报完成。";
        case OFFICIAL_CHAT_SERVICE_STATE_ERROR:
            return "语音助手启动失败，可尝试重新配网后再进入本页。";
        case OFFICIAL_CHAT_SERVICE_STATE_WAITING_NETWORK:
            return "正在等待网络服务真正可用。";
        case OFFICIAL_CHAT_SERVICE_STATE_STARTING:
        case OFFICIAL_CHAT_SERVICE_STATE_ACTIVATING:
        case OFFICIAL_CHAT_SERVICE_STATE_CONNECTING:
        case OFFICIAL_CHAT_SERVICE_STATE_STOPPED:
        default:
            return "正在拉起语音助手，请稍候。";
    }
}

static void ai_chat_view_apply_footer_layout(ai_chat_view_t *view) {
    if (view == NULL || view->primary_btn == NULL || view->secondary_btn == NULL) {
        return;
    }

    if (lv_obj_has_flag(view->secondary_btn, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_set_flex_grow(view->primary_btn, 1);
    } else {
        lv_obj_set_flex_grow(view->primary_btn, 1);
        lv_obj_set_flex_grow(view->secondary_btn, 1);
    }
}

ai_chat_view_t *ai_chat_view_create(const ai_chat_view_config_t *config) {
    esp_err_t font_assets_ret = ui_font_assets_init();
    if (font_assets_ret != ESP_OK) {
        ESP_LOGW(TAG, "ui_font_assets_init failed: %s",
                 esp_err_to_name(font_assets_ret));
    }

    ai_chat_view_t *view = calloc(1, sizeof(*view));
    if (view == NULL) {
        ESP_LOGE(TAG, "calloc ai_chat_view_t failed");
        return NULL;
    }

    static const ai_chat_view_config_t kDefaultConfig = {
        .title_text = "小智",
        .badge_text = "准备中",
        .badge_color_hex = 0x64748b,
        .status_text = "正在准备",
        .hint_text = "",
        .primary_action_text = "进入配网",
        .secondary_action_text = NULL,
        .primary_action_cb = NULL,
        .secondary_action_cb = NULL,
        .user_data = NULL,
    };
    view->config = config != NULL ? config : &kDefaultConfig;

    view->screen = lv_obj_create(NULL);
    lv_obj_set_size(view->screen, 410, 502);
    lv_obj_set_style_bg_color(view->screen, lv_color_hex(0x0b1020), 0);
    lv_obj_set_style_bg_opa(view->screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(view->screen, lv_color_hex(0x121a2f), 0);
    lv_obj_set_style_bg_grad_dir(view->screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_scrollbar_mode(view->screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title_label = lv_label_create(view->screen);
    lv_label_set_text(title_label,
                      view->config->title_text != NULL ? view->config->title_text
                                                       : "小智");
    lv_obj_set_pos(title_label, 24, 20);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title_label, ui_font_assets_title(), 0);

    view->badge = lv_obj_create(view->screen);
    lv_obj_set_pos(view->badge, 282, 18);
    lv_obj_set_size(view->badge, 108, 34);
    ai_chat_view_style_card(view->badge);
    lv_obj_set_style_radius(view->badge, 19, 0);
    lv_obj_set_style_bg_color(view->badge,
                              lv_color_hex(view->config->badge_color_hex), 0);
    lv_obj_set_style_border_width(view->badge, 0, 0);

    view->badge_label = lv_label_create(view->badge);
    lv_label_set_text(view->badge_label,
                      view->config->badge_text != NULL ? view->config->badge_text
                                                       : "准备中");
    lv_obj_set_style_text_color(view->badge_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(view->badge_label, ui_font_assets_meta(), 0);
    lv_obj_center(view->badge_label);

    lv_obj_t *hero_card = lv_obj_create(view->screen);
    lv_obj_set_pos(hero_card, 20, 62);
    lv_obj_set_size(hero_card, 370, 92);
    ai_chat_view_style_card(hero_card);

    lv_obj_t *icon_ring = lv_obj_create(hero_card);
    lv_obj_set_size(icon_ring, 72, 72);
    lv_obj_set_pos(icon_ring, 12, 10);
    lv_obj_set_style_radius(icon_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(icon_ring, lv_color_hex(0x1d4ed8), 0);
    lv_obj_set_style_bg_opa(icon_ring, LV_OPA_20, 0);
    lv_obj_set_style_border_width(icon_ring, 1, 0);
    lv_obj_set_style_border_color(icon_ring, lv_color_hex(0x60a5fa), 0);
    lv_obj_set_scrollbar_mode(icon_ring, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *icon = lv_image_create(hero_card);
    lv_image_set_src(icon, &_ai_RGB565A8_70x70);
    lv_obj_set_pos(icon, 13, 11);

    view->status_label = lv_label_create(hero_card);
    lv_label_set_text(view->status_label,
                      view->config->status_text != NULL ? view->config->status_text
                                                        : "正在准备");
    lv_obj_set_width(view->status_label, 256);
    lv_obj_set_pos(view->status_label, 96, 18);
    lv_obj_set_style_text_align(view->status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(view->status_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(view->status_label, ui_font_assets_title(), 0);

    view->hint_label = lv_label_create(hero_card);
    lv_label_set_text(view->hint_label,
                      view->config->hint_text != NULL ? view->config->hint_text
                                                      : "");
    lv_obj_set_width(view->hint_label, 252);
    lv_obj_set_pos(view->hint_label, 96, 46);
    lv_label_set_long_mode(view->hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(view->hint_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(view->hint_label, lv_color_hex(0xcbd5e1), 0);
    lv_obj_set_style_text_font(view->hint_label, ui_font_assets_body(), 0);
    lv_obj_set_style_text_line_space(view->hint_label, 4, 0);

    view->chat_card = lv_obj_create(view->screen);
    lv_obj_set_pos(view->chat_card, 20, 162);
    lv_obj_set_size(view->chat_card, 370, 274);
    ai_chat_view_style_card(view->chat_card);

    view->chat_title_label = lv_label_create(view->chat_card);
    lv_label_set_text(view->chat_title_label, "聊天区");
    lv_obj_set_pos(view->chat_title_label, 14, 8);
    lv_obj_set_style_text_color(view->chat_title_label, lv_color_hex(0x8b9bb4),
                                0);
    lv_obj_set_style_text_font(view->chat_title_label, ui_font_assets_meta(), 0);

    view->chat_scroll = lv_obj_create(view->chat_card);
    lv_obj_set_pos(view->chat_scroll, 10, 28);
    lv_obj_set_size(view->chat_scroll, 350, 236);
    lv_obj_set_scroll_dir(view->chat_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(view->chat_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_radius(view->chat_scroll, 18, 0);
    lv_obj_set_style_bg_color(view->chat_scroll, lv_color_hex(0x0b1220), 0);
    lv_obj_set_style_bg_opa(view->chat_scroll, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->chat_scroll, 1, 0);
    lv_obj_set_style_border_color(view->chat_scroll, lv_color_hex(0x243244), 0);
    lv_obj_set_style_pad_all(view->chat_scroll, 0, 0);
    lv_obj_set_style_pad_top(view->chat_scroll, 8, 0);
    lv_obj_set_style_pad_bottom(view->chat_scroll, 8, 0);
    lv_obj_set_style_pad_left(view->chat_scroll, 10, 0);
    lv_obj_set_style_pad_right(view->chat_scroll, 8, 0);

    view->chat_content = lv_obj_create(view->chat_scroll);
    lv_obj_set_width(view->chat_content, lv_pct(100));
    lv_obj_set_height(view->chat_content, LV_SIZE_CONTENT);
    lv_obj_clear_flag(view->chat_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view->chat_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->chat_content, 0, 0);
    lv_obj_set_style_shadow_width(view->chat_content, 0, 0);
    lv_obj_set_style_pad_all(view->chat_content, 0, 0);
    lv_obj_set_style_pad_row(view->chat_content, 6, 0);
    lv_obj_set_flex_flow(view->chat_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view->chat_content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *footer = lv_obj_create(view->screen);
    lv_obj_set_pos(footer, 20, 444);
    lv_obj_set_size(footer, 370, 42);
    ai_chat_view_style_card(footer);
    lv_obj_set_style_radius(footer, 16, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_set_style_pad_left(footer, 8, 0);
    lv_obj_set_style_pad_right(footer, 8, 0);
    lv_obj_set_style_pad_top(footer, 0, 0);
    lv_obj_set_style_pad_bottom(footer, 0, 0);
    lv_obj_set_style_pad_column(footer, 6, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    view->primary_btn = lv_btn_create(footer);
    lv_obj_set_height(view->primary_btn, 34);
    lv_obj_set_flex_grow(view->primary_btn, 1);
    lv_obj_set_style_radius(view->primary_btn, 14, 0);
    lv_obj_set_style_bg_color(view->primary_btn, lv_color_hex(0x2563eb), 0);
    lv_obj_set_style_bg_opa(view->primary_btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(view->primary_btn, ai_chat_view_button_event_cb,
                        LV_EVENT_CLICKED, view);

    view->primary_label = lv_label_create(view->primary_btn);
    lv_label_set_text(view->primary_label,
                      view->config->primary_action_text != NULL
                          ? view->config->primary_action_text
                          : "进入配网");
    lv_obj_set_style_text_color(view->primary_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(view->primary_label, ui_font_assets_body(), 0);
    lv_obj_center(view->primary_label);

    view->secondary_btn = lv_btn_create(footer);
    lv_obj_set_height(view->secondary_btn, 34);
    lv_obj_set_flex_grow(view->secondary_btn, 1);
    lv_obj_set_style_radius(view->secondary_btn, 14, 0);
    lv_obj_set_style_bg_color(view->secondary_btn, lv_color_hex(0x30363d), 0);
    lv_obj_set_style_bg_opa(view->secondary_btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(view->secondary_btn, ai_chat_view_button_event_cb,
                        LV_EVENT_CLICKED, view);

    view->secondary_label = lv_label_create(view->secondary_btn);
    lv_label_set_text(view->secondary_label,
                      view->config->secondary_action_text != NULL
                          ? view->config->secondary_action_text
                          : "");
    lv_obj_set_style_text_color(view->secondary_label, lv_color_hex(0xffffff),
                                0);
    lv_obj_set_style_text_font(view->secondary_label, ui_font_assets_body(), 0);
    lv_obj_center(view->secondary_label);

    if (view->config->secondary_action_cb == NULL ||
        view->config->secondary_action_text == NULL ||
        view->config->secondary_action_text[0] == '\0') {
        lv_obj_add_flag(view->secondary_btn, LV_OBJ_FLAG_HIDDEN);
    }

    ai_chat_view_apply_footer_layout(view);
    ai_chat_view_reload_messages(view,
                                 "还没有真实消息，先说一句试试。");
    return view;
}

void ai_chat_view_destroy(ai_chat_view_t *view) {
    if (view == NULL) {
        return;
    }

    if (view->screen != NULL && lv_obj_is_valid(view->screen)) {
        lv_obj_del(view->screen);
    }
    free(view);
}

lv_obj_t *ai_chat_view_get_screen(const ai_chat_view_t *view) {
    if (view == NULL) {
        return NULL;
    }
    return view->screen;
}

void ai_chat_view_set_top_status(ai_chat_view_t *view, const char *badge_text,
                                 lv_color_t badge_color,
                                 const char *status_text,
                                 const char *hint_text) {
    if (view == NULL) {
        return;
    }

    if (view->badge_label != NULL) {
        lv_label_set_text(view->badge_label,
                          badge_text != NULL ? badge_text : "");
    }
    if (view->badge != NULL) {
        lv_obj_set_style_bg_color(view->badge, badge_color, 0);
    }
    if (view->status_label != NULL) {
        lv_label_set_text(view->status_label,
                          status_text != NULL ? status_text : "");
    }
    if (view->hint_label != NULL) {
        lv_label_set_text(view->hint_label, hint_text != NULL ? hint_text : "");
    }
}

void ai_chat_view_set_primary_action(ai_chat_view_t *view,
                                     const char *action_text, bool enabled) {
    if (view == NULL || view->primary_btn == NULL || view->primary_label == NULL) {
        return;
    }

    lv_label_set_text(view->primary_label,
                      action_text != NULL ? action_text : "");
    if (enabled) {
        lv_obj_clear_state(view->primary_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(view->primary_btn, LV_STATE_DISABLED);
    }
}

void ai_chat_view_set_secondary_action(ai_chat_view_t *view,
                                       const char *action_text, bool visible,
                                       bool enabled) {
    if (view == NULL || view->secondary_btn == NULL ||
        view->secondary_label == NULL) {
        return;
    }

    lv_label_set_text(view->secondary_label,
                      action_text != NULL ? action_text : "");
    if (visible) {
        lv_obj_clear_flag(view->secondary_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(view->secondary_btn, LV_OBJ_FLAG_HIDDEN);
    }

    if (enabled) {
        lv_obj_clear_state(view->secondary_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(view->secondary_btn, LV_STATE_DISABLED);
    }

    ai_chat_view_apply_footer_layout(view);
}

void ai_chat_view_reload_messages(ai_chat_view_t *view,
                                  const char *empty_placeholder_text) {
    if (view == NULL || view->chat_content == NULL) {
        return;
    }

    lv_obj_clean(view->chat_content);

    lv_obj_t *last_row = NULL;
    bool appended = false;

    size_t message_count = official_chat_service_get_message_count();
    for (size_t i = 0; i < message_count; ++i) {
        official_chat_service_message_t message = {0};
        if (official_chat_service_get_message(i, &message) != ESP_OK) {
            ESP_LOGW(TAG, "get_message failed at index=%u", (unsigned)i);
            continue;
        }

        if (message.text[0] == '\0') {
            continue;
        }

        appended |= ai_chat_view_append_text(view, message.role, message.text,
                                             &last_row);
    }

    if (!appended) {
        char last_user_text[192] = {0};
        char last_assistant_text[256] = {0};
        bool has_user = official_chat_service_get_last_user_text(
                            last_user_text, sizeof(last_user_text)) == ESP_OK &&
                        last_user_text[0] != '\0';
        bool has_assistant = official_chat_service_get_last_assistant_text(
                                 last_assistant_text,
                                 sizeof(last_assistant_text)) == ESP_OK &&
                             last_assistant_text[0] != '\0';

        if (has_user) {
            appended |= ai_chat_view_append_text(
                view, OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER, last_user_text,
                &last_row);
        }
        if (has_assistant) {
            appended |= ai_chat_view_append_text(
                view, OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT,
                last_assistant_text, &last_row);
        }
    }

    if (!appended) {
        last_row = ai_chat_view_append_placeholder(
            view, empty_placeholder_text != NULL ? empty_placeholder_text
                                                 : "还没有真实消息，先说一句试试。");
    }

    lv_obj_update_layout(view->chat_content);
    if (last_row != NULL && lv_obj_is_valid(last_row)) {
        lv_obj_scroll_to_view(last_row, LV_ANIM_OFF);
    }
}

void ai_chat_view_scroll_to_bottom(ai_chat_view_t *view) {
    if (view == NULL || view->chat_content == NULL) {
        return;
    }

    lv_obj_t *last_child = lv_obj_get_child(view->chat_content, -1);
    if (last_child != NULL) {
        lv_obj_scroll_to_view(last_child, LV_ANIM_OFF);
    }
}
