#include <stdint.h>
#include <string.h>
#include "esp_err.h"
#include "network_manager.h"
#include "danger_detection_service.h"
#include "system_time.h"
#include "services/network_service.h"
#include "features/weather/time_weather.h"

esp_err_t weather_service_get_info(weather_info_t *out)
{
    if (out == NULL) return ESP_FAIL;
    out->temp = 24;
    strcpy(out->weather_text, "多云");
    strcpy(out->icon_path, "A:/weather/duoyun.png");
    out->is_valid = true;
    return ESP_OK;
}

const char *esp_err_to_name(esp_err_t code) { return "OK"; }
bool network_manager_is_ble_enabled(void) { return true; }
bool network_manager_is_ble_active(void) { return false; }
esp_err_t network_manager_set_ble_enabled(bool enabled) { return ESP_OK; }
esp_err_t network_manager_get_status(network_manager_status_t *status) {
    status->wifi_connected = true;
    status->ble_enabled = true;
    status->ble_active = false;
    return ESP_OK;
}
danger_detection_state_t danger_detection_service_get_state(void) { return DANGER_DETECTION_STATE_IDLE; }
void danger_detection_service_set_state(danger_detection_state_t state) {}
esp_err_t system_time_get_local_time(system_time_local_t *t) {
    t->year = 2026; t->month = 6; t->day = 18;
    t->hour = 10; t->min = 9; t->sec = 0;
    return ESP_OK;
}
void vTaskDelay(uint32_t xTicksToDelay) {}

#include "lvgl.h"
extern const lv_font_t lv_font_montserrat_14;
lv_font_t *cbin_font_create(uint8_t *data) { return (lv_font_t*)&lv_font_montserrat_14; }
void cbin_font_delete(lv_font_t *font) {}

#include "cJSON.h"
cJSON *cJSON_Parse(const char *value) { return NULL; }
void cJSON_Delete(cJSON *c) {}
cJSON *cJSON_GetObjectItem(const cJSON * const object, const char * const string) { return NULL; }
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string) { return NULL; }
int cJSON_GetArraySize(const cJSON *array) { return 0; }
cJSON *cJSON_GetArrayItem(const cJSON *array, int index) { return NULL; }

esp_err_t network_manager_use_latest_wifi(void) { return ESP_OK; }
esp_err_t network_manager_disconnect(void) { return ESP_OK; }
esp_err_t network_manager_start_ble_provisioning(void) { return ESP_OK; }
esp_err_t network_manager_start_softap_provisioning(void) { return ESP_OK; }

#include "esp_partition.h"
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) { return NULL; }
esp_err_t esp_partition_mmap(const esp_partition_t *partition, uint32_t offset, uint32_t size, int memory_type, const void** out_ptr, esp_partition_mmap_handle_t* out_handle) { return ESP_FAIL; }
void esp_partition_munmap(esp_partition_mmap_handle_t handle) {}
cJSON *cJSON_ParseWithLength(const char *value, int buffer_length) { return NULL; }
int cJSON_IsString(const cJSON * const item) { return 0; }

esp_err_t co5300_panel_set_brightness_percent(int percent) { return ESP_OK; }
network_manager_state_t network_manager_get_state_cached(void) { return NETWORK_MANAGER_STATE_IDLE; }

#include "esp_timer.h"
int64_t esp_timer_get_time(void) { return 0; }

#include "services/memory_watch_service.h"

static memory_watch_service_snapshot_t s_memory_watch_snapshot = {
    .state = MEMORY_WATCH_SERVICE_STATE_READY,
    .network_ready = true,
    .endpoint_configured = true,
    .hermes_online = true,
    .request_active = false,
    .clarification_active = false,
    .last_error = ESP_OK,
    .request_id = "preview-request",
    .clarification_id = "",
    .asr_text = "帮我记住下午三点去取快递",
    .reply_text = "已记录: 下午三点取快递, 需要时我会提醒你",
};

esp_err_t memory_watch_service_cancel_waiting(void) {
    s_memory_watch_snapshot.state = MEMORY_WATCH_SERVICE_STATE_CANCELED;
    s_memory_watch_snapshot.request_active = false;
    strncpy(s_memory_watch_snapshot.reply_text, "已取消等待 Hermes 回复",
            sizeof(s_memory_watch_snapshot.reply_text) - 1);
    return ESP_OK;
}

esp_err_t memory_watch_service_cancel_clarification(void) {
    s_memory_watch_snapshot.state = MEMORY_WATCH_SERVICE_STATE_CANCELED;
    s_memory_watch_snapshot.clarification_active = false;
    strncpy(s_memory_watch_snapshot.reply_text, "已取消 Hermes 追问",
            sizeof(s_memory_watch_snapshot.reply_text) - 1);
    return ESP_OK;
}

esp_err_t memory_watch_service_begin_recording(void) {
    s_memory_watch_snapshot.state = MEMORY_WATCH_SERVICE_STATE_RECORDING;
    s_memory_watch_snapshot.request_active = true;
    strncpy(s_memory_watch_snapshot.asr_text, "正在聆听...",
            sizeof(s_memory_watch_snapshot.asr_text) - 1);
    strncpy(s_memory_watch_snapshot.reply_text, "松手后发送给 Hermes",
            sizeof(s_memory_watch_snapshot.reply_text) - 1);
    return ESP_OK;
}

esp_err_t memory_watch_service_send_recording(void) {
    s_memory_watch_snapshot.state = MEMORY_WATCH_SERVICE_STATE_DONE;
    s_memory_watch_snapshot.request_active = false;
    strncpy(s_memory_watch_snapshot.asr_text, "帮我记住下午三点去取快递",
            sizeof(s_memory_watch_snapshot.asr_text) - 1);
    strncpy(s_memory_watch_snapshot.reply_text,
            "已记录: 下午三点取快递, 需要时我会提醒你",
            sizeof(s_memory_watch_snapshot.reply_text) - 1);
    return ESP_OK;
}

esp_err_t memory_watch_service_cancel_recording(void) {
    s_memory_watch_snapshot.state = MEMORY_WATCH_SERVICE_STATE_CANCELED;
    s_memory_watch_snapshot.request_active = false;
    strncpy(s_memory_watch_snapshot.reply_text, "已取消本次录音",
            sizeof(s_memory_watch_snapshot.reply_text) - 1);
    return ESP_OK;
}

esp_err_t memory_watch_service_get_snapshot(
    memory_watch_service_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_snapshot = s_memory_watch_snapshot;
    return ESP_OK;
}

esp_err_t memory_watch_service_check_health(void) {
    s_memory_watch_snapshot.network_ready = true;
    s_memory_watch_snapshot.endpoint_configured = true;
    s_memory_watch_snapshot.hermes_online = true;
    return ESP_OK;
}

#if 0
void mini_game_2048_reset(void) {}
int mini_game_2048_is_game_over(void) { return 0; }
void mini_game_2048_move(int dir) {}
int mini_game_2048_get_score(void) { return 0; }
int mini_game_2048_get_cell(int r, int c) { return 0; }
void mini_game_2048_init(void) {}
#endif

#include "app/board_button.h"
void board_button_clear_events(void) {}
board_button_event_t board_button_consume_event(void) { return BOARD_BUTTON_EVENT_NONE; }

uint8_t _binary_font_puhui_common_20_4_bin_start[1] = {0};
uint8_t _binary_font_puhui_common_20_4_bin_end[1] = {0};

/* Mock ui_font_assets.h to return correct LXGW and Montserrat fonts */
#include "ui_font_assets.h"
extern const lv_font_t lv_font_montserrat_lxgw_tghz_level1_3500_22_4;
extern const lv_font_t lv_font_montserrat_lxgw_tghz_level1_3500_16_4;
extern const lv_font_t lv_font_montserratMedium_16;

esp_err_t ui_font_assets_init(void) { return ESP_OK; }
bool ui_font_assets_ready(void) { return true; }
const lv_font_t *ui_font_assets_title(void) { return &lv_font_montserrat_lxgw_tghz_level1_3500_22_4; }
const lv_font_t *ui_font_assets_body(void) { return &lv_font_montserrat_lxgw_tghz_level1_3500_16_4; }
const lv_font_t *ui_font_assets_meta(void) { return &lv_font_montserrat_lxgw_tghz_level1_3500_16_4; }
const lv_font_t *ui_font_assets_icon(void) { return &lv_font_montserratMedium_16; }

/* Mock official_chat_service.h with 8 conversation messages */
#include "services/official_chat_service.h"

static official_chat_service_message_t s_mock_messages[] = {
    { OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER, "你好, 小智!" },
    { OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT, "你好! 我是小智, 你的个人 AI 语音助手. 很高兴为你服务." },
    { OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER, "今天天气怎么样?" },
    { OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT, "今天北京天气晴朗, 气温 18 到 28 度, 非常适合户外出行. 建议注意防晒." },
    { OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER, "北京有什么好玩的地方推荐吗?" },
    { OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT, "北京有许多景点! 如果您喜欢历史, 可以去故宫和长城; 如果偏好现代, 可以逛逛 798 艺术区. 您偏好哪种?" },
    { OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER, "我更偏好历史古迹." },
    { OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT, "那推荐您先去故宫博物院, 建议提前预约门票. 之后可以沿着景山公园俯瞰紫禁城全景, 非常壮观!" }
};

size_t official_chat_service_get_message_count(void) {
    return sizeof(s_mock_messages) / sizeof(s_mock_messages[0]);
}

esp_err_t official_chat_service_get_message(size_t index, official_chat_service_message_t *out_message) {
    if (index >= sizeof(s_mock_messages) / sizeof(s_mock_messages[0])) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_message != NULL) {
        *out_message = s_mock_messages[index];
    }
    return ESP_OK;
}

esp_err_t official_chat_service_get_last_user_text(char *buffer, size_t size) {
    if (buffer != NULL && size > 0) {
        strncpy(buffer, "我更偏好历史古迹.", size - 1);
        buffer[size - 1] = '\0';
    }
    return ESP_OK;
}

esp_err_t official_chat_service_get_last_assistant_text(char *buffer, size_t size) {
    if (buffer != NULL && size > 0) {
        strncpy(buffer, "那推荐您先去故宫博物院, 建议提前预约门票. 之后可以沿着景山公园俯瞰紫禁城全景, 非常壮观!", size - 1);
        buffer[size - 1] = '\0';
    }
    return ESP_OK;
}

bool official_chat_service_is_shutdown_pending(void) { return false; }
void official_chat_service_leave_foreground(void) {}
void official_chat_service_enter_foreground(void) {}
official_chat_service_state_t official_chat_service_get_state(void) { return OFFICIAL_CHAT_SERVICE_STATE_IDLE; }

void network_service_request_portal(void) {}
network_service_state_t network_service_get_state(void) { return NETWORK_SERVICE_STATE_OFFLINE; }

void app_alert_manager_set_traffic_audio_overlay_enabled(int enabled) {}

void background_service_manager_set_danger_detection_enabled(int enabled) {}
void *background_service_manager_get_snapshot(void) { return NULL; }
void background_service_manager_init(void) {}

void *danger_detection_service_get_snapshot(void) { return NULL; }

#include "services/power_service.h"
esp_err_t power_service_get_snapshot(board_power_state_t *out_state) {
    if (out_state != NULL) {
        out_state->available = true;
        out_state->battery_data_valid = true;
        out_state->snapshot_stale = false;
        out_state->charging = false;
        out_state->discharging = true;
        out_state->external_power_present = false;
        out_state->battery_present = true;
        out_state->battery_mv = 3800;
        out_state->system_mv = 3800;
        out_state->battery_percent = 80;
    }
    return ESP_OK;
}
const board_power_state_t *power_service_get_state(void) {
    static board_power_state_t s_state = {
        .available = true,
        .battery_data_valid = true,
        .battery_percent = 80,
    };
    return &s_state;
}

#include "gui_guider.h"
lv_ui guider_ui;

esp_err_t memory_watch_service_get_inbox_meta(memory_watch_inbox_meta_t *out_meta)
{
    if (out_meta == NULL) return ESP_ERR_INVALID_ARG;
    out_meta->generation = 1;
    out_meta->unread_count = 2;
    out_meta->item_count = 2;
    out_meta->sync_state = 0; // MEMORY_WATCH_INBOX_SYNC_STATE_IDLE
    out_meta->last_success_ms = 0;
    return ESP_OK;
}

esp_err_t memory_watch_service_copy_inbox_summaries(
    memory_watch_inbox_summary_t *out_summaries,
    size_t capacity,
    size_t *out_count)
{
    if (out_summaries == NULL || out_count == NULL) return ESP_ERR_INVALID_ARG;
    size_t cnt = 0;
    if (capacity > cnt) {
        strncpy(out_summaries[cnt].notification_id, "preview-006", sizeof(out_summaries[cnt].notification_id) - 1);
        strncpy(out_summaries[cnt].title, "Hermes 新提示", sizeof(out_summaries[cnt].title) - 1);
        strncpy(out_summaries[cnt].preview, "已整理下午三点取快递提醒", sizeof(out_summaries[cnt].preview) - 1);
        out_summaries[cnt].read = false;
        cnt++;
    }
    if (capacity > cnt) {
        strncpy(out_summaries[cnt].notification_id, "preview-005", sizeof(out_summaries[cnt].notification_id) - 1);
        strncpy(out_summaries[cnt].title, "电池日志提醒", sizeof(out_summaries[cnt].title) - 1);
        strncpy(out_summaries[cnt].preview, "建议确认待机电流和屏幕亮度曲线", sizeof(out_summaries[cnt].preview) - 1);
        out_summaries[cnt].read = false;
        cnt++;
    }
    *out_count = cnt;
    return ESP_OK;
}
