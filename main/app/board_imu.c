#include "app/board_imu.h"

static const board_imu_config_t k_board_imu_config = {
    .qmi_i2c_addr_7bit = 0x6B,
    .qmi_int1_gpio = GPIO_NUM_21,
    .wom_threshold_mg = 120U,
    .wom_blanking_samples = 16U,
    .wom_accel_fs_code = 0U,
    .wom_accel_odr_code = 3U,
    .motion_sample_count = 16U,
    .motion_sample_period_ms = 40U,
    .motion_accel_fs_code = 0U,
    .motion_accel_odr_code = 3U,
    .motion_gyro_fs_code = 4U,
    .motion_gyro_odr_code = 3U,
    .accel_lsb_per_g = 16384,
    .final_pose_sample_count = 5U,
    .final_norm_min_mg = 750,
    .final_norm_max_mg = 1250,
    .final_stability_max_mg = 180,
    .face_axis = BOARD_IMU_FACE_AXIS_NEG_Z,
    .face_axis_threshold_raw = -6500,
};

const board_imu_config_t *board_imu_get_config(void)
{
    return &k_board_imu_config;
}
