# IMU 软件运动识别算法组件
# - 只维护短历史和姿态判定，不访问 I2C、GPIO、LVGL 或后台任务
idf_component_register(
    SRCS "imu_motion.c"
    INCLUDE_DIRS "include"
)
