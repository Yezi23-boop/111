#include <stdint.h>
#include "esp_err.h"
#include "network_manager.h"
#include "danger_detection_service.h"
#include "system_time.h"

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
    t->hour = 12; t->min = 0; t->sec = 0;
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

void memory_watch_service_cancel_waiting(void) {}
void memory_watch_service_cancel_clarification(void) {}
void memory_watch_service_begin_recording(void) {}
void memory_watch_service_send_recording(void) {}
void memory_watch_service_cancel_recording(void) {}
void *memory_watch_service_get_snapshot(void) { return NULL; }
int memory_watch_service_check_health(void) { return 0; }

void mini_game_2048_reset(void) {}
int mini_game_2048_is_game_over(void) { return 0; }
void mini_game_2048_move(int dir) {}
int mini_game_2048_get_score(void) { return 0; }
int mini_game_2048_get_cell(int r, int c) { return 0; }
void mini_game_2048_init(void) {}

void board_button_clear_events(void) {}
int board_button_consume_event(int btn, int evt) { return 0; }

uint8_t _binary_font_puhui_common_20_4_bin_start[1] = {0};
uint8_t _binary_font_puhui_common_20_4_bin_end[1] = {0};

int official_chat_service_get_message_count(void) { return 0; }
void *official_chat_service_get_message(int index) { return NULL; }
const char *official_chat_service_get_last_user_text(void) { return ""; }
const char *official_chat_service_get_last_assistant_text(void) { return ""; }
int official_chat_service_is_shutdown_pending(void) { return 0; }
void official_chat_service_leave_foreground(void) {}
void official_chat_service_enter_foreground(void) {}
int official_chat_service_get_state(void) { return 0; }

void network_service_request_portal(void) {}
int network_service_get_state(void) { return 0; }

void app_alert_manager_set_traffic_audio_overlay_enabled(int enabled) {}

void background_service_manager_set_danger_detection_enabled(int enabled) {}
void *background_service_manager_get_snapshot(void) { return NULL; }
void background_service_manager_init(void) {}

void *danger_detection_service_get_snapshot(void) { return NULL; }


#include "gui_guider.h"
lv_ui guider_ui;
