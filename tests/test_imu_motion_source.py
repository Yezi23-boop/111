import unittest

from tests.main_paths import IMU_MOTION_CMAKE
from tests.main_paths import IMU_MOTION_HEADER
from tests.main_paths import IMU_MOTION_SOURCE


class ImuMotionSourceTests(unittest.TestCase):
    def test_component_is_pure_algorithm_component(self) -> None:
        self.assertTrue(IMU_MOTION_CMAKE.exists(), "components/imu_motion/CMakeLists.txt should exist")
        cmake = IMU_MOTION_CMAKE.read_text(encoding="utf-8")
        self.assertIn('SRCS "imu_motion.c"', cmake)
        self.assertIn('INCLUDE_DIRS "include"', cmake)
        self.assertNotIn("qmi8658c", cmake)
        self.assertNotIn("driver", cmake)
        self.assertNotIn("freertos", cmake.lower())

    def test_public_api_exposes_state_config_and_result(self) -> None:
        self.assertTrue(IMU_MOTION_HEADER.exists(), "components/imu_motion/include/imu_motion.h should exist")
        header = IMU_MOTION_HEADER.read_text(encoding="utf-8")
        self.assertIn("#define IMU_MOTION_HISTORY_SIZE 8U", header)
        self.assertIn("#define IMU_MOTION_STATS_WINDOW 2U", header)
        self.assertIn("imu_motion_config_t", header)
        self.assertIn("imu_motion_state_t", header)
        self.assertIn("imu_motion_result_t", header)
        self.assertIn("imu_motion_default_config(void)", header)
        self.assertIn("imu_motion_update(imu_motion_state_t *state", header)
        self.assertIn("imu_motion_reason_text", header)

    def test_default_thresholds_follow_infinitime_style_units(self) -> None:
        self.assertTrue(IMU_MOTION_SOURCE.exists(), "components/imu_motion/imu_motion.c should exist")
        source = IMU_MOTION_SOURCE.read_text(encoding="utf-8")
        self.assertIn(".x_abs_threshold = 384", source)
        self.assertIn(".y_max_threshold = -64", source)
        self.assertIn(".y_unstable_threshold = -724", source)
        self.assertIn(".variance_threshold = 56U * 56U", source)
        self.assertIn(".roll_threshold_degrees = -45", source)

    def test_algorithm_uses_history_stats_and_roll_delta_without_platform_io(self) -> None:
        source = IMU_MOTION_SOURCE.read_text(encoding="utf-8")
        self.assertIn("imu_motion_compute_stats", source)
        self.assertIn("imu_motion_roll_delta_degrees", source)
        self.assertIn("atan2f", source)
        self.assertIn("IMU_MOTION_REASON_RAISE_DETECTED", source)

        forbidden_tokens = (
            "qmi8658c_",
            "i2c_",
            "gpio_",
            "lv_",
            "xTaskCreate",
            "vTaskDelay",
            "esp_light_sleep_start",
            "esp_deep_sleep_start",
        )
        for token in forbidden_tokens:
            self.assertNotIn(token, source)


if __name__ == "__main__":
    unittest.main()
