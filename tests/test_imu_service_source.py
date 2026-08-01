import unittest

from tests.main_cmake_contract import assert_main_source_globbed
from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import BOARD_IMU_HEADER
from tests.main_paths import BOARD_IMU_SOURCE
from tests.main_paths import IMU_SERVICE_HEADER
from tests.main_paths import IMU_SERVICE_SOURCE
from tests.main_paths import MAIN_CMAKE


class ImuServiceSourceTests(unittest.TestCase):
    def test_main_cmake_registers_imu_service_and_dependencies(self) -> None:
        self.assertTrue(MAIN_CMAKE.exists(), "main/CMakeLists.txt should exist")
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        assert_main_source_globbed(self, "services/sensors/imu_service.c")
        assert_main_source_globbed(self, "app/board_imu.c")
        self.assertIn("imu_sensor", cmake)
        self.assertNotIn("qmi8658c", cmake)
        self.assertNotIn("imu_motion", cmake)

    def test_board_imu_config_centralizes_only_current_board_facts(self) -> None:
        self.assertTrue(BOARD_IMU_HEADER.exists(), "main/app/board_imu.h should exist")
        self.assertTrue(BOARD_IMU_SOURCE.exists(), "main/app/board_imu.c should exist")
        header = BOARD_IMU_HEADER.read_text(encoding="utf-8")
        source = BOARD_IMU_SOURCE.read_text(encoding="utf-8")

        self.assertIn("board_imu_config_t", header)
        self.assertIn("board_imu_get_config", header)
        self.assertIn("qmi_i2c_addr_7bit", header)
        self.assertIn("qmi_int1_gpio", header)
        self.assertIn("BOARD_IMU_FACE_AXIS_NEG_Z", header)
        self.assertIn("表盘朝上 / 正面朝上 -> QMI8658C `-Z`", header)
        self.assertIn("表背朝上 / 背面朝上 -> QMI8658C `+Z`", header)
        self.assertIn("手表顶部朝上        -> QMI8658C `+X`", header)
        self.assertIn("手表底部朝上        -> QMI8658C `-X`", header)
        self.assertIn("手表左侧朝上        -> QMI8658C `-Y`", header)
        self.assertIn("手表右侧朝上        -> QMI8658C `+Y`", header)
        self.assertIn(".qmi_i2c_addr_7bit = 0x6B", source)
        self.assertIn(".qmi_int1_gpio = GPIO_NUM_21", source)
        self.assertIn(".face_axis = BOARD_IMU_FACE_AXIS_NEG_Z", source)
        self.assertNotIn("wom_threshold_mg", header + source)
        self.assertNotIn("motion_sample_count", header + source)
        self.assertNotIn("motion_sample_period_ms", header + source)
        self.assertNotIn("motion_accel_fs_code", header + source)
        self.assertNotIn("motion_gyro_fs_code", header + source)
        self.assertNotIn("final_norm_min_mg", header + source)
        self.assertNotIn("final_stability_max_mg", header + source)
        self.assertNotIn("face_axis_threshold_mg", header + source)
        self.assertNotIn("accel_lsb_per_g", header + source)
        self.assertNotIn("ae_motion_on_demand", source + header)

    def test_app_main_keeps_imu_service_disabled_by_default(self) -> None:
        self.assertTrue(APP_MAIN_SOURCE.exists(), "main/app/app_main.c should exist")
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "services/sensors/imu_service.h"', source)
        self.assertIn("imu_service_start()", source)
        self.assertIn("boot_stage: imu_service_disabled_by_default", source)
        self.assertIn("默认关闭 IMU/Fall 后台链路", source)
        self.assertIn("kMotionServicesEnabledByDefault", source)
        self.assertIn("fall_detection_service_destroy()", source)
        self.assertIn("imu_service_destroy()", source)
        self.assertNotIn("#if 0", source)
        imu_start_index = source.index("imu_service_start()")
        nearby_source = source[imu_start_index - 120:imu_start_index + 260]
        self.assertIn("kMotionServicesEnabledByDefault", nearby_source)
        self.assertGreater(source.index("official_chat_service_init()"), source.index("network_service_start()"))
        self.assertGreater(imu_start_index, source.index("official_chat_service_init()"))

    def test_header_exposes_config_snapshot_getter_contract(self) -> None:
        self.assertTrue(IMU_SERVICE_HEADER.exists(), "main/services/sensors/imu_service.h should exist")
        header = IMU_SERVICE_HEADER.read_text(encoding="utf-8")
        self.assertIn("#define IMU_SERVICE_SAMPLE_RATE_HZ 50U", header)
        self.assertIn("#define IMU_SERVICE_EVENT_PRE_FRAMES 50U", header)
        self.assertIn("#define IMU_SERVICE_EVENT_POST_FRAMES 250U", header)
        self.assertIn("#define IMU_SERVICE_WINDOW_FRAME_COUNT \\", header)
        self.assertIn("6 秒 / 300 帧事件窗口", header)
        self.assertIn("2 秒模型子窗口", header)
        self.assertIn("事件后 post-check", header)
        self.assertIn("imu_service_snapshot_t", header)
        self.assertIn("imu_service_accel_window_t", header)
        self.assertIn("QueueHandle_t queue", header)
        self.assertIn("uint32_t trigger_sample_count", header)
        self.assertIn("uint16_t trigger_frame_index", header)
        self.assertIn("uint32_t trigger_flags", header)
        self.assertIn("float trigger_acc_norm_mps2", header)
        self.assertIn("float trigger_gyro_norm_radps", header)
        self.assertIn("float trigger_jerk_mps2_per_frame", header)
        self.assertIn("imu_sensor_accel_t accel[IMU_SERVICE_WINDOW_FRAME_COUNT]", header)
        self.assertIn("imu_sensor_gyro_t gyro[IMU_SERVICE_WINDOW_FRAME_COUNT]", header)
        self.assertIn("当前板级右手系", header)
        self.assertIn("`+X` 朝手表顶部", header)
        self.assertIn("`+Y` 朝手表右侧", header)
        self.assertIn("`+Z`\n     * 朝表背/向下", header)
        self.assertIn("Fall V1 输入层\n     * 对 Z 轴取反", header)
        self.assertIn("gyro 从 `deg/s` 转换为 `rad/s`", header)
        self.assertIn("IMU_SERVICE_STATE_PROBING", header)
        self.assertIn("IMU_SERVICE_STATE_RUNNING", header)
        self.assertIn("bool configured", header)
        self.assertIn("uint8_t accel_fs", header)
        self.assertIn("uint8_t gyro_fs", header)
        self.assertIn("bool accel_enabled", header)
        self.assertIn("bool gyro_enabled", header)
        self.assertIn("bool int1_isr_installed", header)
        self.assertIn("int int1_gpio", header)
        self.assertIn("uint32_t int1_irq_count", header)
        self.assertIn("bool sampling_active", header)
        self.assertIn("bool sample_window_ready", header)
        self.assertIn("uint16_t sample_rate_hz", header)
        self.assertIn("uint16_t window_frame_count", header)
        self.assertIn("uint32_t sample_count", header)
        self.assertIn("uint32_t sample_error_count", header)
        self.assertIn("last_sample_time_us", header)
        self.assertIn("last_sample_interval_us", header)
        self.assertIn("configured_time_us", header)
        self.assertIn("esp_err_t imu_service_get_snapshot(imu_service_snapshot_t *out);", header)
        self.assertIn("esp_err_t imu_service_destroy(void);", header)
        self.assertIn("esp_err_t imu_service_set_window_queue(QueueHandle_t queue);", header)
        self.assertIn("getter 只复制 service task 已缓存的事实", header)
        self.assertIn("xQueueOverwrite()", header)
        self.assertNotIn("wom_", header)
        self.assertNotIn("last_wom", header)
        self.assertNotIn("raise_reason", header)
        self.assertNotIn("motion_window", header)
        self.assertNotIn("qmi8658c_wom_status_t", header)

    def test_service_uses_imu_sensor_and_owns_gpio21_isr(self) -> None:
        self.assertTrue(IMU_SERVICE_SOURCE.exists(), "main/services/sensors/imu_service.c should exist")
        source = IMU_SERVICE_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "app/board_imu.h"', source)
        self.assertIn('#include "esp_heap_caps.h"', source)
        self.assertIn('#include "imu_sensor.h"', source)
        self.assertNotIn('#include "qmi8658c.h"', source)
        self.assertIn("board_imu_get_config()", source)
        self.assertIn("imu_service_profile_t", source)
        self.assertIn("k_imu_service_profile", source)
        self.assertIn(".accel_fs_code = 3U", source)
        self.assertIn(".gyro_fs_code = 7U", source)
        self.assertIn(".accel_enable = true", source)
        self.assertIn(".gyro_enable = true", source)
        self.assertIn("imu_sensor_init(&bus_config)", source)
        self.assertIn("imu_sensor_probe(&identity)", source)
        self.assertIn("imu_sensor_config(&config)", source)
        self.assertIn(".int1_source = IMU_SENSOR_INT_SOURCE_DISABLED", source)
        self.assertIn(".int2_source = IMU_SENSOR_INT_SOURCE_DISABLED", source)
        self.assertIn("configured: accel_fs=%u", source)
        self.assertIn("gpio_isr_handler_add", source)
        self.assertIn("vTaskNotifyGiveFromISR", source)
        self.assertIn("ulTaskNotifyTake", source)
        self.assertIn("gpio_intr_enable", source)
        self.assertIn("INT1中断:", source)
        self.assertIn("k_sample_period_ticks", source)
        self.assertIn("IMU_SERVICE_SAMPLE_RATE_HZ", source)
        self.assertIn("IMU_SERVICE_WINDOW_FRAME_COUNT", source)
        self.assertIn("imu_service_ring_sample_t *sample_ring", source)
        self.assertIn("imu_service_event_trigger_t", source)
        self.assertIn("k_event_acc_norm_high_mps2 = 25.0f", source)
        self.assertIn("k_event_gyro_norm_high_radps = 5.0f", source)
        self.assertIn("k_event_jerk_high_mps2_per_frame = 10.0f", source)
        self.assertIn("k_event_gyro_jerk_min_mps2_per_frame = 5.0f", source)
        self.assertIn("k_event_jerk_acc_norm_min_mps2 = 16.0f", source)
        self.assertIn("k_event_jerk_gyro_norm_min_radps = 3.0f", source)
        self.assertIn("k_event_cooldown_frames =\n    IMU_SERVICE_WINDOW_FRAME_COUNT", source)
        self.assertIn(
            "jerk_mps2_per_frame > k_event_jerk_high_mps2_per_frame &&",
            source,
        )
        self.assertIn(
            "(acc_norm_mps2 > k_event_jerk_acc_norm_min_mps2 ||",
            source,
        )
        self.assertIn(
            "gyro_norm_radps > k_event_jerk_gyro_norm_min_radps)",
            source,
        )
        self.assertIn("imu_service_accel_window_t *publish_window", source)
        self.assertIn("imu_service_prepare_buffers", source)
        self.assertIn("heap_caps_calloc(\n                IMU_SERVICE_WINDOW_FRAME_COUNT", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)
        self.assertIn("window_queue", source)
        self.assertIn("imu_service_update_event_trigger", source)
        self.assertIn("imu_service_build_event_window", source)
        self.assertIn("imu_service_publish_window_if_ready(sample_count)", source)
        self.assertIn("xQueueOverwrite(window_queue, s_imu_service.publish_window)", source)
        self.assertIn("事件触发表 |", source)
        self.assertIn("事件窗口表 |", source)
        self.assertNotIn("imu_service_build_periodic_window", source)
        self.assertNotIn("last_periodic_publish_sample_count", source)
        self.assertNotIn("定期窗口表", source)
        self.assertIn("vTaskDelayUntil(&last_wake_tick, k_sample_period_ticks)", source)
        self.assertIn("imu_sensor_read(&sample)", source)
        self.assertIn("k_sample_log_interval = IMU_SERVICE_SAMPLE_RATE_HZ * 2U", source)
        self.assertIn("采样表 |", source)
        self.assertIn("ESP_LOGD(TAG,\n                         \"采样表 |", source)
        self.assertIn("sample_window_ready", source)
        self.assertIn("imu_service_release_runtime_resources", source)
        self.assertIn("vTaskDelete(NULL)", source)
        self.assertIn("gpio_isr_handler_remove", source)
        self.assertIn("destroy_requested", source)
        self.assertNotIn("qmi8658c_", source)
        self.assertNotIn("qmi8658c_enable_wom", source)
        self.assertNotIn("qmi8658c_disable_wom", source)
        self.assertNotIn("qmi8658c_read_wom", source)
        self.assertNotIn("qmi8658c_read_int", source)
        self.assertNotIn("qmi8658c_read(&", source)
        self.assertNotIn("imu_motion_update", source)
        self.assertNotIn("imu_csv:", source)
        self.assertNotIn("raise_result:", source)
        self.assertNotIn("wom_event:", source)

    def test_service_sampling_path_does_not_touch_ui_or_sleep(self) -> None:
        source = IMU_SERVICE_SOURCE.read_text(encoding="utf-8")
        self.assertIn("已启动: 50Hz采样", source)
        forbidden_tokens = (
            "ui_refresh_policy_notify_activity",
            "lv_",
            "esp_light_sleep_start",
            "esp_deep_sleep_start",
            "esp_sleep_enable_timer_wakeup",
            "esp_sleep_enable_ext0_wakeup",
            "esp_sleep_enable_ext1_wakeup",
            "esp_sleep_enable_gpio_wakeup",
        )
        for token in forbidden_tokens:
            self.assertNotIn(token, source)


if __name__ == "__main__":
    unittest.main()
