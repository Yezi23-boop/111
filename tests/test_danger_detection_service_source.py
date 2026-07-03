import unittest

from tests.main_paths import APP_ALERT_MANAGER_SOURCE
from tests.main_paths import DANGER_DETECTION_SERVICE_HEADER
from tests.main_paths import DANGER_DETECTION_SERVICE_SOURCE


class DangerDetectionServiceSourceTests(unittest.TestCase):
    def test_service_and_alert_pipeline_are_wired(self) -> None:
        service_header = DANGER_DETECTION_SERVICE_HEADER.read_text(encoding="utf-8")
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")
        alert_source = APP_ALERT_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("danger_detection_service_start", service_header)
        self.assertIn("danger_detection_service_stop", service_header)
        self.assertIn("DANGER_DETECTION_STATE_RUNNING", service_header)
        self.assertIn(
            "traffic_inference_postprocess_set_alert_callback", service_source
        )
        self.assertIn("app_alert_manager_raise", service_source)
        self.assertIn("app_alert_manager_clear", service_source)
        self.assertIn("watch_endpoint_service_post_danger_alert", service_source)
        self.assertNotIn('#include "services/memory_watch_service.h"', service_source)
        self.assertIn("audio_alert_player_play_warning_once", alert_source)

    def test_espdl_backend_is_single_model_default(self) -> None:
        service_header = DANGER_DETECTION_SERVICE_HEADER.read_text(encoding="utf-8")
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("DANGER_DETECTION_BACKEND_ESPDL", service_header)
        self.assertIn(
            "DANGER_DETECTION_BACKEND_ESPDL);",
            service_source,
        )
        self.assertIn("const espdl_model_result_t *result", service_source)
        self.assertIn("result->probabilities[1]", service_source)
        self.assertNotIn("DANGER_DETECTION_BACKEND_ESPDL_DUAL", service_header)
        self.assertNotIn("DANGER_DETECTION_BACKEND_ESPDL_DUAL", service_source)
        self.assertNotIn("espdl_dual_runner", service_source)
        self.assertNotIn("fused_danger_prob", service_source)
        self.assertNotIn("fusion_strategy", service_source)

    def test_espdl_backend_uses_consecutive_window_postprocess(self) -> None:
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn(".confirm_windows = 2U", service_source)
        self.assertIn(".clear_windows = 3U", service_source)
        self.assertIn(".alert_hold_ms = 2000U", service_source)
        self.assertIn("danger_detection_service_get_policy_profile", service_source)
        self.assertIn("s_espdl_danger_window_count", service_source)
        self.assertIn("s_espdl_clear_window_count", service_source)
        self.assertIn("s_espdl_hold_until_tick", service_source)
        self.assertIn("danger_detection_reset_espdl_postprocess", service_source)

    def test_stop_failure_keeps_runtime_owned_until_cleanup_succeeds(self) -> None:
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("service_state == DANGER_DETECTION_STATE_STOPPING", service_source)
        self.assertIn("service_state == DANGER_DETECTION_STATE_ERROR", service_source)
        self.assertIn("if (ret != ESP_OK)\n    {\n        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);\n        return ret;\n    }", service_source)
        self.assertLess(
            service_source.index("if (ret != ESP_OK)\n    {\n        danger_detection_set_state(DANGER_DETECTION_STATE_ERROR, ret);\n        return ret;\n    }"),
            service_source.index("s_service_state.callback_registered = false;"),
        )

    def test_espdl_callback_rechecks_service_state_before_alert_commit(self) -> None:
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("danger_detection_service_allows_alert_commit", service_source)
        self.assertIn("state != DANGER_DETECTION_STATE_STOPPING", service_source)
        self.assertIn("state != DANGER_DETECTION_STATE_ERROR", service_source)
        self.assertIn("if (!danger_detection_service_allows_alert_commit())", service_source)
        espdl_callback = service_source[
            service_source.index("static void danger_detection_on_espdl_result") :
            service_source.index("/**\n * @brief 初始化危险检测服务。")
        ]
        self.assertLess(
            espdl_callback.index("if (!danger_detection_service_allows_alert_commit())"),
            espdl_callback.index("app_alert_request_t request"),
        )
        self.assertIn(
            "danger_detection_set_state(DANGER_DETECTION_STATE_IDLE, ESP_OK);\n    (void)app_alert_manager_clear(APP_ALERT_SOURCE_TRAFFIC_AUDIO);",
            service_source,
        )

    def test_alerting_posts_cloud_alert_once_per_confirmed_raise(self) -> None:
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")
        espdl_callback = service_source[
            service_source.index("static void danger_detection_on_espdl_result") :
            service_source.index("/**\n * @brief 初始化危险检测服务。")
        ]

        self.assertIn("danger_detection_post_cloud_alert", service_source)
        self.assertIn("kDangerAlertCloudMessage", service_source)
        self.assertIn("alert_sequence = s_service_state.snapshot.alert_sequence", espdl_callback)
        self.assertIn('danger_detection_post_cloud_alert("danger", danger_prob', espdl_callback)
        self.assertLess(
            espdl_callback.index("should_raise_alert = true;"),
            espdl_callback.index('danger_detection_post_cloud_alert("danger", danger_prob'),
        )

    def test_alerting_capture_uses_window_end_sample_index(self) -> None:
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")
        espdl_callback = service_source[
            service_source.index("static void danger_detection_on_espdl_result") :
            service_source.index("/**\n * @brief 初始化危险检测服务。")
        ]

        self.assertIn("danger_sample_recorder_capture", service_source)
        self.assertIn("result->window_end_sample_index", espdl_callback)
        self.assertLess(
            espdl_callback.index("app_alert_manager_raise(&request)"),
            espdl_callback.index("danger_sample_recorder_capture("),
        )

    def test_recorder_stop_path_resets_session_instead_of_deinit(self) -> None:
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")
        stop_body = service_source[
            service_source.index("esp_err_t danger_detection_service_stop") :
            service_source.index("/**\n * @brief 获取当前危险检测快照。")
        ]

        self.assertIn("danger_sample_recorder_reset_session();", stop_body)
        self.assertNotIn("danger_sample_recorder_deinit();", stop_body)
        self.assertIn("普通后台开关 stop 只重置会话", stop_body)

    def test_espdl_pcm_tap_uses_type_safe_adapter(self) -> None:
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("danger_detection_on_espdl_pcm_tap", service_source)
        self.assertIn("const espdl_audio_pcm_window_meta_t *meta", service_source)
        self.assertIn("const danger_sample_pcm_window_meta_t recorder_meta", service_source)
        self.assertIn("recorder_callback(pcm_data, samples, &recorder_meta, NULL)", service_source)
        self.assertIn(
            "espdl_audio_runtime_set_pcm_tap_callback(\n        danger_detection_on_espdl_pcm_tap, NULL)",
            service_source,
        )
        self.assertNotIn("(espdl_audio_pcm_tap_callback_t)", service_source)

    def test_recorder_capture_failure_does_not_block_alert_pipeline(self) -> None:
        service_source = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")
        espdl_callback = service_source[
            service_source.index("static void danger_detection_on_espdl_result") :
            service_source.index("/**\n * @brief 初始化危险检测服务。")
        ]

        self.assertLess(
            espdl_callback.index("app_alert_manager_raise(&request)"),
            espdl_callback.index("danger_sample_recorder_capture("),
        )
        self.assertIn("录制失败不影响主功能", espdl_callback)


if __name__ == "__main__":
    unittest.main()
