import unittest

from tests.main_paths import UI_CUSTOM_DIR


DANGER_DETECTION_CONTROLLER_SOURCE = (
    UI_CUSTOM_DIR / "danger_detection_controller.c"
)


class DangerDetectionControllerSourceTests(unittest.TestCase):
    def test_ui_uses_background_manager_switch_and_displays_danger_label(self) -> None:
        source = DANGER_DETECTION_CONTROLLER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "background_service_manager_set_danger_detection_enabled(enabled)",
            source,
        )
        self.assertNotIn("danger_detection_service_start_with_backend(", source)
        self.assertNotIn("(void)danger_detection_service_start();", source)
        self.assertIn("case DANGER_DETECTION_LABEL_DANGER:", source)
        self.assertIn('return "DANGER";', source)
        self.assertIn("安全监听", (UI_CUSTOM_DIR / "danger_detection_view.c").read_text(encoding="utf-8"))

    def test_status_text_uses_manager_runtime_snapshot_for_transition(self) -> None:
        source = DANGER_DETECTION_CONTROLLER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("manager_snapshot->danger_should_run", source)
        self.assertIn("manager_snapshot->danger_runtime_running", source)
        self.assertIn(
            "manager_snapshot->danger_block_reason",
            source,
        )
        self.assertIn("BACKGROUND_SERVICE_MANAGER_DANGER_BLOCK_USER_DISABLED", source)
        self.assertIn("BACKGROUND_SERVICE_MANAGER_DANGER_BLOCK_POLICY", source)
        self.assertIn("BACKGROUND_SERVICE_MANAGER_DANGER_BLOCK_FOREGROUND_AUDIO", source)
        self.assertIn("BACKGROUND_SERVICE_MANAGER_DANGER_BLOCK_FOREGROUND_RUNTIME", source)
        self.assertIn('return "资源占用，暂时等待";', source)
        self.assertIn('return "正在启动";', source)
        self.assertIn('return "正在停止";', source)
        self.assertLess(
            source.index("state == DANGER_DETECTION_STATE_STOPPING"),
            source.index("manager_snapshot->danger_block_reason"),
        )


if __name__ == "__main__":
    unittest.main()
