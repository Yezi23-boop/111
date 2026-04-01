import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_DIR = REPO_ROOT / "main"


class DangerDetectionServiceSourceTests(unittest.TestCase):
    def test_service_and_alert_pipeline_are_wired(self) -> None:
        service_header = (MAIN_DIR / "danger_detection_service.h").read_text(
            encoding="utf-8"
        )
        service_source = (MAIN_DIR / "danger_detection_service.c").read_text(
            encoding="utf-8"
        )
        alert_source = (MAIN_DIR / "app_alert_manager.c").read_text(
            encoding="utf-8"
        )

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
