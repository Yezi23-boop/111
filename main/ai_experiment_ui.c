#include "ai_experiment_ui.h"

#include "ai_chat_view.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_service.h"
#include "official_chat_service.h"
#include "lv_port.h"
#include "lvgl.h"
#include "wifi_provision.h"

static const char *TAG = "AI_EXPERIMENT_UI";

static TaskHandle_t s_ui_task_handle = NULL;
static ai_chat_view_t *s_view = NULL;
static lv_timer_t *s_status_timer = NULL;
static bool s_foreground_requested = false;

static void ai_experiment_ui_refresh_status(void);

static void portal_button_event(void *user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "request provisioning portal");
    network_service_request_portal();
    ai_experiment_ui_refresh_status();
}

static void ai_experiment_ui_refresh_status(void) {
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

    ai_chat_view_set_secondary_action(s_view, NULL, false, false);
    ai_chat_view_reload_messages(s_view, "还没有真实消息，先说一句试试。");
}

static void status_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_view == NULL || lv_screen_active() != ai_chat_view_get_screen(s_view)) {
        return;
    }
    ai_experiment_ui_refresh_status();
}

static void create_experiment_screen(void) {
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
        .secondary_action_text = NULL,
        .primary_action_cb = portal_button_event,
        .secondary_action_cb = NULL,
        .user_data = NULL,
    };

    s_view = ai_chat_view_create(&kConfig);
    if (s_view == NULL) {
        ESP_LOGE(TAG, "ai_chat_view_create failed");
        return;
    }

    if (s_status_timer == NULL) {
        s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    }

    ai_experiment_ui_refresh_status();
    lv_screen_load(ai_chat_view_get_screen(s_view));
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
