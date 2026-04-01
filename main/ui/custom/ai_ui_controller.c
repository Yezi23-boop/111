#include "ai_ui_controller.h"

#include "ai_chat_view.h"
#include "esp_log.h"
#include "gui_guider.h"
#include "network_service.h"
#include "official_chat_service.h"

static const char *TAG = "ai_ui";

static lv_ui *s_ui = NULL;
static ai_chat_view_t *s_view = NULL;
static lv_timer_t *s_status_timer = NULL;
static bool s_foreground_requested = false;

static void ai_ui_refresh_status(void);

static void ai_ui_back_event(void *user_data) {
    (void)user_data;

    if (s_ui == NULL || s_ui->screen_main == NULL) {
        ESP_LOGW(TAG, "screen_main not ready when leaving ai page");
        return;
    }

    official_chat_service_leave_foreground();
    s_foreground_requested = false;
    lv_screen_load_anim(s_ui->screen_main, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,
                        false);
}

static void ai_ui_portal_event(void *user_data) {
    (void)user_data;

    network_service_request_portal();
    ai_ui_refresh_status();
}

static void ai_ui_refresh_status(void) {
    if (s_view == NULL) {
        return;
    }

    network_service_state_t network_state = network_service_get_state();
    official_chat_service_state_t service_state =
        official_chat_service_get_state();

    if (network_state == NETWORK_SERVICE_STATE_SERVICE_READY) {
        if (!s_foreground_requested) {
            official_chat_service_enter_foreground();
            s_foreground_requested = true;
            ESP_LOGI(TAG, "official_chat foreground requested");
        }

        ai_chat_view_set_top_status(
            s_view, ai_chat_view_network_badge_text(network_state),
            ai_chat_view_network_badge_color(network_state),
            ai_chat_view_service_title(service_state),
            ai_chat_view_service_hint(service_state));
        ai_chat_view_set_primary_action(
            s_view,
            network_state == NETWORK_SERVICE_STATE_SERVICE_READY ? "网络设置"
                                                                 : "进入配网",
            true);
    } else {
        s_foreground_requested = false;
        ai_chat_view_set_top_status(
            s_view, ai_chat_view_network_badge_text(network_state),
            ai_chat_view_network_badge_color(network_state),
            ai_chat_view_network_title(network_state),
            ai_chat_view_network_hint(network_state));
        ai_chat_view_set_primary_action(s_view, "进入配网", true);
    }

    ai_chat_view_set_secondary_action(s_view, "返回主页", true, true);
    ai_chat_view_reload_messages(s_view, "还没有真实消息，先说一句试试。");
}

static void ai_ui_status_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_view == NULL || lv_screen_active() != ai_chat_view_get_screen(s_view)) {
        return;
    }

    ai_ui_refresh_status();
}

void ai_ui_controller_init(lv_ui *ui) {
    s_ui = ui;
}

static void ai_ui_ensure_screen_created(void) {
    if (s_view != NULL) {
        return;
    }

    static const ai_chat_view_config_t kConfig = {
        .title_text = "小智",
        .badge_text = "准备中",
        .badge_color_hex = 0x64748b,
        .status_text = "正在准备",
        .hint_text = "",
        .primary_action_text = "进入配网",
        .secondary_action_text = "返回主页",
        .primary_action_cb = ai_ui_portal_event,
        .secondary_action_cb = ai_ui_back_event,
        .user_data = NULL,
    };

    s_view = ai_chat_view_create(&kConfig);
    if (s_view == NULL) {
        ESP_LOGE(TAG, "ai_chat_view_create failed");
        return;
    }

    if (s_status_timer == NULL) {
        s_status_timer = lv_timer_create(ai_ui_status_timer_cb, 1000, NULL);
    }

    ai_ui_refresh_status();
}

void ai_ui_open(void) {
    ai_ui_ensure_screen_created();
    if (s_view == NULL) {
        return;
    }

    ai_ui_refresh_status();
    lv_screen_load_anim(ai_chat_view_get_screen(s_view),
                        LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}
