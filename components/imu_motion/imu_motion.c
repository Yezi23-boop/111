#include "imu_motion.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const float k_degrees_per_radian = 57.2957795f;

typedef struct
{
    int32_t x_mean;
    int32_t y_mean;
    int32_t z_mean;
    uint32_t x_variance;
    uint32_t y_variance;
    uint32_t z_variance;
} imu_motion_stats_t;

static imu_motion_sample_t imu_motion_history_back(
    const imu_motion_state_t *state, uint8_t back)
{
    const uint8_t index =
        (uint8_t)((state->head + IMU_MOTION_HISTORY_SIZE - back) %
                  IMU_MOTION_HISTORY_SIZE);
    return state->history[index];
}

static int16_t imu_motion_clamp_i16(int32_t value)
{
    if (value > INT16_MAX)
    {
        return INT16_MAX;
    }
    if (value < INT16_MIN)
    {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static int16_t imu_motion_normalize_degrees(float degrees)
{
    while (degrees > 180.0f)
    {
        degrees -= 360.0f;
    }
    while (degrees < -180.0f)
    {
        degrees += 360.0f;
    }
    return imu_motion_clamp_i16((int32_t)lroundf(degrees));
}

static int16_t imu_motion_roll_delta_degrees(int16_t y, int16_t z,
                                             int16_t prev_y, int16_t prev_z)
{
    const float roll = atan2f((float)y, (float)z) * k_degrees_per_radian;
    const float prev_roll =
        atan2f((float)prev_y, (float)prev_z) * k_degrees_per_radian;
    return imu_motion_normalize_degrees(roll - prev_roll);
}

static imu_motion_stats_t imu_motion_compute_stats(
    const imu_motion_state_t *state, uint8_t newest_back)
{
    imu_motion_stats_t stats = {0};

    for (uint8_t i = 0; i < IMU_MOTION_STATS_WINDOW; ++i)
    {
        const imu_motion_sample_t sample =
            imu_motion_history_back(state, (uint8_t)(newest_back + i));
        stats.x_mean += sample.x;
        stats.y_mean += sample.y;
        stats.z_mean += sample.z;
    }

    stats.x_mean /= IMU_MOTION_STATS_WINDOW;
    stats.y_mean /= IMU_MOTION_STATS_WINDOW;
    stats.z_mean /= IMU_MOTION_STATS_WINDOW;

    for (uint8_t i = 0; i < IMU_MOTION_STATS_WINDOW; ++i)
    {
        const imu_motion_sample_t sample =
            imu_motion_history_back(state, (uint8_t)(newest_back + i));
        const int32_t dx = sample.x - stats.x_mean;
        const int32_t dy = sample.y - stats.y_mean;
        const int32_t dz = sample.z - stats.z_mean;
        stats.x_variance += (uint32_t)(dx * dx);
        stats.y_variance += (uint32_t)(dy * dy);
        stats.z_variance += (uint32_t)(dz * dz);
    }

    stats.x_variance /= IMU_MOTION_STATS_WINDOW;
    stats.y_variance /= IMU_MOTION_STATS_WINDOW;
    stats.z_variance /= IMU_MOTION_STATS_WINDOW;
    return stats;
}

imu_motion_config_t imu_motion_default_config(void)
{
    imu_motion_config_t config = {
        .x_abs_threshold = 384,
        .y_max_threshold = -64,
        .y_unstable_threshold = -724,
        .variance_threshold = 56U * 56U,
        .roll_threshold_degrees = -45,
    };
    return config;
}

void imu_motion_init(imu_motion_state_t *state,
                     const imu_motion_config_t *config)
{
    if (state == NULL)
    {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->config = config != NULL ? *config : imu_motion_default_config();
}

bool imu_motion_update(imu_motion_state_t *state,
                       const imu_motion_sample_t *sample,
                       imu_motion_result_t *result)
{
    if (state == NULL || sample == NULL || result == NULL)
    {
        return false;
    }

    state->head = (uint8_t)((state->head + 1U) % IMU_MOTION_HISTORY_SIZE);
    state->history[state->head] = *sample;
    if (state->count < IMU_MOTION_HISTORY_SIZE)
    {
        state->count++;
    }
    state->samples_seen++;

    memset(result, 0, sizeof(*result));
    result->reason = IMU_MOTION_REASON_WARMUP;
    result->samples_seen = state->samples_seen;

    if (state->count < IMU_MOTION_HISTORY_SIZE)
    {
        return false;
    }

    const imu_motion_stats_t current = imu_motion_compute_stats(state, 0);
    const imu_motion_stats_t previous =
        imu_motion_compute_stats(state,
                                 IMU_MOTION_HISTORY_SIZE -
                                     IMU_MOTION_STATS_WINDOW);

    result->x_mean = imu_motion_clamp_i16(current.x_mean);
    result->y_mean = imu_motion_clamp_i16(current.y_mean);
    result->z_mean = imu_motion_clamp_i16(current.z_mean);
    result->prev_y_mean = imu_motion_clamp_i16(previous.y_mean);
    result->prev_z_mean = imu_motion_clamp_i16(previous.z_mean);
    result->x_variance = current.x_variance;
    result->y_variance = current.y_variance;
    result->z_variance = current.z_variance;
    result->roll_delta_degrees =
        imu_motion_roll_delta_degrees(result->y_mean,
                                      result->z_mean,
                                      result->prev_y_mean,
                                      result->prev_z_mean);

    const imu_motion_config_t *cfg = &state->config;
    if (abs(result->x_mean) > cfg->x_abs_threshold)
    {
        result->reason = IMU_MOTION_REASON_X_TILT;
        return false;
    }

    if (result->y_variance > cfg->variance_threshold ||
        (result->y_mean < cfg->y_unstable_threshold &&
         result->z_variance > cfg->variance_threshold))
    {
        result->reason = IMU_MOTION_REASON_UNSTABLE;
        return false;
    }

    if (result->y_mean > cfg->y_max_threshold)
    {
        result->reason = IMU_MOTION_REASON_Y_ORIENTATION;
        return false;
    }

    if (result->roll_delta_degrees >= cfg->roll_threshold_degrees)
    {
        result->reason = IMU_MOTION_REASON_ROLL_TOO_SMALL;
        return false;
    }

    result->raise_detected = true;
    result->reason = IMU_MOTION_REASON_RAISE_DETECTED;
    return true;
}

const char *imu_motion_reason_text(imu_motion_reason_t reason)
{
    switch (reason)
    {
    case IMU_MOTION_REASON_WARMUP:
        return "WARMUP";
    case IMU_MOTION_REASON_X_TILT:
        return "X_TILT";
    case IMU_MOTION_REASON_UNSTABLE:
        return "UNSTABLE";
    case IMU_MOTION_REASON_Y_ORIENTATION:
        return "Y_ORIENTATION";
    case IMU_MOTION_REASON_ROLL_TOO_SMALL:
        return "ROLL_TOO_SMALL";
    case IMU_MOTION_REASON_RAISE_DETECTED:
        return "RAISE_DETECTED";
    default:
        return "UNKNOWN";
    }
}
