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
        self.assertIn("fall_window_result:", source)
        self.assertIn('#include "features/alerts/app_alert_manager.h"', source)
        self.assertIn('#include "services/watch_endpoint_service.h"', source)
        self.assertNotIn("qmi8658c_", source)
        self.assertNotIn("imu_sensor_read", source)
        self.assertNotIn("lv_", source)
        self.assertNotIn("display_alert", source)
        self.assertNotIn("haptic_alert", source)

    def test_service_applies_board_axis_remap_and_frame_interleaved_flatten(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("model +X = watch right / USB side = -chip X", source)
        self.assertIn("model +Y = watch top = +chip Y", source)
        self.assertIn("model +Z = watch front/dial = -chip Z", source)
        self.assertIn("input[(frame * 3U) + 0U] = -window->accel[frame].x;", source)
        self.assertIn("input[(frame * 3U) + 1U] = window->accel[frame].y;", source)
        self.assertIn("input[(frame * 3U) + 2U] = -window->accel[frame].z;", source)
        self.assertNotIn("window->accel[frame].gyro", source)

    def test_single_window_fall_confirms_and_three_low_windows_clear(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("static const float k_fall_clear_threshold = 0.50f;", source)
        self.assertIn("static const uint32_t k_fall_clear_window_count = 3U;", source)
        self.assertIn("result->fall_prob >= FALL_MODEL_THRESHOLD_DEFAULT", source)
        self.assertNotIn("danger_window_count", source)
        self.assertIn("FALL_DETECTION_ALERT_STATE_IDLE", source)
        self.assertIn("FALL_DETECTION_ALERT_STATE_CONFIRMED", source)
        self.assertIn("result->fall_prob < k_fall_clear_threshold", source)
        self.assertIn("clear_window_count >=\n            k_fall_clear_window_count", source)
        self.assertIn("fall_alert_confirmed:", source)
        self.assertIn("fall_alert_cleared:", source)

    def test_confirmed_fall_raises_local_alert_and_app_upload_once(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("fall_detection_raise_local_alert", source)
        self.assertIn("APP_ALERT_SOURCE_FALL_DETECTION", source)
        self.assertIn("APP_ALERT_LABEL_FALL", source)
        self.assertIn("app_alert_manager_raise(&request)", source)
        self.assertIn("watch_endpoint_service_post_danger_alert(&alert)", source)
        self.assertIn('static const char *k_fall_app_danger_type = "fall";', source)
        self.assertIn('static const char *k_fall_app_message = "检测到跌倒";', source)
        self.assertIn("fall_app_upload_queued:", source)
        self.assertIn("fall_app_upload_failed:", source)
        self.assertIn("fall_local_alert_failed:", source)
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


if __name__ == "__main__":
    unittest.main()
