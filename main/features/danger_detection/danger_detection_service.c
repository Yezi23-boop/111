#include "danger_detection_service.h"

#include <stdbool.h>
#include <string.h>

#include "features/alerts/app_alert_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "traffic_audio_runtime.h"
#include "traffic_inference_postprocess.h"

#define TAG "danger_detection"
#define DANGER_DETECTION_STOP_TIMEOUT_MS 2000U

typedef struct {
    bool initialized;
    bool callback_registered;
    bool runtime_started;
    danger_detection_snapshot_t snapshot;
    portMUX_TYPE lock;
} danger_detection_service_state_t;

static danger_detection_service_state_t s_service_state = {
    .initialized = false,
    .callback_registered = false,
    .runtime_started = false,
    .snapshot = {
        .state = DANGER_DETECTION_STATE_IDLE,
        .stable_label = DANGER_DETECTION_LABEL_NONE,
        .last_detected_label = DANGER_DETECTION_LABEL_NONE,
        .last_detected_confidence = 0.0f,
        .horn_confidence = 0.0f,
        .siren_confidence = 0.0f,
        .alert_sequence = 0U,
        .last_error = ESP_OK,
        .danger_overlay_active = false,
    },
    .lock = portMUX_INITIALIZER_UNLOCKED,
};

static danger_detection_label_t danger_detection_map_label(
    traffic_inference_postprocess_stable_label_t label)
{
    switch (label) {
        case TRAFFIC_INFERENCE_POSTPROCESS_STABLE_LABEL_HORN:
            return DANGER_DETECTION_LABEL_HORN;
        case TRAFFIC_INFERENCE_POSTPROCESS_STABLE_LABEL_SIREN:
            return DANGER_DETECTION_LABEL_SIREN;
        case TRAFFIC_INFERENCE_POSTPROCESS_STABLE_LABEL_NONE:
        default:
            return DANGER_DETECTION_LABEL_NONE;
    }
}

static danger_detection_state_t danger_detection_map_runtime_state(
    traffic_audio_runtime_state_t runtime_state,
    danger_detection_state_t fallback)
{
    switch (runtime_state) {
        case TRAFFIC_AUDIO_RUNTIME_STATE_STARTING:
            return DANGER_DETECTION_STATE_STARTING;
        case TRAFFIC_AUDIO_RUNTIME_STATE_RUNNING:
            return DANGER_DETECTION_STATE_RUNNING;
        case TRAFFIC_AUDIO_RUNTIME_STATE_STOPPING:
            return DANGER_DETECTION_STATE_STOPPING;
        case TRAFFIC_AUDIO_RUNTIME_STATE_FAILED:
            return DANGER_DETECTION_STATE_ERROR;
        case TRAFFIC_AUDIO_RUNTIME_STATE_IDLE:
        default:
            return fallback;
    }
}

static void danger_detection_set_state(danger_detection_state_t state,
                                       esp_err_t last_error)
{
    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.snapshot.state = state;
    s_service_state.snapshot.last_error = last_error;
    taskEXIT_CRITICAL(&s_service_state.lock);
}

static void danger_detection_on_alert(
    const traffic_inference_postprocess_alert_t *alert,
    void *user_data)
{
    (void)user_data;

    if (alert == NULL) {
        return;
    }

    if (alert->action == TRAFFIC_INFERENCE_POSTPROCESS_ALERT_ACTION_RAISE) {
        app_alert_request_t request = {
            .source = APP_ALERT_SOURCE_TRAFFIC_AUDIO,
            .severity = APP_ALERT_SEVERITY_DANGER,
            .label = alert->label ==
                             TRAFFIC_INFERENCE_POSTPROCESS_STABLE_LABEL_HORN
                         ? APP_ALERT_LABEL_HORN
                         : APP_ALERT_LABEL_SIREN,
        };
        (void)app_alert_manager_raise(&request);

        taskENTER_CRITICAL(&s_service_state.lock);
        s_service_state.snapshot.state = DANGER_DETECTION_STATE_RUNNING;
        s_service_state.snapshot.stable_label =
            danger_detection_map_label(alert->label);
        s_service_state.snapshot.last_detected_label =
            danger_detection_map_label(alert->label);
        s_service_state.snapshot.last_detected_confidence =
            alert->confidence_score;
        s_service_state.snapshot.alert_sequence += 1U;
        s_service_state.snapshot.danger_overlay_active = true;
        taskEXIT_CRITICAL(&s_service_state.lock);
    } else if (alert->action ==
               TRAFFIC_INFERENCE_POSTPROCESS_ALERT_ACTION_CLEAR) {
        (void)app_alert_manager_clear(APP_ALERT_SOURCE_TRAFFIC_AUDIO);

        taskENTER_CRITICAL(&s_service_state.lock);
        s_service_state.snapshot.state = DANGER_DETECTION_STATE_RUNNING;
        s_service_state.snapshot.stable_label = DANGER_DETECTION_LABEL_NONE;
        s_service_state.snapshot.danger_overlay_active = false;
        taskEXIT_CRITICAL(&s_service_state.lock);
    }
}

esp_err_t danger_detection_service_init(void)
{
    if (s_service_state.initialized) {
        return ESP_OK;
    }

    esp_err_t ret = app_alert_manager_init();
    if (ret != ESP_OK) {
        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);
        return ret;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.initialized = true;
    s_service_state.snapshot.state = DANGER_DETECTION_STATE_IDLE;
    s_service_state.snapshot.stable_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_confidence = 0.0f;
    s_service_state.snapshot.horn_confidence = 0.0f;
    s_service_state.snapshot.siren_confidence = 0.0f;
    s_service_state.snapshot.alert_sequence = 0U;
    s_service_state.snapshot.last_error = ESP_OK;
    s_service_state.snapshot.danger_overlay_active = false;
    taskEXIT_CRITICAL(&s_service_state.lock);
    return ESP_OK;
}

esp_err_t danger_detection_service_start(void)
{
    esp_err_t ret = danger_detection_service_init();
    if (ret != ESP_OK) {
        return ret;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    const bool already_running = s_service_state.runtime_started;
    taskEXIT_CRITICAL(&s_service_state.lock);
    if (already_running) {
        return ESP_OK;
    }

    traffic_audio_runtime_config_t config = {
        .input_chunk_frames = 0U,
        .read_timeout_ms = 250U,
        .task_stack_size = 8192U,
        .task_priority = 5U,
    };

    danger_detection_set_state(DANGER_DETECTION_STATE_STARTING, ESP_OK);
    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.snapshot.stable_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_confidence = 0.0f;
    s_service_state.snapshot.horn_confidence = 0.0f;
    s_service_state.snapshot.siren_confidence = 0.0f;
    s_service_state.snapshot.alert_sequence = 0U;
    s_service_state.snapshot.danger_overlay_active = false;
    taskEXIT_CRITICAL(&s_service_state.lock);

    ret = traffic_inference_postprocess_set_alert_callback(
        danger_detection_on_alert, NULL);
    if (ret != ESP_OK) {
        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);
        return ret;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.callback_registered = true;
    taskEXIT_CRITICAL(&s_service_state.lock);

    ret = traffic_audio_runtime_start(&config);
    if (ret != ESP_OK) {
        (void)traffic_inference_postprocess_set_alert_callback(NULL, NULL);
        taskENTER_CRITICAL(&s_service_state.lock);
        s_service_state.callback_registered = false;
        taskEXIT_CRITICAL(&s_service_state.lock);
        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);
        return ret;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.runtime_started = true;
    taskEXIT_CRITICAL(&s_service_state.lock);
    ESP_LOGI(TAG, "danger detection runtime started");
    return ESP_OK;
}

esp_err_t danger_detection_service_stop(uint32_t timeout_ms)
{
    if (!s_service_state.initialized) {
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    const bool runtime_started = s_service_state.runtime_started;
    const bool callback_registered = s_service_state.callback_registered;
    taskEXIT_CRITICAL(&s_service_state.lock);

    if (!runtime_started && !callback_registered) {
        danger_detection_set_state(DANGER_DETECTION_STATE_IDLE, ESP_OK);
        return ESP_OK;
    }

    danger_detection_set_state(DANGER_DETECTION_STATE_STOPPING, ESP_OK);
    (void)app_alert_manager_clear(APP_ALERT_SOURCE_TRAFFIC_AUDIO);

    esp_err_t ret = ESP_OK;
    if (runtime_started) {
        ret = traffic_audio_runtime_stop(timeout_ms == 0U
                                             ? DANGER_DETECTION_STOP_TIMEOUT_MS
                                             : timeout_ms);
    }

    if (callback_registered) {
        esp_err_t clear_ret =
            traffic_inference_postprocess_set_alert_callback(NULL, NULL);
        if (ret == ESP_OK && clear_ret != ESP_OK) {
            ret = clear_ret;
        }
    }

    taskENTER_CRITICAL(&s_service_state.lock);
    s_service_state.callback_registered = false;
    s_service_state.runtime_started = false;
    s_service_state.snapshot.stable_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_label = DANGER_DETECTION_LABEL_NONE;
    s_service_state.snapshot.last_detected_confidence = 0.0f;
    s_service_state.snapshot.horn_confidence = 0.0f;
    s_service_state.snapshot.siren_confidence = 0.0f;
    s_service_state.snapshot.alert_sequence = 0U;
    s_service_state.snapshot.danger_overlay_active = false;
    taskEXIT_CRITICAL(&s_service_state.lock);

    if (ret != ESP_OK) {
        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);
        return ret;
    }

    danger_detection_set_state(DANGER_DETECTION_STATE_IDLE, ESP_OK);
    ESP_LOGI(TAG, "danger detection runtime stopped");
    return ESP_OK;
}

danger_detection_snapshot_t danger_detection_service_get_snapshot(void)
{
    danger_detection_snapshot_t snapshot;
    bool runtime_started = false;

    taskENTER_CRITICAL(&s_service_state.lock);
    snapshot = s_service_state.snapshot;
    runtime_started = s_service_state.runtime_started;
    taskEXIT_CRITICAL(&s_service_state.lock);

    if (runtime_started) {
        traffic_audio_runtime_state_t runtime_state =
            traffic_audio_runtime_get_state();
        const traffic_inference_postprocess_snapshot_t postprocess_snapshot =
            traffic_inference_postprocess_get_latest_snapshot();
        snapshot.state = danger_detection_map_runtime_state(runtime_state,
                                                            snapshot.state);
        snapshot.horn_confidence = postprocess_snapshot.horn_score;
        snapshot.siren_confidence = postprocess_snapshot.siren_score;
        if (runtime_state == TRAFFIC_AUDIO_RUNTIME_STATE_FAILED &&
            snapshot.last_error == ESP_OK) {
            snapshot.last_error = ESP_FAIL;
        }
    }

    return snapshot;
}
