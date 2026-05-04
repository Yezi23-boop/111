import unittest

from tests.main_paths import UI_CUSTOM_DIR


DANGER_DETECTION_CONTROLLER_SOURCE = (
    UI_CUSTOM_DIR / "danger_detection_controller.c"
)


class DangerDetectionControllerSourceTests(unittest.TestCase):
    def test_ui_starts_espdl_backend_and_displays_danger_label(self) -> None:
        source = DANGER_DETECTION_CONTROLLER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "danger_detection_service_start_with_backend(\n"
            "        DANGER_DETECTION_BACKEND_ESPDL)",
            source,
        )
        self.assertIn("case DANGER_DETECTION_LABEL_DANGER:", source)
        self.assertIn('return "DANGER";', source)
        self.assertNotIn("(void)danger_detection_service_start();", source)


if __name__ == "__main__":
    unittest.main()
