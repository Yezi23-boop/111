import unittest

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
        self.assertIn("${CMAKE_CURRENT_LIST_DIR}/services/imu_service.c", cmake)
        self.assertIn("${CMAKE_CURRENT_LIST_DIR}/app/board_imu.c", cmake)
        self.assertIn("qmi8658c", cmake)
        self.assertIn("imu_motion", cmake)

    def test_board_imu_config_centralizes_current_board_facts(self) -> None:
        self.assertTrue(BOARD_IMU_HEADER.exists(), "main/app/board_imu.h should exist")
        self.assertTrue(BOARD_IMU_SOURCE.exists(), "main/app/board_imu.c should exist")
        header = BOARD_IMU_HEADER.read_text(encoding="utf-8")
        source = BOARD_IMU_SOURCE.read_text(encoding="utf-8")

        self.assertIn("board_imu_config_t", header)
        self.assertIn("board_imu_get_config", header)
        self.assertIn("qmi_i2c_addr_7bit", header)
        self.assertIn("qmi_int1_gpio", header)
        self.assertIn("motion_sample_count", header)
        self.assertIn("BOARD_IMU_FACE_AXIS_NEG_Z", header)
        self.assertIn(".qmi_i2c_addr_7bit = 0x6B", source)
        self.assertIn(".qmi_int1_gpio = GPIO_NUM_21", source)
        self.assertIn(".face_axis = BOARD_IMU_FACE_AXIS_NEG_Z", source)
        self.assertIn(".face_axis_threshold_raw = -6500", source)
        self.assertIn(".motion_sample_count = 16U", source)
        self.assertNotIn("ae_motion_on_demand", source + header)

    def test_app_main_starts_imu_service_as_deferred_log_only_service(self) -> None:
        self.assertTrue(APP_MAIN_SOURCE.exists(), "main/app/app_main.c should exist")
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "services/imu_service.h"', source)
        self.assertIn("imu_service_start()", source)
        self.assertIn("boot_stage: imu_service_ready", source)
        self.assertIn("QMI8658C 内部 WoM/INT1 第一版只做事件日志验证", source)

        self.assertGreater(source.index("official_chat_service_init()"), source.index("network_service_start()"))
        self.assertGreater(source.index("imu_service_start()"), source.index("official_chat_service_init()"))

    def test_header_exposes_snapshot_getter_contract(self) -> None:
        self.assertTrue(IMU_SERVICE_HEADER.exists(), "main/services/imu_service.h should exist")
        header = IMU_SERVICE_HEADER.read_text(encoding="utf-8")
        self.assertIn("imu_service_snapshot_t", header)
        self.assertIn("IMU_SERVICE_STATE_PROBING", header)
        self.assertIn("IMU_SERVICE_STATE_RUNNING", header)
        self.assertIn("uint32_t wom_irq_count", header)
        self.assertIn("uint32_t wom_poll_event_count", header)
        self.assertIn("uint32_t wom_event_count", header)
        self.assertIn("bool int1_path_usable", header)
        self.assertIn("bool poll_fallback_active", header)
        self.assertIn("imu_service_raise_reason_t", header)
        self.assertIn("uint32_t motion_window_count", header)
        self.assertIn("uint32_t raise_detected_count", header)
        self.assertIn("bool last_raise_detected", header)
        self.assertIn("bool last_motion_pass", header)
        self.assertIn("bool last_final_pose_pass", header)
        self.assertIn("last_final_accel_norm_mg", header)
        self.assertIn("last_final_accel_stability_mg", header)
        self.assertIn("qmi8658c_raw_sample_t last_final_raw", header)
        self.assertIn("last_motion_roll_delta_degrees", header)
        self.assertIn("qmi8658c_raw_sample_t last_raw", header)
        self.assertIn("qmi8658c_status1_t last_status1", header)
        self.assertIn("esp_err_t imu_service_get_snapshot(imu_service_snapshot_t *out);", header)
        self.assertIn("imu_service_raise_reason_text", header)
        self.assertIn("getter 只复制 service task 已缓存的事实", header)

    def test_service_uses_qmi_wom_int1_and_task_notification(self) -> None:
        self.assertTrue(IMU_SERVICE_SOURCE.exists(), "main/services/imu_service.c should exist")
        source = IMU_SERVICE_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "app/board_imu.h"', source)
        self.assertIn("board_imu_get_config()", source)
        self.assertIn("qmi8658c_init_with_bus_config(&bus_config)", source)
        self.assertIn("qmi8658c_probe(&identity)", source)
        self.assertIn("qmi8658c_configure_wake_on_motion(&config)", source)
        self.assertIn("qmi8658c_read_status1(&status)", source)
        self.assertIn("qmi8658c_read_statusint(&statusint)", source)
        self.assertIn("qmi8658c_read_raw(&raw)", source)
        self.assertIn("qmi8658c_disable_wake_on_motion()", source)
        self.assertIn("qmi8658c_configure(&config)", source)
        self.assertIn("imu_motion_update(&motion_state", source)
        self.assertNotIn("qmi8658c_request_motion_on_demand()", source)
        self.assertNotIn("qmi8658c_read_dq(&sample)", source)
        self.assertIn("imu_service_read_final_pose(result, event_id", source)
        self.assertIn("board_config->qmi_int1_gpio", source)
        self.assertIn("gpio_isr_handler_add", source)
        self.assertIn("vTaskNotifyGiveFromISR", source)
        self.assertIn("ulTaskNotifyTake", source)
        self.assertIn("k_wom_poll_fallback_ticks", source)
        self.assertIn("int1_path_unusable:", source)
        self.assertIn("gpio_intr_disable", source)
        self.assertIn("imu_service_handle_wom_check(false)", source)
        self.assertIn(".interrupt = QMI8658C_WOM_INTERRUPT_INT1_INITIAL_LOW", source)
        self.assertIn("wom_poll_recovery:", source)
        self.assertIn("imu_service_handle_wom_check(true)", source)
        self.assertIn("imu_service_probe_and_configure_wom()", source)
        self.assertNotIn("static const gpio_num_t k_qmi_int1_gpio", source)

    def test_service_logs_raw_motion_csv_and_raise_result_reasons(self) -> None:
        source = IMU_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("imu_csv: source=raw_motion", source)
        self.assertIn("event_id=%u", source)
        self.assertIn("trigger=%s", source)
        self.assertIn("motion_reason=%s", source)
        self.assertIn("raw_gyro=(%d,%d,%d)", source)
        self.assertIn("raise_config:", source)
        self.assertIn("next_wom_event_id", source)
        self.assertIn("final_pose: event_id=%u source=%s pass=%d", source)
        self.assertIn("raise_result: event_id=%u source=%s raise_detected=%d motion_pass=%d final_pose_pass=%d", source)
        self.assertIn("IMU_SERVICE_RAISE_REASON_PASS", source)
        self.assertIn("IMU_SERVICE_RAISE_REASON_MOTION_REJECT", source)
        self.assertIn("IMU_SERVICE_RAISE_REASON_FINAL_NORM", source)
        self.assertIn("IMU_SERVICE_RAISE_REASON_FINAL_UNSTABLE", source)
        self.assertIn("IMU_SERVICE_RAISE_REASON_FINAL_POSE", source)
        self.assertIn("board_config->final_norm_min_mg", source)
        self.assertIn("board_config->final_norm_max_mg", source)
        self.assertIn("board_config->face_axis_threshold_raw", source)
        self.assertIn("board_config->motion_sample_count", source)
        self.assertIn("mode=raw_6axis", source)
        self.assertNotIn("qmi8658c_request_motion_on_demand", source)
        self.assertIn("result->motion_pass && result->final_pose_pass", source)
        self.assertIn("action=log_only", source)

    def test_service_is_log_only_and_does_not_touch_ui_or_sleep(self) -> None:
        source = IMU_SERVICE_SOURCE.read_text(encoding="utf-8")
        self.assertIn("wom_event:", source)
        self.assertIn("action=log_only", source)
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
