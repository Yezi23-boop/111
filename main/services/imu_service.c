#include "services/imu_service.h"

#include <stdint.h>
#include <string.h>

#include "app/board_imu.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "imu_motion.h"

static const char *TAG = "imu_service";

static const TickType_t k_retry_delay_ticks = pdMS_TO_TICKS(5000);
static const TickType_t k_event_wait_timeout_ticks = pdMS_TO_TICKS(10000);
static const TickType_t k_wom_poll_fallback_ticks = pdMS_TO_TICKS(20);

enum
{
    k_final_pose_max_sample_count = 8U, /* 防止板级配置把栈上采样窗口放大。 */
};

typedef struct
{
    bool motion_pass;
    bool final_pose_pass;
    bool detected;
    imu_service_raise_reason_t reason;
    uint32_t sample_count;
    imu_motion_reason_t motion_reason;
    int16_t motion_roll_delta_degrees;
    int32_t final_accel_norm_mg;
    int32_t final_accel_stability_mg;
    qmi8658c_raw_sample_t final_raw;
} imu_service_motion_window_result_t;

typedef struct
{
    bool initialized;
    bool started;
    bool gpio_ready;
    bool int1_path_fault_latched;
    uint32_t next_wom_event_id;
    TaskHandle_t task_handle;
    portMUX_TYPE lock;
    imu_service_snapshot_t snapshot;
} imu_service_context_t;

static const char *imu_service_face_axis_text(board_imu_face_axis_t axis);

static imu_service_context_t s_imu_service = {
    .initialized = false,
    .started = false,
    .gpio_ready = false,
    .int1_path_fault_latched = false,
    .next_wom_event_id = 0,
    .task_handle = NULL,
    .lock = portMUX_INITIALIZER_UNLOCKED,
    .snapshot = {
        .state = IMU_SERVICE_STATE_STOPPED,
        .present = false,
        .last_error = ESP_OK,
    },
};

static void IRAM_ATTR imu_service_int1_isr(void *arg)
{
    (void)arg;

    BaseType_t should_yield = pdFALSE;
    TaskHandle_t task = s_imu_service.task_handle;
    if (task != NULL)
    {
        vTaskNotifyGiveFromISR(task, &should_yield);
    }
    if (should_yield == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

static void imu_service_store_state(imu_service_state_t state,
                                    esp_err_t error)
{
    taskENTER_CRITICAL(&s_imu_service.lock);
    s_imu_service.snapshot.state = state;
    s_imu_service.snapshot.last_error = error;
    taskEXIT_CRITICAL(&s_imu_service.lock);
}

static void imu_service_store_identity(const qmi8658c_identity_t *identity)
{
    taskENTER_CRITICAL(&s_imu_service.lock);
    s_imu_service.snapshot.present = identity->present;
    s_imu_service.snapshot.who_am_i = identity->who_am_i;
    s_imu_service.snapshot.revision_id = identity->revision_id;
    taskEXIT_CRITICAL(&s_imu_service.lock);
}

static uint32_t imu_service_store_wom_event(const qmi8658c_status1_t *status,
                                            const qmi8658c_raw_sample_t *raw,
                                            bool from_irq,
                                            bool confirmed,
                                            int64_t now_us)
{
    uint32_t event_id = 0;
    taskENTER_CRITICAL(&s_imu_service.lock);
    s_imu_service.snapshot.state = IMU_SERVICE_STATE_RUNNING;
    if (from_irq)
    {
        s_imu_service.snapshot.wom_irq_count++;
    }
    else if (confirmed)
    {
        s_imu_service.snapshot.wom_poll_event_count++;
    }
    s_imu_service.snapshot.last_status1 = *status;
    s_imu_service.snapshot.last_error = ESP_OK;
    if (raw != NULL)
    {
        s_imu_service.snapshot.sample_count++;
        s_imu_service.snapshot.last_sample_time_us = now_us;
        s_imu_service.snapshot.last_raw = *raw;
    }
    if (confirmed)
    {
        event_id = ++s_imu_service.next_wom_event_id;
        s_imu_service.snapshot.wom_event_count++;
        s_imu_service.snapshot.last_wom_time_us = now_us;
    }
    else if (from_irq)
    {
        s_imu_service.snapshot.spurious_irq_count++;
    }
    taskEXIT_CRITICAL(&s_imu_service.lock);
    return event_id;
}

static void imu_service_store_int1_path_state(bool usable)
{
    taskENTER_CRITICAL(&s_imu_service.lock);
    s_imu_service.snapshot.int1_path_usable = usable;
    s_imu_service.snapshot.poll_fallback_active = !usable;
    taskEXIT_CRITICAL(&s_imu_service.lock);
}

static void imu_service_store_raise_result(
    const imu_service_motion_window_result_t *result)
{
    taskENTER_CRITICAL(&s_imu_service.lock);
    s_imu_service.snapshot.state = IMU_SERVICE_STATE_RUNNING;
    s_imu_service.snapshot.motion_window_count++;
    s_imu_service.snapshot.last_raise_detected = result->detected;
    s_imu_service.snapshot.last_raise_reason = result->reason;
    s_imu_service.snapshot.last_motion_sample_count = result->sample_count;
    s_imu_service.snapshot.last_motion_reason = result->motion_reason;
    s_imu_service.snapshot.last_motion_roll_delta_degrees =
        result->motion_roll_delta_degrees;
    s_imu_service.snapshot.last_motion_pass = result->motion_pass;
    s_imu_service.snapshot.last_final_pose_pass = result->final_pose_pass;
    s_imu_service.snapshot.last_final_accel_norm_mg =
        result->final_accel_norm_mg;
    s_imu_service.snapshot.last_final_accel_stability_mg =
        result->final_accel_stability_mg;
    s_imu_service.snapshot.last_final_raw = result->final_raw;
    if (result->detected)
    {
        s_imu_service.snapshot.raise_detected_count++;
    }
    taskEXIT_CRITICAL(&s_imu_service.lock);
}

static esp_err_t imu_service_configure_int1_gpio(void)
{
    const board_imu_config_t *board_config = board_imu_get_config();
    if (s_imu_service.gpio_ready)
    {
        return ESP_OK;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << board_config->qmi_int1_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        return ret;
    }

    ret = gpio_isr_handler_add(board_config->qmi_int1_gpio,
                               imu_service_int1_isr, NULL);
    if (ret == ESP_ERR_INVALID_STATE)
    {
        (void)gpio_isr_handler_remove(board_config->qmi_int1_gpio);
        ret = gpio_isr_handler_add(board_config->qmi_int1_gpio,
                                   imu_service_int1_isr, NULL);
    }
    if (ret != ESP_OK)
    {
        return ret;
    }

    s_imu_service.gpio_ready = true;
    ESP_LOGI(TAG, "int1_gpio_ready: gpio=%d level=%d intr=anyedge",
             board_config->qmi_int1_gpio,
             gpio_get_level(board_config->qmi_int1_gpio));
    return ESP_OK;
}

static esp_err_t imu_service_validate_board_config(
    const board_imu_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "board imu config is null");
    ESP_RETURN_ON_FALSE(config->qmi_i2c_addr_7bit <= 0x7F,
                        ESP_ERR_INVALID_ARG, TAG,
                        "qmi i2c addr is not 7-bit");
    ESP_RETURN_ON_FALSE(config->wom_threshold_mg > 0, ESP_ERR_INVALID_ARG,
                        TAG, "wom threshold must be > 0");
    ESP_RETURN_ON_FALSE(config->motion_sample_count > 0,
                        ESP_ERR_INVALID_ARG, TAG,
                        "motion sample count must be > 0");
    ESP_RETURN_ON_FALSE(config->motion_sample_period_ms > 0,
                        ESP_ERR_INVALID_ARG, TAG,
                        "motion sample period must be > 0");
    ESP_RETURN_ON_FALSE(config->accel_lsb_per_g > 0, ESP_ERR_INVALID_ARG,
                        TAG, "accel lsb per g must be > 0");
    ESP_RETURN_ON_FALSE(config->final_pose_sample_count > 0 &&
                            config->final_pose_sample_count <=
                                k_final_pose_max_sample_count,
                        ESP_ERR_INVALID_ARG, TAG,
                        "final pose sample count out of range");
    return ESP_OK;
}

static esp_err_t imu_service_probe_and_configure_wom(void)
{
    imu_service_store_state(IMU_SERVICE_STATE_PROBING, ESP_OK);
    const board_imu_config_t *board_config = board_imu_get_config();
    ESP_RETURN_ON_ERROR(imu_service_validate_board_config(board_config), TAG,
                        "board imu config invalid");

    const qmi8658c_bus_config_t bus_config = {
        .i2c_addr_7bit = board_config->qmi_i2c_addr_7bit,
    };
    esp_err_t ret = qmi8658c_init_with_bus_config(&bus_config);
    if (ret != ESP_OK)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
        return ret;
    }

    qmi8658c_identity_t identity = {0};
    ret = qmi8658c_probe(&identity);
    if (ret != ESP_OK)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
        return ret;
    }

    imu_service_store_identity(&identity);
    ESP_LOGI(TAG, "probe: present=%d who_am_i=0x%02x revision_id=0x%02x",
             identity.present,
             identity.who_am_i,
             identity.revision_id);

    if (!identity.present)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ESP_ERR_NOT_FOUND);
        return ESP_ERR_NOT_FOUND;
    }

    const qmi8658c_wom_config_t config = {
        .threshold_mg = board_config->wom_threshold_mg,
        .blanking_samples = board_config->wom_blanking_samples,
        .accel_fs = board_config->wom_accel_fs_code,
        .accel_odr = board_config->wom_accel_odr_code,
        .interrupt = QMI8658C_WOM_INTERRUPT_INT1_INITIAL_LOW,
    };

    ret = qmi8658c_configure_wake_on_motion(&config);
    if (ret != ESP_OK)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
        return ret;
    }

    ret = imu_service_configure_int1_gpio();
    if (ret != ESP_OK)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
        return ret;
    }

    qmi8658c_status1_t status = {0};
    ret = qmi8658c_read_status1(&status);
    if (ret != ESP_OK)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
        return ret;
    }

    qmi8658c_statusint_t statusint = {0};
    ret = qmi8658c_read_statusint(&statusint);
    if (ret != ESP_OK)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
        return ret;
    }

    const int gpio_level = gpio_get_level(board_config->qmi_int1_gpio);
    qmi8658c_statusint_t verify_statusint = {0};
    ret = qmi8658c_read_statusint(&verify_statusint);
    if (ret != ESP_OK)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
        return ret;
    }
    const bool int1_mirror_stable =
        statusint.int1_high == verify_statusint.int1_high;
    statusint = verify_statusint;
    const bool level_matches =
        gpio_level == (statusint.int1_high ? 1 : 0);
    if (int1_mirror_stable && !level_matches)
    {
        s_imu_service.int1_path_fault_latched = true;
    }
    const bool int1_path_usable = !s_imu_service.int1_path_fault_latched;
    imu_service_store_int1_path_state(int1_path_usable);
    if (!int1_path_usable)
    {
        ESP_RETURN_ON_ERROR(gpio_intr_disable(board_config->qmi_int1_gpio), TAG,
                            "disable unusable INT1 GPIO interrupt failed");
        ESP_LOGW(TAG,
                 "int1_path_unusable: gpio=%d level=%d statusint=0x%02x int1_mirror=%d mirror_stable=%d fallback_poll_ms=%u",
                 board_config->qmi_int1_gpio,
                 gpio_level,
                 statusint.raw_statusint,
                 statusint.int1_high,
                 int1_mirror_stable,
                 (unsigned)pdTICKS_TO_MS(k_wom_poll_fallback_ticks));
    }

    ESP_LOGI(TAG,
             "wom_configured: threshold_mg=%u blanking=%u int1_gpio=%d level=%d statusint=0x%02x int1_mirror=%d mirror_stable=%d int1_usable=%d status1=0x%02x action=log_only",
             board_config->wom_threshold_mg,
             board_config->wom_blanking_samples,
             board_config->qmi_int1_gpio,
             gpio_level,
             statusint.raw_statusint,
             statusint.int1_high,
             int1_mirror_stable,
             int1_path_usable,
             status.raw_status1);
    ESP_LOGI(TAG,
             "raise_config: source=raw_motion motion_samples=%u motion_period_ms=%u final_norm_min_mg=%d final_norm_max_mg=%d final_stability_max_mg=%d face_axis=%s face_threshold_raw=%d",
             (unsigned)board_config->motion_sample_count,
             (unsigned)board_config->motion_sample_period_ms,
             (int)board_config->final_norm_min_mg,
             (int)board_config->final_norm_max_mg,
             (int)board_config->final_stability_max_mg,
             imu_service_face_axis_text(board_config->face_axis),
             (int)board_config->face_axis_threshold_raw);
    imu_service_store_state(IMU_SERVICE_STATE_RUNNING, ESP_OK);
    return ESP_OK;
}

static uint32_t imu_service_isqrt_u64(uint64_t value)
{
    uint64_t bit = 1ULL << 62;
    uint64_t result = 0;

    while (bit > value)
    {
        bit >>= 2;
    }

    while (bit != 0)
    {
        if (value >= result + bit)
        {
            value -= result + bit;
            result = (result >> 1) + bit;
        }
        else
        {
            result >>= 1;
        }
        bit >>= 2;
    }

    return (uint32_t)result;
}

static int32_t imu_service_accel_norm_mg(const qmi8658c_raw_sample_t *sample)
{
    const board_imu_config_t *board_config = board_imu_get_config();
    const int32_t x = sample->accel_x;
    const int32_t y = sample->accel_y;
    const int32_t z = sample->accel_z;
    const uint64_t sum =
        (uint64_t)((int64_t)x * x) +
        (uint64_t)((int64_t)y * y) +
        (uint64_t)((int64_t)z * z);
    const uint32_t norm_raw = imu_service_isqrt_u64(sum);
    return (int32_t)((norm_raw * 1000U +
                      (board_config->accel_lsb_per_g / 2U)) /
                     board_config->accel_lsb_per_g);
}

static uint32_t imu_service_abs_i32(int32_t value)
{
    return value < 0 ? (uint32_t)(-value) : (uint32_t)value;
}

static int16_t imu_service_normalize_accel(int16_t raw)
{
    const int32_t lsb_per_g = board_imu_get_config()->accel_lsb_per_g;
    int32_t normalized = ((int32_t)raw * 1024) / lsb_per_g;
    if (normalized > INT16_MAX)
    {
        normalized = INT16_MAX;
    }
    if (normalized < INT16_MIN)
    {
        normalized = INT16_MIN;
    }
    return (int16_t)normalized;
}

static bool imu_service_final_face_axis_pass(
    const board_imu_config_t *board_config,
    const qmi8658c_raw_sample_t *sample)
{
    switch (board_config->face_axis)
    {
    case BOARD_IMU_FACE_AXIS_NEG_Z:
        return sample->accel_z <= board_config->face_axis_threshold_raw;
    default:
        return false;
    }
}

static const char *imu_service_face_axis_text(board_imu_face_axis_t axis)
{
    switch (axis)
    {
    case BOARD_IMU_FACE_AXIS_NEG_Z:
        return "-Z";
    default:
        return "UNKNOWN";
    }
}

static esp_err_t imu_service_read_final_pose(
    imu_service_motion_window_result_t *result,
    uint32_t event_id,
    const char *event_source)
{
    const board_imu_config_t *board_config = board_imu_get_config();
    qmi8658c_raw_sample_t samples[k_final_pose_max_sample_count] = {0};
    uint32_t norm_sum_mg = 0;

    for (uint32_t i = 0; i < board_config->final_pose_sample_count; ++i)
    {
        vTaskDelay(pdMS_TO_TICKS(board_config->motion_sample_period_ms));
        esp_err_t ret = qmi8658c_read_raw(&samples[i]);
        if (ret != ESP_OK)
        {
            return ret;
        }
        norm_sum_mg += (uint32_t)imu_service_accel_norm_mg(&samples[i]);
    }

    const qmi8658c_raw_sample_t *last =
        &samples[board_config->final_pose_sample_count - 1U];
    uint32_t max_delta_raw = 0;
    for (uint32_t i = 0; i + 1U < board_config->final_pose_sample_count; ++i)
    {
        const uint32_t dx = imu_service_abs_i32(
            (int32_t)samples[i + 1U].accel_x - samples[i].accel_x);
        const uint32_t dy = imu_service_abs_i32(
            (int32_t)samples[i + 1U].accel_y - samples[i].accel_y);
        const uint32_t dz = imu_service_abs_i32(
            (int32_t)samples[i + 1U].accel_z - samples[i].accel_z);
        if (dx > max_delta_raw)
        {
            max_delta_raw = dx;
        }
        if (dy > max_delta_raw)
        {
            max_delta_raw = dy;
        }
        if (dz > max_delta_raw)
        {
            max_delta_raw = dz;
        }
    }

    result->final_raw = *last;
    result->final_accel_norm_mg =
        (int32_t)(norm_sum_mg / board_config->final_pose_sample_count);
    result->final_accel_stability_mg =
        (int32_t)((max_delta_raw * 1000U +
                   (board_config->accel_lsb_per_g / 2U)) /
                  board_config->accel_lsb_per_g);

    const bool norm_pass =
        result->final_accel_norm_mg >= board_config->final_norm_min_mg &&
        result->final_accel_norm_mg <= board_config->final_norm_max_mg;
    const bool stable_pass =
        result->final_accel_stability_mg <=
        board_config->final_stability_max_mg;
    const bool face_pass = imu_service_final_face_axis_pass(
        board_config, &result->final_raw);
    result->final_pose_pass = norm_pass && stable_pass && face_pass;

    ESP_LOGI(TAG,
             "final_pose: event_id=%u source=%s pass=%d norm_pass=%d stable_pass=%d face_pass=%d accel=(%d,%d,%d) norm_mg=%d stability_mg=%d face_axis=%s face_threshold_raw=%d action=log_only",
             (unsigned)event_id,
             event_source,
             result->final_pose_pass ? 1 : 0,
             norm_pass ? 1 : 0,
             stable_pass ? 1 : 0,
             face_pass ? 1 : 0,
             result->final_raw.accel_x,
             result->final_raw.accel_y,
             result->final_raw.accel_z,
             (int)result->final_accel_norm_mg,
             (int)result->final_accel_stability_mg,
             imu_service_face_axis_text(board_config->face_axis),
             (int)board_config->face_axis_threshold_raw);
    return ESP_OK;
}

static imu_service_raise_reason_t imu_service_evaluate_raise_window(
    const imu_service_motion_window_result_t *result)
{
    if (!result->motion_pass)
    {
        return IMU_SERVICE_RAISE_REASON_MOTION_REJECT;
    }
    const board_imu_config_t *board_config = board_imu_get_config();
    if (result->final_accel_norm_mg < board_config->final_norm_min_mg ||
        result->final_accel_norm_mg > board_config->final_norm_max_mg)
    {
        return IMU_SERVICE_RAISE_REASON_FINAL_NORM;
    }
    if (result->final_accel_stability_mg >
        board_config->final_stability_max_mg)
    {
        return IMU_SERVICE_RAISE_REASON_FINAL_UNSTABLE;
    }
    if (!imu_service_final_face_axis_pass(board_config, &result->final_raw))
    {
        return IMU_SERVICE_RAISE_REASON_FINAL_POSE;
    }
    return IMU_SERVICE_RAISE_REASON_PASS;
}

static esp_err_t imu_service_run_raw_motion_window(
    imu_service_motion_window_result_t *result,
    uint32_t event_id,
    const char *event_source)
{
    const board_imu_config_t *board_config = board_imu_get_config();
    memset(result, 0, sizeof(*result));
    result->reason = IMU_SERVICE_RAISE_REASON_MOTION_REJECT;
    result->motion_reason = IMU_MOTION_REASON_WARMUP;

    esp_err_t ret = qmi8658c_disable_wake_on_motion();
    if (ret != ESP_OK)
    {
        return ret;
    }

    const qmi8658c_config_t config = {
        .accel_fs = board_config->motion_accel_fs_code,
        .accel_odr = board_config->motion_accel_odr_code,
        .gyro_fs = board_config->motion_gyro_fs_code,
        .gyro_odr = board_config->motion_gyro_odr_code,
        .accel_enable = true,
        .gyro_enable = true,
    };
    ret = qmi8658c_configure(&config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    imu_motion_state_t motion_state = {0};
    imu_motion_init(&motion_state, NULL);

    ESP_LOGI(TAG,
             "motion_window_start: event_id=%u source=%s samples=%u sample_period_ms=%u mode=raw_6axis action=log_only",
             (unsigned)event_id,
             event_source,
             board_config->motion_sample_count,
             (unsigned)board_config->motion_sample_period_ms);

    for (uint32_t i = 0; i < board_config->motion_sample_count; ++i)
    {
        vTaskDelay(pdMS_TO_TICKS(board_config->motion_sample_period_ms));

        qmi8658c_raw_sample_t sample = {0};
        ret = qmi8658c_read_raw(&sample);
        if (ret != ESP_OK)
        {
            return ret;
        }

        const imu_motion_sample_t motion_sample = {
            .x = imu_service_normalize_accel(sample.accel_x),
            .y = imu_service_normalize_accel(sample.accel_y),
            .z = imu_service_normalize_accel(sample.accel_z),
        };
        imu_motion_result_t motion_result = {0};
        const bool motion_detected =
            imu_motion_update(&motion_state, &motion_sample, &motion_result);
        result->sample_count++;
        if (!result->motion_pass || motion_detected)
        {
            result->motion_reason = motion_result.reason;
            result->motion_roll_delta_degrees =
                motion_result.roll_delta_degrees;
        }
        result->motion_pass = result->motion_pass || motion_detected;

        ESP_LOGI(TAG,
                 "imu_csv: source=raw_motion,label=unknown,event_id=%u,trigger=%s,index=%u,ax=%d,ay=%d,az=%d,gx=%d,gy=%d,gz=%d,nx=%d,ny=%d,nz=%d,motion_detected=%d,motion_reason=%s,roll_delta_deg=%d",
                 (unsigned)event_id,
                 event_source,
                 (unsigned)i,
                 sample.accel_x,
                 sample.accel_y,
                 sample.accel_z,
                 sample.gyro_x,
                 sample.gyro_y,
                 sample.gyro_z,
                 motion_sample.x,
                 motion_sample.y,
                 motion_sample.z,
                 motion_detected ? 1 : 0,
                 imu_motion_reason_text(motion_result.reason),
                 motion_result.roll_delta_degrees);
    }

    ESP_RETURN_ON_ERROR(imu_service_read_final_pose(result, event_id,
                                                   event_source),
                        TAG,
                        "read final pose failed");
    result->reason = imu_service_evaluate_raise_window(result);
    result->detected =
        result->motion_pass && result->final_pose_pass &&
        result->reason == IMU_SERVICE_RAISE_REASON_PASS;
    return ESP_OK;
}

static void imu_service_handle_wom_check(bool from_irq)
{
    const board_imu_config_t *board_config = board_imu_get_config();
    const int64_t now_us = esp_timer_get_time();
    const int level = gpio_get_level(board_config->qmi_int1_gpio);

    qmi8658c_statusint_t statusint = {0};
    esp_err_t ret = qmi8658c_read_statusint(&statusint);
    if (ret != ESP_OK)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
        ESP_LOGW(TAG, "statusint_read_failed_after_int1: %s",
                 esp_err_to_name(ret));
        return;
    }

    qmi8658c_status1_t status = {0};
    ret = qmi8658c_read_status1(&status);
    if (ret != ESP_OK)
    {
        imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
        ESP_LOGW(TAG, "status1_read_failed_after_int1: %s",
                 esp_err_to_name(ret));
        return;
    }

    qmi8658c_raw_sample_t raw = {0};
    qmi8658c_raw_sample_t *raw_ptr = NULL;
    if (status.wake_on_motion)
    {
        ret = qmi8658c_read_raw(&raw);
        if (ret == ESP_OK)
        {
            raw_ptr = &raw;
        }
        else
        {
            ESP_LOGW(TAG, "raw_read_after_wom_failed: %s",
                     esp_err_to_name(ret));
        }
    }

    if (!from_irq && !status.wake_on_motion)
    {
        return;
    }

    const char *event_source = from_irq ? "irq" : "poll";
    const uint32_t event_id =
        imu_service_store_wom_event(&status, raw_ptr, from_irq,
                                    status.wake_on_motion, now_us);

    if (status.wake_on_motion)
    {
        ESP_LOGI(TAG,
                 "wom_event: event_id=%u source=%s gpio=%d level=%d statusint=0x%02x int1_mirror=%d status1=0x%02x raw_accel=(%d,%d,%d) raw_gyro=(%d,%d,%d) action=log_only",
                 (unsigned)event_id,
                 event_source,
                 board_config->qmi_int1_gpio,
                 level,
                 statusint.raw_statusint,
                 statusint.int1_high,
                 status.raw_status1,
                 raw.accel_x,
                 raw.accel_y,
                 raw.accel_z,
                 raw.gyro_x,
                 raw.gyro_y,
                 raw.gyro_z);

        imu_service_motion_window_result_t result = {0};
        ret =
            imu_service_run_raw_motion_window(&result, event_id, event_source);
        if (ret == ESP_OK)
        {
            imu_service_store_raise_result(&result);
            ESP_LOGI(TAG,
                     "raise_result: event_id=%u source=%s raise_detected=%d motion_pass=%d final_pose_pass=%d reject_reason=%s motion_samples=%u motion_reason=%s roll_delta_deg=%d final_norm_mg=%d final_stability_mg=%d final_accel=(%d,%d,%d) action=log_only",
                     (unsigned)event_id,
                     event_source,
                     result.detected ? 1 : 0,
                     result.motion_pass ? 1 : 0,
                     result.final_pose_pass ? 1 : 0,
                     imu_service_raise_reason_text(result.reason),
                     (unsigned)result.sample_count,
                     imu_motion_reason_text(result.motion_reason),
                     result.motion_roll_delta_degrees,
                     (int)result.final_accel_norm_mg,
                     (int)result.final_accel_stability_mg,
                     result.final_raw.accel_x,
                     result.final_raw.accel_y,
                     result.final_raw.accel_z);
        }
        else
        {
            imu_service_store_state(IMU_SERVICE_STATE_ERROR, ret);
            ESP_LOGW(TAG, "raw_motion_window_failed: %s",
                     esp_err_to_name(ret));
        }

        ret = imu_service_probe_and_configure_wom();
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "wom_reconfigure_after_motion_failed: %s",
                     esp_err_to_name(ret));
        }
    }
    else
    {
        ESP_LOGI(TAG, "int1_no_wom: gpio=%d level=%d status1=0x%02x",
                 board_config->qmi_int1_gpio,
                 level,
                 status.raw_status1);
    }
}

static void imu_service_task(void *arg)
{
    (void)arg;

    while (1)
    {
        esp_err_t ret = imu_service_probe_and_configure_wom();
        if (ret != ESP_OK)
        {
            ESP_LOGW(TAG, "wom_config_failed: %s", esp_err_to_name(ret));
            vTaskDelay(k_retry_delay_ticks);
            continue;
        }

        while (1)
        {
            uint32_t notified = ulTaskNotifyTake(pdTRUE,
                                                 s_imu_service.int1_path_fault_latched
                                                     ? k_wom_poll_fallback_ticks
                                                     : k_event_wait_timeout_ticks);
            if (notified > 0)
            {
                imu_service_handle_wom_check(true);
                continue;
            }

            if (s_imu_service.int1_path_fault_latched)
            {
                imu_service_handle_wom_check(false);
                continue;
            }

            const gpio_num_t int1_gpio = board_imu_get_config()->qmi_int1_gpio;
            const int level = gpio_get_level(int1_gpio);
            ESP_LOGI(TAG, "wom_wait: gpio=%d level=%d", int1_gpio, level);

            /*
             * 当前 WoM 固定使用 INT1 初始低电平。若启动阶段在 ISR 安装前已经
             * 发生 WoM，GPIO 会保持高电平且不会再产生可等待的首个边沿；
             * 超时路径主动读取 STATUS1，既恢复中断线，也保留该事件的诊断证据。
             */
            if (level != 0)
            {
                ESP_LOGW(TAG, "wom_poll_recovery: gpio=%d level=%d",
                          int1_gpio, level);
                imu_service_handle_wom_check(false);
            }
        }
    }
}

esp_err_t imu_service_init(void)
{
    if (s_imu_service.initialized)
    {
        return ESP_OK;
    }

    taskENTER_CRITICAL(&s_imu_service.lock);
    s_imu_service.snapshot.state = IMU_SERVICE_STATE_STOPPED;
    s_imu_service.snapshot.last_error = ESP_OK;
    s_imu_service.initialized = true;
    taskEXIT_CRITICAL(&s_imu_service.lock);
    return ESP_OK;
}

esp_err_t imu_service_start(void)
{
    esp_err_t ret = imu_service_init();
    if (ret != ESP_OK)
    {
        return ret;
    }

    taskENTER_CRITICAL(&s_imu_service.lock);
    const bool already_started = s_imu_service.started;
    taskEXIT_CRITICAL(&s_imu_service.lock);
    if (already_started)
    {
        return ESP_OK;
    }

    const BaseType_t ok = xTaskCreate(imu_service_task, "imu_service", 4096,
                                      NULL, 3, &s_imu_service.task_handle);
    if (ok != pdPASS)
    {
        s_imu_service.task_handle = NULL;
        return ESP_FAIL;
    }

    taskENTER_CRITICAL(&s_imu_service.lock);
    s_imu_service.started = true;
    taskEXIT_CRITICAL(&s_imu_service.lock);

    ESP_LOGI(TAG, "started: qmi_wom_int1=log_only");
    return ESP_OK;
}

esp_err_t imu_service_get_snapshot(imu_service_snapshot_t *out)
{
    if (out == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    taskENTER_CRITICAL(&s_imu_service.lock);
    *out = s_imu_service.snapshot;
    taskEXIT_CRITICAL(&s_imu_service.lock);
    return ESP_OK;
}

const char *imu_service_state_text(imu_service_state_t state)
{
    switch (state)
    {
    case IMU_SERVICE_STATE_STOPPED:
        return "STOPPED";
    case IMU_SERVICE_STATE_PROBING:
        return "PROBING";
    case IMU_SERVICE_STATE_RUNNING:
        return "RUNNING";
    case IMU_SERVICE_STATE_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

const char *imu_service_raise_reason_text(imu_service_raise_reason_t reason)
{
    switch (reason)
    {
    case IMU_SERVICE_RAISE_REASON_NONE:
        return "NONE";
    case IMU_SERVICE_RAISE_REASON_PASS:
        return "PASS";
    case IMU_SERVICE_RAISE_REASON_MOTION_REJECT:
        return "MOTION_REJECT";
    case IMU_SERVICE_RAISE_REASON_FINAL_NORM:
        return "FINAL_NORM";
    case IMU_SERVICE_RAISE_REASON_FINAL_UNSTABLE:
        return "FINAL_UNSTABLE";
    case IMU_SERVICE_RAISE_REASON_FINAL_POSE:
        return "FINAL_POSE";
    default:
        return "UNKNOWN";
    }
}
