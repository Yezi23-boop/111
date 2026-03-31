import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
EVENTS_INIT_SOURCE = REPO_ROOT / "main" / "ui" / "generated" / "events_init.c"
LVGL_TASK_SOURCE = REPO_ROOT / "main" / "lvgl_task.c"
CUSTOM_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "custom.h"
AI_UI_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.h"
AI_UI_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.c"


class AiUiEntrySourceTests(unittest.TestCase):
    def test_main_menu_ai_option_routes_to_custom_ai_page(self) -> None:
        source = EVENTS_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("screen_main_option_2_event_handler", source)
        self.assertIn("ai_ui_open()", source)
        self.assertIn("ui->screen_main_option_2", source)

    def test_lvgl_task_initializes_ai_ui_controller(self) -> None:
        source = LVGL_TASK_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ai_ui_controller.h"', source)
        self.assertIn("ai_ui_controller_init(&guider_ui);", source)

    def test_custom_layer_exposes_ai_ui_bridge_api(self) -> None:
        custom_header = CUSTOM_HEADER.read_text(encoding="utf-8")

        self.assertIn('#include "ai_ui_controller.h"', custom_header)
        self.assertTrue(AI_UI_HEADER.exists())

        ai_header = AI_UI_HEADER.read_text(encoding="utf-8")
        self.assertIn("void ai_ui_controller_init(lv_ui *ui);", ai_header)
        self.assertIn("void ai_ui_open(void);", ai_header)

    def test_ai_ui_controller_reads_network_service_and_can_request_portal(self) -> None:
        self.assertTrue(AI_UI_SOURCE.exists())
        source = AI_UI_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "network_service.h"', source)
        self.assertIn("network_service_get_state()", source)
        self.assertIn("network_service_request_portal()", source)
        self.assertIn("lv_timer_create(", source)
        self.assertIn("未联网", source)
        self.assertIn("进入配网", source)


if __name__ == "__main__":
    unittest.main()
