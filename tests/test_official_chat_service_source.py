import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SERVICE_HEADER = REPO_ROOT / "main" / "official_chat_service.h"
SERVICE_SOURCE = REPO_ROOT / "main" / "official_chat_service.c"
MAIN_ENTRY_SOURCE = REPO_ROOT / "main" / "111.c"
AI_UI_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.c"


class OfficialChatServiceSourceTests(unittest.TestCase):
    def test_service_header_and_source_exist(self) -> None:
        self.assertTrue(SERVICE_HEADER.exists())
        self.assertTrue(SERVICE_SOURCE.exists())

    def test_service_wraps_official_chat_lifecycle(self) -> None:
        source = SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "official_chat.h"', source)
        self.assertIn("official_chat_create(", source)
        self.assertIn("official_chat_set_event_callback(", source)
        self.assertIn("official_chat_start(", source)
        self.assertIn("network_service_is_service_ready()", source)
        self.assertIn("official_chat_service_enter_foreground(", source)

    def test_formal_entry_initializes_official_chat_service(self) -> None:
        source = MAIN_ENTRY_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "official_chat_service.h"', source)
        self.assertIn("official_chat_service_init()", source)

    def test_ai_ui_controller_uses_service_for_auto_start_and_status(self) -> None:
        source = AI_UI_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "official_chat_service.h"', source)
        self.assertIn("official_chat_service_enter_foreground()", source)
        self.assertIn("official_chat_service_get_state()", source)
        self.assertIn("待唤醒", source)
        self.assertIn("回答中", source)


if __name__ == "__main__":
    unittest.main()
