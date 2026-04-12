#include "services/power_service.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "POWER_SERVICE";
static const TickType_t k_failure_log_throttle_ticks = pdMS_TO_TICKS(5000);

static TaskHandle_t s_task_handle = NULL;
static board_power_state_t s_state_buffers[2] = {
    {.battery_percent = UINT8_MAX},
    {.battery_percent = UINT8_MAX},
};
static power_state_changed_cb_t s_callback = NULL;
static bool s_initialized = false;
static bool s_started = false;
static uint32_t s_failure_count = 0;
static TickType_t s_last_failure_log_tick = 0;
static uint8_t s_active_state_index = 0;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static board_power_state_t power_service_make_unsampled_state(void)
{
    board_power_state_t state = {0};
    state.battery_percent = UINT8_MAX;
    return state;
}

static bool power_service_cached_state_has_history(
    const board_power_state_t *cached_state)
{
    return cached_state != NULL && cached_state->available;
}

static void power_service_copy_cached_state(void)
{
    const board_power_state_t *cached_state = board_power_get_cached_state();
    board_power_state_t snapshot = power_service_make_unsampled_state();

    if (cached_state != NULL) {
        snapshot = *cached_state;
    }

    taskENTER_CRITICAL(&s_lock);
    s_state_buffers[0] = snapshot;
    s_state_buffers[1] = snapshot;
    s_active_state_index = 0;
    taskEXIT_CRITICAL(&s_lock);
}

static bool power_service_state_equal(const board_power_state_t *lhs,
                                      const board_power_state_t *rhs)
{
    return lhs->available == rhs->available &&
           lhs->battery_data_valid == rhs->battery_data_valid &&
           lhs->snapshot_stale == rhs->snapshot_stale &&
           lhs->charging == rhs->charging &&
           lhs->discharging == rhs->discharging &&
           lhs->external_power_present == rhs->external_power_present &&
           lhs->battery_present == rhs->battery_present &&
           lhs->battery_mv == rhs->battery_mv &&
           lhs->system_mv == rhs->system_mv &&
           lhs->battery_percent == rhs->battery_percent;
}

static void power_service_store_state(const board_power_state_t *next_state,
                                      bool *state_changed,
                                      power_state_changed_cb_t *callback,
                                      const board_power_state_t **published_state)
{
    taskENTER_CRITICAL(&s_lock);
    uint8_t active_index = s_active_state_index;
    uint8_t inactive_index = active_index ^ 1U;

    *state_changed =
        !power_service_state_equal(&s_state_buffers[active_index], next_state);
    if (*state_changed) {
        /* 先写入非活动缓冲，再切换活动索引，避免读到半更新状态。 */
        s_state_buffers[inactive_index] = *next_state;
        s_active_state_index = inactive_index;
        *published_state = &s_state_buffers[inactive_index];
        *callback = s_callback;
    } else {
        *published_state = &s_state_buffers[active_index];
        *callback = NULL;
    }
    taskEXIT_CRITICAL(&s_lock);
}

static void power_service_log_failure(esp_err_t ret)
{
    TickType_t now = xTaskGetTickCount();
    bool should_log = (s_last_failure_log_tick == 0) ||
                      ((now - s_last_failure_log_tick) >=
                       k_failure_log_throttle_ticks);

    if (should_log) {
        s_last_failure_log_tick = now;
        ESP_LOGW(TAG, "power refresh failed: %s", esp_err_to_name(ret));
    }
}

static void power_service_prepare_failure_state(board_power_state_t *state)
{
    const board_power_state_t *cached_state = board_power_get_cached_state();

    if (cached_state == NULL) {
        *state = power_service_make_unsampled_state();
        return;
    }

    *state = *cached_state;
    if (power_service_cached_state_has_history(cached_state)) {
        state->snapshot_stale = true;
        state->available = false;
    }
}

static void power_service_task(void *pv_parameter)
{
    (void)pv_parameter;

    while (1) {
        board_power_state_t next_state = {0};
        TickType_t delay_ticks = pdMS_TO_TICKS(1000);
        esp_err_t ret = board_power_refresh(&next_state);

        if (ret == ESP_OK) {
            bool state_changed = false;
            power_state_changed_cb_t callback = NULL;
            const board_power_state_t *published_state = NULL;

            s_failure_count = 0;
            s_last_failure_log_tick = 0;
            power_service_store_state(&next_state, &state_changed, &callback,
                                      &published_state);
            if (state_changed && callback != NULL) {
                callback(published_state);
            }
        } else {
            power_service_prepare_failure_state(&next_state);

            bool state_changed = false;
            power_state_changed_cb_t callback = NULL;
            const board_power_state_t *published_state = NULL;
            power_service_store_state(&next_state, &state_changed, &callback,
                                      &published_state);
            if (state_changed && callback != NULL) {
                callback(published_state);
            }

            ++s_failure_count;
            delay_ticks =
                s_failure_count >= 3 ? pdMS_TO_TICKS(5000)
                                    : pdMS_TO_TICKS(2000);
            power_service_log_failure(ret);
        }

        vTaskDelay(delay_ticks);
    }
}

esp_err_t power_service_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    power_service_copy_cached_state();
    s_failure_count = 0;
    s_last_failure_log_tick = 0;
    s_started = false;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t power_service_start(void)
{
    esp_err_t ret = power_service_init();
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_started) {
        return ESP_OK;
    }

    BaseType_t ok =
        xTaskCreate(power_service_task, "power_service", 4096, NULL, 5,
                    &s_task_handle);
    if (ok != pdPASS) {
        s_task_handle = NULL;
        return ESP_FAIL;
    }

    s_started = true;
    return ESP_OK;
}

void power_service_register_callback(power_state_changed_cb_t cb)
{
    taskENTER_CRITICAL(&s_lock);
    s_callback = cb;
    taskEXIT_CRITICAL(&s_lock);
}

const board_power_state_t *power_service_get_state(void)
{
    const board_power_state_t *state = NULL;

    taskENTER_CRITICAL(&s_lock);
    state = &s_state_buffers[s_active_state_index];
    taskEXIT_CRITICAL(&s_lock);
    return state;
}
