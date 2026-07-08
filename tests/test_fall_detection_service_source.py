import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import APP_ALERT_MANAGER_HEADER
from tests.main_paths import APP_ALERT_MANAGER_SOURCE
from tests.main_paths import FALL_DETECTION_SERVICE_HEADER
from tests.main_paths import FALL_DETECTION_SERVICE_SOURCE
from tests.main_paths import MAIN_CMAKE


class FallDetectionServiceSourceTests(unittest.TestCase):
    def test_main_build_and_startup_register_fall_detection_after_imu(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        app_main = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn("${CMAKE_CURRENT_LIST_DIR}/services/fall_detection_service.c", cmake)
        self.assertIn("fall_detection_inference", cmake)
        self.assertIn('#include "services/fall_detection_service.h"', app_main)
        self.assertIn("fall_detection_service_start()", app_main)
        self.assertIn("boot_stage: fall_detection_ready", app_main)
        self.assertGreater(
            app_main.index("fall_detection_service_start()"),
            app_main.index("imu_service_start()"),
        )

    def test_service_snapshot_is_log_only_runtime_contract(self) -> None:
        header = FALL_DETECTION_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("fall_detection_service_snapshot_t", header)
        self.assertIn("FALL_DETECTION_SERVICE_STATE_RUNNING", header)
        self.assertIn("bool model_ready", header)
        self.assertIn("bool self_test_passed", header)
        self.assertIn("uint32_t window_count", header)
        self.assertIn("uint32_t inference_count", header)
        self.assertIn("float adl_prob", header)
        self.assertIn("float fall_prob", header)
        self.assertIn("float threshold", header)
        self.assertIn("fall_detection_alert_state_t", header)
        self.assertIn("FALL_DETECTION_ALERT_STATE_IDLE", header)
        self.assertIn("FALL_DETECTION_ALERT_STATE_CONFIRMED", header)
        self.assertIn("uint32_t alert_sequence", header)
        self.assertIn("uint32_t clear_window_count", header)
        self.assertIn("uint32_t last_alert_window_sequence", header)
        self.assertIn("float last_alert_fall_prob", header)
        self.assertIn("esp_err_t last_alert_error", header)
        self.assertIn("fall_detection_service_get_snapshot", header)
        self.assertIn("does not read IMU hardware directly", header)
        self.assertIn("common alert manager and watch endpoint", header)
        self.assertIn("does not\n * play the dangerous-sound warning audio", header)

    def test_service_consumes_imu_windows_without_touching_driver_or_ui(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "esp_heap_caps.h"', source)
        self.assertIn('#include "freertos/idf_additions.h"', source)
        self.assertIn('#include "services/imu_service.h"', source)
        self.assertIn('#include "fall_model_runner.h"', source)
        self.assertIn("fall_detection_prepare_buffers", source)
        self.assertIn("uint8_t *window_queue_storage", source)
        self.assertIn("imu_service_accel_window_t *current_window", source)
        self.assertIn("float *model_input", source)
        self.assertIn("k_degrees_to_radians", source)
        self.assertIn("heap_caps_malloc(\n                k_window_queue_length * sizeof(imu_service_accel_window_t)", source)
        self.assertIn("heap_caps_calloc(\n                1U, sizeof(imu_service_accel_window_t)", source)
        self.assertIn("heap_caps_calloc(FALL_MODEL_INPUT_ELEMENTS", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)
        self.assertNotIn("uint8_t window_queue_storage[sizeof(imu_service_accel_window_t)]", source)
        self.assertNotIn("imu_service_accel_window_t current_window", source)
        self.assertNotIn("float model_input[FALL_MODEL_INPUT_ELEMENTS]", source)
        self.assertIn("xQueueCreateStatic", source)
        self.assertIn("sizeof(imu_service_accel_window_t)", source)
        self.assertIn("xQueueReceive", source)
        self.assertIn("xTaskCreateWithCaps(", source)
        self.assertIn("MALLOC_CAP_SPIRAM", source)
        self.assertIn("imu_service_set_window_queue(queue)", source)
        self.assertIn("fall_model_runner_create", source)
        self.assertIn("fall_model_runner_self_test", source)
        self.assertIn("fall_model_runner_run", source)
        self.assertIn("tcn_v1_rf5s_6ch_5s", source)
        self.assertIn("跌倒表 |", source)
        self.assertIn("事件窗口契约不匹配", source)
        self.assertIn("window->trigger_frame_index !=\n                IMU_SERVICE_EVENT_PRE_FRAMES", source)
        self.assertIn('#include "features/alerts/app_alert_manager.h"', source)
        self.assertIn('#include "services/watch_endpoint_service.h"', source)
        self.assertNotIn("qmi8658c_", source)
        self.assertNotIn("imu_sensor_read", source)
        self.assertNotIn("lv_", source)
        self.assertNotIn("display_alert", source)
        self.assertNotIn("haptic_alert", source)

    def test_service_applies_v1_6ch_input_with_gyro_rad_per_sec(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("修正后右手系板级物理轴", source)
        self.assertIn("禁止再套旧 raw chip", source)
        self.assertIn("model accX  =  imu accX", source)
        self.assertIn("model accZ  = -imu accZ", source)
        self.assertIn("model gyroX =  imu gyroX", source)
        self.assertIn("model gyroZ = -imu gyroZ", source)
        self.assertIn("for (uint16_t frame = 0; frame < IMU_SERVICE_WINDOW_FRAME_COUNT; ++frame)", source)
        self.assertIn("const uint16_t base = frame * 6U;", source)
        self.assertIn("input[base + 0U] =  window->accel[frame].x;", source)
        self.assertIn("input[base + 2U] = -window->accel[frame].z;", source)
        self.assertIn("input[base + 3U] =  window->gyro[frame].x * k_degrees_to_radians;", source)
        self.assertIn("input[base + 5U] = -window->gyro[frame].z * k_degrees_to_radians;", source)

    def test_event_window_confirms_and_local_alert_auto_clears_after_five_seconds(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("static const float k_fall_clear_threshold = 0.50f;", source)
        self.assertIn("static const uint32_t k_fall_clear_window_count = 2U;", source)
        self.assertIn("k_alert_receive_timeout_ticks = pdMS_TO_TICKS(1000)", source)
        self.assertIn("k_fall_alert_auto_clear_us = 5LL * 1000LL * 1000LL", source)
        self.assertIn("last_alert_time_us", source)
        self.assertIn("fall_detection_update_alert_timeout", source)
        self.assertIn("esp_timer_get_time()", source)
        self.assertNotIn("fall_detection_should_run_inference", source)
        self.assertNotIn("fall_detection_is_alert_confirmed_active", source)
        self.assertIn("window->trigger_flags != 0U", source)
        self.assertIn("非事件窗口已拒绝:", source)
        self.assertNotIn("定期窗口已忽略:", source)
        self.assertNotIn("idle_no_event", source)
        self.assertIn("is_event_window &&", source)
        self.assertIn("result->fall_prob >= FALL_MODEL_THRESHOLD_DEFAULT", source)
        self.assertNotIn("danger_window_count", source)
        self.assertIn("FALL_DETECTION_ALERT_STATE_IDLE", source)
        self.assertIn("FALL_DETECTION_ALERT_STATE_CONFIRMED", source)
        self.assertIn("s_fall_detection.last_alert_time_us = window->end_time_us;", source)
        self.assertIn("(now_us - s_fall_detection.last_alert_time_us) >=\n            k_fall_alert_auto_clear_us", source)
        self.assertIn("result->fall_prob < k_fall_clear_threshold", source)
        self.assertIn("clear_window_count >=\n            k_fall_clear_window_count", source)
        self.assertIn("跌倒告警已确认:", source)
        self.assertIn("跌倒告警已清除:", source)

    def test_confirmed_fall_raises_local_alert_and_uploads_danger_alert_once(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("fall_detection_raise_local_alert", source)
        self.assertIn("fall_detection_post_app_alert", source)
        self.assertIn("APP_ALERT_SOURCE_FALL_DETECTION", source)
        self.assertIn("APP_ALERT_LABEL_FALL", source)
        self.assertIn("app_alert_manager_raise(&request)", source)
        self.assertIn("watch_endpoint_service_post_danger_alert(&alert)", source)
        self.assertIn('static const char *k_fall_app_danger_type = "fall";', source)
        self.assertIn('static const char *k_fall_app_message = "检测到跌倒";', source)
        self.assertIn("跌倒App上传已入队:", source)
        self.assertIn("跌倒App上传失败:", source)
        self.assertIn("跌倒危险语言播发已跳过:", source)
        self.assertIn("本地跌倒告警失败:", source)
        self.assertIn("app_alert_manager_clear(APP_ALERT_SOURCE_FALL_DETECTION)", source)
        self.assertNotIn("app_upload_pending", source)
        self.assertNotIn("fall_detection_retry_pending_app_upload", source)
        self.assertNotIn("fall_app_upload_retry_failed:", source)

    def test_app_alert_manager_declares_fall_source_and_label(self) -> None:
        header = APP_ALERT_MANAGER_HEADER.read_text(encoding="utf-8")
        source = APP_ALERT_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("APP_ALERT_SOURCE_FALL_DETECTION", header)
        self.assertIn("APP_ALERT_LABEL_FALL", header)
        self.assertIn('return "跌倒";', source)
        self.assertIn(
            "request->source != APP_ALERT_SOURCE_FALL_DETECTION",
            source,
        )
        self.assertIn("跳过危险提示音", source)


if __name__ == "__main__":
    unittest.main()
