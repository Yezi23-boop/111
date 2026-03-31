#include "ai_ui_controller.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_err.h"
#include "gui_guider.h"
#include "network_service.h"
#include "official_chat_service.h"
#include "ui_font_assets.h"

static const char *TAG = "ai_ui";

static lv_ui *s_ui = NULL;
static lv_obj_t *s_ai_screen = NULL;
static lv_obj_t *s_title_label = NULL;
static lv_obj_t *s_state_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_ip_label = NULL;
static lv_obj_t *s_action_btn = NULL;
static lv_obj_t *s_action_label = NULL;
static lv_obj_t *s_back_btn = NULL;
static lv_timer_t *s_status_timer = NULL;

static void ai_ui_refresh_status(void);

static void ai_ui_back_event_handler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_ui == NULL || s_ui->screen_main == NULL) {
        ESP_LOGW(TAG, "screen_main not ready when leaving ai page");
        return;
    }

    lv_screen_load_anim(s_ui->screen_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,
                        false);
}

static void ai_ui_portal_event_handler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    network_service_request_portal();
    ai_ui_refresh_status();
}

static const char *ai_ui_state_title(network_service_state_t state) {
    switch (state) {
        case NETWORK_SERVICE_STATE_PORTAL_REQUIRED:
        case NETWORK_SERVICE_STATE_OFFLINE:
            return "未联网";
        case NETWORK_SERVICE_STATE_CONNECTING:
            return "正在联网";
        case NETWORK_SERVICE_STATE_WIFI_READY:
            return "网络已连接";
        case NETWORK_SERVICE_STATE_SERVICE_READY:
            return "AI 服务已就绪";
        case NETWORK_SERVICE_STATE_ERROR:
        default:
            return "网络异常";
    }
}

static const char *ai_ui_service_title(official_chat_service_state_t state) {
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

static const char *ai_ui_state_hint(network_service_state_t state) {
    switch (state) {
        case NETWORK_SERVICE_STATE_PORTAL_REQUIRED:
        case NETWORK_SERVICE_STATE_OFFLINE:
            return "手表其余功能可继续使用。\n点击下方按钮进入本地 AP 配网。";
        case NETWORK_SERVICE_STATE_CONNECTING:
            return "后台正在尝试使用已保存的 Wi-Fi 凭据联网。\n如需改网，也可以直接进入配网。";
        case NETWORK_SERVICE_STATE_WIFI_READY:
            return "已经拿到 STA 网络，正在检测 AI 服务可用性。";
        case NETWORK_SERVICE_STATE_SERVICE_READY:
            return "网络和 AI 服务都已准备完成。\n下一步可在此页面接入前台对话与待唤醒。";
        case NETWORK_SERVICE_STATE_ERROR:
        default:
            return "后台联网遇到异常。\n可点击下方按钮重新进入本地配网。";
    }
}

static const char *ai_ui_service_hint(official_chat_service_state_t state) {
    switch (state) {
        case OFFICIAL_CHAT_SERVICE_STATE_IDLE:
            return "小智已经进入待唤醒状态。\n现在可以直接说“你好小智”。";
        case OFFICIAL_CHAT_SERVICE_STATE_LISTENING:
            return "正在监听你的问题，请继续说话。";
        case OFFICIAL_CHAT_SERVICE_STATE_SPEAKING:
            return "小智正在回答，你可以先听完当前回复。";
        case OFFICIAL_CHAT_SERVICE_STATE_ERROR:
            return "语音助手启动失败。\n可先尝试重新配网，再重新进入本页面。";
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

static const char *ai_ui_action_text(network_service_state_t state) {
    switch (state) {
        case NETWORK_SERVICE_STATE_SERVICE_READY:
            return "重新配网";
        case NETWORK_SERVICE_STATE_CONNECTING:
        case NETWORK_SERVICE_STATE_WIFI_READY:
            return "网络设置";
        case NETWORK_SERVICE_STATE_PORTAL_REQUIRED:
        case NETWORK_SERVICE_STATE_OFFLINE:
        case NETWORK_SERVICE_STATE_ERROR:
        default:
            return "进入配网";
    }
}

static void ai_ui_refresh_status(void) {
    if (s_ai_screen == NULL || s_state_label == NULL || s_hint_label == NULL ||
        s_ip_label == NULL || s_action_label == NULL) {
        return;
    }

    network_service_state_t state = network_service_get_state();
    if (state == NETWORK_SERVICE_STATE_SERVICE_READY) {
        official_chat_service_enter_foreground();
        official_chat_service_state_t service_state =
            official_chat_service_get_state();
        lv_label_set_text(s_state_label, ai_ui_service_title(service_state));
        lv_label_set_text(s_hint_label, ai_ui_service_hint(service_state));
    } else {
        lv_label_set_text(s_state_label, ai_ui_state_title(state));
        lv_label_set_text(s_hint_label, ai_ui_state_hint(state));
    }
    lv_label_set_text(s_action_label, ai_ui_action_text(state));

    char ip[16] = {0};
    if (network_service_get_ip(ip, sizeof(ip)) == ESP_OK) {
        char ip_text[48] = {0};
        snprintf(ip_text, sizeof(ip_text), "当前 Wi-Fi IP: %s", ip);
        lv_label_set_text(s_ip_label, ip_text);
    } else if (state == NETWORK_SERVICE_STATE_PORTAL_REQUIRED ||
               state == NETWORK_SERVICE_STATE_OFFLINE ||
               state == NETWORK_SERVICE_STATE_ERROR) {
        lv_label_set_text(s_ip_label,
                          "配网地址: http://192.168.100.1/");
    } else {
        lv_label_set_text(s_ip_label, "当前 Wi-Fi IP: 等待中");
    }
}

static void ai_ui_status_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_ai_screen == NULL || lv_screen_active() != s_ai_screen) {
        return;
    }

    ai_ui_refresh_status();
}

static void ai_ui_ensure_screen_created(void) {
    if (s_ai_screen != NULL) {
        return;
    }

    esp_err_t font_assets_ret = ui_font_assets_init();
    if (font_assets_ret != ESP_OK) {
        ESP_LOGW(TAG, "ui_font_assets_init failed: %s",
                 esp_err_to_name(font_assets_ret));
    }

    s_ai_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ai_screen, lv_color_hex(0x0d1117), 0);
    lv_obj_set_style_bg_opa(s_ai_screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(s_ai_screen, LV_SCROLLBAR_MODE_OFF);

    s_title_label = lv_label_create(s_ai_screen);
    lv_label_set_text(s_title_label, "小智");
    lv_obj_set_pos(s_title_label, 36, 42);
    lv_obj_set_style_text_color(s_title_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_title_label, ui_font_assets_title(), 0);

    s_state_label = lv_label_create(s_ai_screen);
    lv_label_set_text(s_state_label, "未联网");
    lv_obj_set_pos(s_state_label, 36, 112);
    lv_obj_set_width(s_state_label, 320);
    lv_label_set_long_mode(s_state_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0x5eead4), 0);
    lv_obj_set_style_text_font(s_state_label, ui_font_assets_title(), 0);

    s_hint_label = lv_label_create(s_ai_screen);
    lv_label_set_text(s_hint_label, "");
    lv_obj_set_pos(s_hint_label, 36, 172);
    lv_obj_set_width(s_hint_label, 338);
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0xd1d5db), 0);
    lv_obj_set_style_text_font(s_hint_label, ui_font_assets_body(), 0);
    lv_obj_set_style_text_line_space(s_hint_label, 6, 0);

    s_ip_label = lv_label_create(s_ai_screen);
    lv_label_set_text(s_ip_label, "");
    lv_obj_set_pos(s_ip_label, 36, 280);
    lv_obj_set_width(s_ip_label, 338);
    lv_label_set_long_mode(s_ip_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_ip_label, lv_color_hex(0x93c5fd), 0);
    lv_obj_set_style_text_font(s_ip_label, ui_font_assets_meta(), 0);

    s_action_btn = lv_btn_create(s_ai_screen);
    lv_obj_set_pos(s_action_btn, 36, 350);
    lv_obj_set_size(s_action_btn, 338, 64);
    lv_obj_set_style_radius(s_action_btn, 18, 0);
    lv_obj_set_style_bg_color(s_action_btn, lv_color_hex(0x2563eb), 0);
    lv_obj_add_event_cb(s_action_btn, ai_ui_portal_event_handler,
                        LV_EVENT_CLICKED, NULL);

    s_action_label = lv_label_create(s_action_btn);
    lv_label_set_text(s_action_label, "进入配网");
    lv_obj_set_style_text_color(s_action_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_action_label, ui_font_assets_body(), 0);
    lv_obj_center(s_action_label);

    s_back_btn = lv_btn_create(s_ai_screen);
    lv_obj_set_pos(s_back_btn, 36, 430);
    lv_obj_set_size(s_back_btn, 150, 52);
    lv_obj_set_style_radius(s_back_btn, 18, 0);
    lv_obj_set_style_bg_color(s_back_btn, lv_color_hex(0x374151), 0);
    lv_obj_add_event_cb(s_back_btn, ai_ui_back_event_handler, LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *back_label = lv_label_create(s_back_btn);
    lv_label_set_text(back_label, "返回主页");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(back_label, ui_font_assets_body(), 0);
    lv_obj_center(back_label);

    s_status_timer = lv_timer_create(ai_ui_status_timer_cb, 1000, NULL);
    ai_ui_refresh_status();
}

void ai_ui_controller_init(lv_ui *ui) {
    s_ui = ui;
}

void ai_ui_open(void) {
    ai_ui_ensure_screen_created();
    ai_ui_refresh_status();
    lv_screen_load_anim(s_ai_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}
