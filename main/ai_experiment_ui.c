#include "ai_experiment_ui.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui_guider.h"
#include "lv_port.h"
#include "lvgl.h"
#include "network_service.h"
#include "official_chat_service.h"

static const char *TAG = "AI_EXPERIMENT_UI";

static TaskHandle_t s_ui_task_handle = NULL;
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_state_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_ip_label = NULL;
static lv_obj_t *s_action_btn = NULL;
static lv_obj_t *s_action_label = NULL;
static lv_timer_t *s_status_timer = NULL;
static bool s_foreground_requested = false;

static void ai_experiment_ui_refresh_status(void);

static const char *network_state_title(network_service_state_t state) {
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

static const char *network_state_hint(network_service_state_t state) {
    switch (state) {
        case NETWORK_SERVICE_STATE_PORTAL_REQUIRED:
        case NETWORK_SERVICE_STATE_OFFLINE:
            return "尚未联网。\n点击下方按钮进入本地 AP 配网。";
        case NETWORK_SERVICE_STATE_CONNECTING:
            return "后台正在使用已保存的 Wi-Fi 凭据联网。";
        case NETWORK_SERVICE_STATE_WIFI_READY:
            return "已经拿到 STA 网络，正在检测 AI 服务。";
        case NETWORK_SERVICE_STATE_SERVICE_READY:
            return "网络和 AI 服务都已准备完成。";
        case NETWORK_SERVICE_STATE_ERROR:
        default:
            return "网络初始化失败，可重新进入配网。";
    }
}

static const char *service_state_title(official_chat_service_state_t state) {
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

static const char *service_state_hint(official_chat_service_state_t state) {
    switch (state) {
        case OFFICIAL_CHAT_SERVICE_STATE_IDLE:
            return "小智已经进入待唤醒。\n现在可以直接说“你好小智”。";
        case OFFICIAL_CHAT_SERVICE_STATE_LISTENING:
            return "正在监听你的问题，请继续说话。";
        case OFFICIAL_CHAT_SERVICE_STATE_SPEAKING:
            return "小智正在回答，请先听完当前回复。";
        case OFFICIAL_CHAT_SERVICE_STATE_ERROR:
            return "语音助手启动失败，可稍后重新进入配网。";
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

static const char *action_button_text(network_service_state_t state) {
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

static void portal_button_event_handler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    ESP_LOGI(TAG, "request provisioning portal");
    network_service_request_portal();
    ai_experiment_ui_refresh_status();
}

static void status_timer_cb(lv_timer_t *timer) {
    (void)timer;
    ai_experiment_ui_refresh_status();
}

static void ai_experiment_ui_refresh_status(void) {
    if (s_state_label == NULL || s_hint_label == NULL || s_ip_label == NULL ||
        s_action_label == NULL) {
        return;
    }

    network_service_state_t network_state = network_service_get_state();
    if (network_state == NETWORK_SERVICE_STATE_SERVICE_READY) {
        if (!s_foreground_requested) {
            official_chat_service_enter_foreground();
            s_foreground_requested = true;
            ESP_LOGI(TAG, "official_chat foreground requested");
        }

        official_chat_service_state_t service_state =
            official_chat_service_get_state();
        lv_label_set_text(s_state_label, service_state_title(service_state));
        lv_label_set_text(s_hint_label, service_state_hint(service_state));
    } else {
        s_foreground_requested = false;
        lv_label_set_text(s_state_label, network_state_title(network_state));
        lv_label_set_text(s_hint_label, network_state_hint(network_state));
    }

    lv_label_set_text(s_action_label, action_button_text(network_state));

    char ip[16] = {0};
    if (network_service_get_ip(ip, sizeof(ip)) == ESP_OK) {
        char ip_text[48] = {0};
        snprintf(ip_text, sizeof(ip_text), "当前 Wi-Fi IP: %s", ip);
        lv_label_set_text(s_ip_label, ip_text);
    } else if (network_state == NETWORK_SERVICE_STATE_PORTAL_REQUIRED ||
               network_state == NETWORK_SERVICE_STATE_OFFLINE ||
               network_state == NETWORK_SERVICE_STATE_ERROR) {
        lv_label_set_text(s_ip_label, "配网地址: http://192.168.100.1/");
    } else {
        lv_label_set_text(s_ip_label, "当前 Wi-Fi IP: 等待中");
    }
}

static void create_experiment_screen(void) {
    if (s_screen != NULL) {
        return;
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0b1220), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *title_label = lv_label_create(s_screen);
    lv_label_set_text(title_label, "AI 对话实验");
    lv_obj_set_pos(title_label, 28, 30);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title_label,
                               &lv_font_SourceHanSerifSC_Regular_22, 0);

    s_state_label = lv_label_create(s_screen);
    lv_label_set_text(s_state_label, "正在准备");
    lv_obj_set_pos(s_state_label, 28, 92);
    lv_obj_set_width(s_state_label, 350);
    lv_label_set_long_mode(s_state_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0x5eead4), 0);
    lv_obj_set_style_text_font(s_state_label,
                               &lv_font_SourceHanSerifSC_Regular_22, 0);

    s_hint_label = lv_label_create(s_screen);
    lv_label_set_text(s_hint_label, "");
    lv_obj_set_pos(s_hint_label, 28, 150);
    lv_obj_set_width(s_hint_label, 350);
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0xd1d5db), 0);
    lv_obj_set_style_text_font(s_hint_label,
                               &lv_font_SourceHanSerifSC_Regular_22, 0);
    lv_obj_set_style_text_line_space(s_hint_label, 6, 0);

    s_ip_label = lv_label_create(s_screen);
    lv_label_set_text(s_ip_label, "当前 Wi-Fi IP: 等待中");
    lv_obj_set_pos(s_ip_label, 28, 286);
    lv_obj_set_width(s_ip_label, 350);
    lv_label_set_long_mode(s_ip_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_ip_label, lv_color_hex(0x93c5fd), 0);
    lv_obj_set_style_text_font(s_ip_label, &lv_font_montserratMedium_16, 0);

    s_action_btn = lv_btn_create(s_screen);
    lv_obj_set_pos(s_action_btn, 28, 360);
    lv_obj_set_size(s_action_btn, 354, 72);
    lv_obj_set_style_radius(s_action_btn, 18, 0);
    lv_obj_set_style_bg_color(s_action_btn, lv_color_hex(0x2563eb), 0);
    lv_obj_add_event_cb(s_action_btn, portal_button_event_handler,
                        LV_EVENT_CLICKED, NULL);

    s_action_label = lv_label_create(s_action_btn);
    lv_label_set_text(s_action_label, "进入配网");
    lv_obj_set_style_text_color(s_action_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_action_label,
                               &lv_font_SourceHanSerifSC_Regular_22, 0);
    lv_obj_center(s_action_label);

    lv_obj_t *footer_label = lv_label_create(s_screen);
    lv_label_set_text(footer_label, "页面自动待唤醒，无需再点开始。");
    lv_obj_set_pos(footer_label, 28, 450);
    lv_obj_set_width(footer_label, 350);
    lv_label_set_long_mode(footer_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(footer_label, lv_color_hex(0x9ca3af), 0);
    lv_obj_set_style_text_font(footer_label,
                               &lv_font_SourceHanSerifSC_Regular_22, 0);

    s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    ai_experiment_ui_refresh_status();
    lv_screen_load(s_screen);
}

static void ai_experiment_ui_task(void *arg) {
    (void)arg;

    ESP_LOGI(TAG, "starting standalone AI experiment UI");
    lv_port_init_small();
    create_experiment_screen();

    while (1) {
        uint32_t next_call = lv_timer_handler();
        uint32_t delay_ms = 5;
        if (next_call == 0U) {
            delay_ms = 1U;
        } else if (next_call < 5U) {
            delay_ms = next_call;
        } else if (next_call > 100U) {
            delay_ms = 100U;
        } else {
            delay_ms = next_call;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t ai_experiment_ui_start(void) {
    if (s_ui_task_handle != NULL) {
        return ESP_OK;
    }

    BaseType_t result = xTaskCreatePinnedToCore(
        ai_experiment_ui_task, "ai_experiment_ui", 1024 * 10, NULL, 6,
        &s_ui_task_handle, 1);
    if (result != pdPASS) {
        s_ui_task_handle = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}
