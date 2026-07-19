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
        self.assertIn("FALL_DETECTION_SERVICE_STATE_STOPPING", header)
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
        self.assertIn("fall_detection_service_destroy", header)
        self.assertIn("does not read IMU hardware directly", header)
        self.assertIn("common alert manager and watch endpoint", header)
        self.assertIn("does not\n * play the dangerous-sound warning audio", header)

    def test_service_consumes_imu_windows_without_touching_driver_or_ui(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "esp_heap_caps.h"', source)
        self.assertIn('#include "freertos/idf_additions.h"', source)
        self.assertIn('#include "services/sensors/imu_service.h"', source)
        self.assertIn('#include "fall_model_runner.h"', source)
        self.assertIn("fall_detection_prepare_buffers", source)
        self.assertIn("fall_detection_window_contract_error_t", source)
        self.assertIn("fall_detection_validate_window_contract", source)
        self.assertIn("FALL_DETECTION_WINDOW_CONTRACT_BASIC_MISMATCH", source)
        self.assertIn("FALL_DETECTION_WINDOW_CONTRACT_MODEL_SLICE", source)
        self.assertIn("FALL_DETECTION_WINDOW_CONTRACT_POST_CHECK", source)
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
        self.assertIn("cnn_v1_recall90_6ch_2s", source)
        self.assertIn("跌倒表 |", source)
        self.assertIn("事件窗口契约不匹配", source)
        self.assertIn("fall_detection_validate_window_contract(window)", source)
        self.assertIn("contract_errors != FALL_DETECTION_WINDOW_CONTRACT_OK", source)
        self.assertIn("错误=0x%02x", source)
        self.assertIn('#include "features/alerts/app_alert_manager.h"', source)
        self.assertIn('#include "services/memory_watch/watch_endpoint_service.h"', source)
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
        self.assertIn("static const uint16_t k_model_pre_event_frames = 35U;", source)
        self.assertIn("const uint16_t model_start_frame =\n        (uint16_t)(window->trigger_frame_index - k_model_pre_event_frames);", source)
        self.assertIn("for (uint16_t frame = 0; frame < FALL_MODEL_FRAME_COUNT; ++frame)", source)
        self.assertIn("const uint16_t window_frame = (uint16_t)(model_start_frame + frame);", source)
        self.assertIn("const uint16_t base = frame * FALL_MODEL_CHANNEL_COUNT;", source)
        self.assertIn("input[base + 0U] =  window->accel[window_frame].x;", source)
        self.assertIn("input[base + 2U] = -window->accel[window_frame].z;", source)
        self.assertIn("input[base + 3U] =  window->gyro[window_frame].x * k_degrees_to_radians;", source)
        self.assertIn("input[base + 5U] = -window->gyro[window_frame].z * k_degrees_to_radians;", source)

    def test_model_candidate_requires_post_check_before_confirmed_alert(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("static const float k_fall_clear_threshold = 0.50f;", source)
        self.assertIn("static const uint32_t k_fall_clear_window_count = 2U;", source)
        self.assertIn("k_alert_receive_timeout_ticks = pdMS_TO_TICKS(250)", source)
        self.assertIn("k_fall_alert_auto_clear_us = 5LL * 1000LL * 1000LL", source)
        self.assertIn("k_post_gravity_start_frame = 150U", source)
        self.assertIn("k_low_motion_start_frame = 150U", source)
        self.assertIn("k_low_motion_frame_count = 150U", source)
        self.assertIn("k_posture_change_threshold_deg = 45.0f", source)
        self.assertIn("k_low_motion_gyro_mean_max_radps = 0.44f", source)
        self.assertIn("k_low_motion_gyro_peak_max_radps = 1.40f", source)
        self.assertIn("k_low_motion_acc_norm_std_max_mps2 = 1.96f", source)
        self.assertIn("k_strong_fall_candidate_threshold = 0.90f", source)
        self.assertIn("last_alert_time_us", source)
        self.assertIn("fall_detection_post_check_t", source)
        self.assertIn("fall_detection_run_post_check", source)
        self.assertIn("fall_detection_update_alert_timeout", source)
        self.assertIn("esp_timer_get_time()", source)
        self.assertNotIn("fall_detection_should_run_inference", source)
        self.assertNotIn("fall_detection_is_alert_confirmed_active", source)
        self.assertIn("window->trigger_flags != 0U", source)
        self.assertIn("非事件窗口已拒绝:", source)
        self.assertNotIn("定期窗口已忽略:", source)
        self.assertNotIn("idle_no_event", source)
        self.assertIn("is_event_window &&", source)
        self.assertIn(".is_candidate = result->fall_prob >= FALL_MODEL_THRESHOLD_DEFAULT", source)
        self.assertIn("post_check->is_candidate &&\n            post_check->confirmed", source)
        self.assertIn("check.low_motion &&\n        (check.posture_change || check.strong_fallback)", source)
        self.assertIn("候选表 |", source)
        self.assertIn("post检查表 |", source)
        self.assertIn("trigger_acc=%.2f_mps2", source)
        self.assertIn("trigger_gyro=%.2f_radps", source)
        self.assertIn("trigger_jerk=%.2f_mps2pf", source)
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

    def test_destroy_api_detaches_fall_queue_and_releases_model_resources(self) -> None:
        source = FALL_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = FALL_DETECTION_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("fall_detection_service_destroy(void)", header)
        self.assertIn("FALL_DETECTION_SERVICE_STATE_STOPPING", header)
        self.assertIn("fall_detection_release_runtime_resources", source)
        self.assertIn("fall_detection_store_destroyed", source)
        self.assertIn("fall_detection_destroy_requested", source)
        self.assertIn("fall_detection_clear_alert_for_destroy", source)
        self.assertIn("s_fall_detection.destroy_requested = true;", source)
        self.assertIn(
            "s_fall_detection.snapshot.state =\n"
            "            FALL_DETECTION_SERVICE_STATE_STOPPING;",
            source,
        )
        self.assertIn("(void)imu_service_set_window_queue(NULL);", source)
        self.assertIn("fall_model_runner_destroy(s_fall_detection.runner)", source)
        self.assertIn("vQueueDelete(s_fall_detection.window_queue)", source)
        self.assertIn("heap_caps_free(s_fall_detection.window_queue_storage)", source)
        self.assertIn("heap_caps_free(s_fall_detection.current_window)", source)
        self.assertIn("heap_caps_free(s_fall_detection.model_input)", source)
        self.assertIn('return "stopping";', source)
        self.assertIn("已销毁: 模型runner和PSRAM窗口缓冲已释放", source)
        self.assertNotIn("imu_service_stop", source)

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
