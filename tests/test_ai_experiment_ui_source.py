import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
AI_EXPERIMENT_UI = REPO_ROOT / "main" / "ai_experiment_ui.c"


class AiExperimentUiSourceTests(unittest.TestCase):
    def test_standalone_ai_experiment_ui_exists(self) -> None:
        self.assertTrue(AI_EXPERIMENT_UI.exists())

    def test_standalone_ai_experiment_ui_bootstraps_lvgl_and_network_actions(self) -> None:
        source = AI_EXPERIMENT_UI.read_text(encoding="utf-8")

        self.assertIn("lv_port_init_small()", source)
        self.assertIn("lv_timer_create(", source)
        self.assertIn("network_service_request_portal()", source)
        self.assertIn("official_chat_service_enter_foreground()", source)

    def test_standalone_ai_experiment_ui_maps_network_and_chat_states(self) -> None:
        source = AI_EXPERIMENT_UI.read_text(encoding="utf-8")

        self.assertIn("NETWORK_SERVICE_STATE_SERVICE_READY", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_STATE_LISTENING", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_STATE_SPEAKING", source)
        self.assertIn("待唤醒", source)
        self.assertIn("进入配网", source)


if __name__ == "__main__":
    unittest.main()
