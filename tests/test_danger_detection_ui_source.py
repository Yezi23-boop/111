import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]


class DangerDetectionUiSourceTests(unittest.TestCase):
    def test_option6_and_lvgl_task_are_wired_to_danger_detection(self) -> None:
        events_source = (
            REPO_ROOT / "main" / "ui" / "generated" / "events_init.c"
        ).read_text(encoding="utf-8")
        lvgl_task_source = (REPO_ROOT / "main" / "lvgl_task.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("screen_main_option_6_event_handler", events_source)
        self.assertIn("danger_detection_ui_open()", events_source)
        self.assertIn("danger_detection_controller_init(&guider_ui);", lvgl_task_source)
        self.assertIn("display_alert_adapter_process_ui();", lvgl_task_source)


if __name__ == "__main__":
    unittest.main()
