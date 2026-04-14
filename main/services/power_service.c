#include "services/power_service.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * 电源服务实现说明：
 * - 对底层电源采样结果做周期刷新、抖动抑制和失败降级；
 * - 采用双缓冲发布状态，读取方永远只能看到一份完整快照；
 * - 该模块本身不直接决定 UI 表现，只输出“当前电源状态是否可信”这一事实。
 */

static const char *TAG = "POWER_SERVICE";
static const TickType_t k_failure_log_throttle_ticks = pdMS_TO_TICKS(5000);
static const uint16_t k_voltage_jitter_threshold_mv = 20;

static TaskHandle_t s_task_handle = NULL; // 后台采样任务句柄
static board_power_state_t s_state_buffers[2] = {
    {.battery_percent = UINT8_MAX},
    {.battery_percent = UINT8_MAX},
};
static power_state_changed_cb_t s_callback = NULL; // 状态变化回调
static bool s_initialized = false;
static bool s_started = false;
static uint32_t s_failure_count = 0;
static TickType_t s_last_failure_log_tick = 0;
static uint8_t s_active_state_index = 0; // 当前发布缓冲索引（0 或 1）
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static board_power_state_t power_service_make_unsampled_state(void)
{
    board_power_state_t state = {0};
    /* UINT8_MAX 作为“未知电量”的哨兵值，避免把 0% 误判成有效结果。 */
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

    if (cached_state != NULL)
    {
        snapshot = *cached_state;
    }

    /* 初始化时同步两份缓冲，让首次读取就能拿到一致视图。 */
    taskENTER_CRITICAL(&s_lock);
    s_state_buffers[0] = snapshot;
    s_state_buffers[1] = snapshot;
    s_active_state_index = 0;
    taskEXIT_CRITICAL(&s_lock);
}

static bool power_service_mv_changed(uint16_t lhs, uint16_t rhs)
{
    /* 小幅电压抖动在电池设备上很常见，不值得触发 UI 全量刷新。 */
    uint16_t delta = lhs > rhs ? (lhs - rhs) : (rhs - lhs);
    return delta >= k_voltage_jitter_threshold_mv;
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
           !power_service_mv_changed(lhs->battery_mv, rhs->battery_mv) &&
           !power_service_mv_changed(lhs->system_mv, rhs->system_mv) &&
           lhs->battery_percent == rhs->battery_percent;
}

static void power_service_log_state_change(const board_power_state_t *state)
{
    if (state == NULL)
    {
        ESP_LOGI(TAG, "power state changed: unavailable");
        return;
    }

    if (state->battery_data_valid)
    {
        ESP_LOGI(TAG,
                 "power state changed: available=%d stale=%d ext=%d bat=%d chg=%d dchg=%d vbat=%umV vsys=%umV soc=%u%%",
                 state->available, state->snapshot_stale,
                 state->external_power_present, state->battery_present,
                 state->charging, state->discharging, state->battery_mv,
                 state->system_mv, state->battery_percent);
        return;
    }

    ESP_LOGI(TAG,
             "power state changed: available=%d stale=%d ext=%d bat=%d chg=%d dchg=%d vbat=%umV vsys=%umV soc=unknown",
             state->available, state->snapshot_stale,
             state->external_power_present, state->battery_present,
             state->charging, state->discharging, state->battery_mv,
             state->system_mv);
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
    if (*state_changed)
    {
        /* 先写入非活动缓冲，再切换活动索引，避免读到半更新状态。 */
        s_state_buffers[inactive_index] = *next_state;
        s_active_state_index = inactive_index;
        *published_state = &s_state_buffers[inactive_index];
        *callback = s_callback;
    }
    else
    {
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

    if (should_log)
    {
        s_last_failure_log_tick = now;
        ESP_LOGW(TAG, "power refresh failed: %s", esp_err_to_name(ret));
    }
}

static void power_service_prepare_failure_state(board_power_state_t *state)
{
    const board_power_state_t *cached_state = board_power_get_cached_state();

    if (cached_state == NULL)
    {
        *state = power_service_make_unsampled_state();
        return;
    }

    /* 采样失败时尽量复用最近快照，让 UI 还能显示“上次已知状态”。 */
    *state = *cached_state;
    if (power_service_cached_state_has_history(cached_state))
    {
        state->snapshot_stale = true;
        state->available = false;
    }
}

static void power_service_task(void *pv_parameter)
{
    (void)pv_parameter;

    while (1)
    {
        board_power_state_t next_state = {0};
        TickType_t delay_ticks = pdMS_TO_TICKS(1000);
        esp_err_t ret = board_power_refresh(&next_state);

        if (ret == ESP_OK)
        {
            /* 采样成功时恢复正常轮询节奏，并按需通知观察者。 */
            bool state_changed = false;
            power_state_changed_cb_t callback = NULL;
            const board_power_state_t *published_state = NULL;

            s_failure_count = 0;
            s_last_failure_log_tick = 0;
            power_service_store_state(&next_state, &state_changed, &callback,
                                      &published_state);
            if (state_changed)
            {
                power_service_log_state_change(published_state);
                if (callback != NULL)
                {
                    callback(published_state);
                }
            }
        }
        else
        {
            /* 连续失败后逐步退避，减轻总线异常或设备缺失时的系统负担。 */
            power_service_prepare_failure_state(&next_state);

            bool state_changed = false;
            power_state_changed_cb_t callback = NULL;
            const board_power_state_t *published_state = NULL;
            power_service_store_state(&next_state, &state_changed, &callback,
                                      &published_state);
            if (state_changed)
            {
                power_service_log_state_change(published_state);
                if (callback != NULL)
                {
                    callback(published_state);
                }
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
    if (s_initialized)
    {
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
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (s_started)
    {
        return ESP_OK;
    }

    /* 电源状态变化不要求极低延迟，普通优先级后台任务即可。 */
    BaseType_t ok =
        xTaskCreate(power_service_task, "power_service", 4096, NULL, 5,
                    &s_task_handle);
    if (ok != pdPASS)
    {
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
