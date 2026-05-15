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


if __name__ == "__main__":
    unittest.main()
