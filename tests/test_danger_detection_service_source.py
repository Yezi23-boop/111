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


if __name__ == "__main__":
    unittest.main()
