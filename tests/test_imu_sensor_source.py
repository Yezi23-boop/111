import unittest

from tests.main_paths import IMU_SENSOR_CMAKE
from tests.main_paths import IMU_SENSOR_HEADER
from tests.main_paths import IMU_SENSOR_SOURCE


class ImuSensorSourceTests(unittest.TestCase):
    def test_component_wraps_qmi8658c_without_gpio_or_tasks(self) -> None:
        self.assertTrue(IMU_SENSOR_CMAKE.exists(), "components/imu_sensor/CMakeLists.txt should exist")
        self.assertTrue(IMU_SENSOR_HEADER.exists(), "components/imu_sensor/include/imu_sensor.h should exist")
        self.assertTrue(IMU_SENSOR_SOURCE.exists(), "components/imu_sensor/imu_sensor.c should exist")

        cmake = IMU_SENSOR_CMAKE.read_text(encoding="utf-8")
        source = IMU_SENSOR_SOURCE.read_text(encoding="utf-8")

        self.assertIn('SRCS "imu_sensor.c"', cmake)
        self.assertIn('INCLUDE_DIRS "include"', cmake)
        self.assertIn("REQUIRES qmi8658c", cmake)
        self.assertIn('#include "qmi8658c.h"', source)
        self.assertIn("qmi8658c_init_bus(&qmi_bus)", source)
        self.assertIn("qmi8658c_probe(&qmi_info)", source)
        self.assertIn("qmi8658c_config(&qmi_config)", source)
        self.assertIn("qmi8658c_read(&qmi_sample)", source)
        self.assertNotIn("gpio_", source)
        self.assertNotIn("xTaskCreate", source)
        self.assertNotIn("ulTaskNotifyTake", source)

    def test_header_exposes_generic_physical_six_axis_contract(self) -> None:
        header = IMU_SENSOR_HEADER.read_text(encoding="utf-8")

        self.assertIn("imu_sensor_bus_t", header)
        self.assertIn("imu_sensor_info_t", header)
        self.assertIn("imu_sensor_accel_t", header)
        self.assertIn("imu_sensor_gyro_t", header)
        self.assertIn("imu_sensor_sample_t", header)
        self.assertIn("单位 `m/s^2`", header)
        self.assertIn("单位 `deg/s`", header)
        self.assertIn("IMU_SENSOR_INT_SOURCE_DISABLED", header)
        self.assertIn("esp_err_t imu_sensor_init(const imu_sensor_bus_t *bus);", header)
        self.assertIn("esp_err_t imu_sensor_probe(imu_sensor_info_t *info);", header)
        self.assertIn("esp_err_t imu_sensor_config(const imu_sensor_config_t *config);", header)
        self.assertIn("esp_err_t imu_sensor_read(imu_sensor_sample_t *sample);", header)
        self.assertNotIn("qmi8658c_", header)
        self.assertNotIn("gpio_num_t", header)


if __name__ == "__main__":
    unittest.main()

