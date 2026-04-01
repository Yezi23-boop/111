import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
AI_CHAT_VIEW_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "ai_chat_view.h"
AI_CHAT_VIEW_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_chat_view.c"


class AiChatViewSourceTests(unittest.TestCase):
    def test_shared_chat_view_files_exist(self) -> None:
        self.assertTrue(AI_CHAT_VIEW_HEADER.exists())
        self.assertTrue(AI_CHAT_VIEW_SOURCE.exists())

    def test_main_cmakelists_registers_shared_chat_view_source(self) -> None:
        source = (REPO_ROOT / "main" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )

        self.assertIn("ui/custom/ai_chat_view.c", source)

    def test_shared_chat_view_exposes_bubble_oriented_api(self) -> None:
        header = AI_CHAT_VIEW_HEADER.read_text(encoding="utf-8")

        self.assertIn("ai_chat_view_t", header)
        self.assertIn("ai_chat_view_config_t", header)
        self.assertIn("ai_chat_view_create(", header)
        self.assertIn("ai_chat_view_reload_messages(", header)
        self.assertIn("ai_chat_view_scroll_to_bottom(", header)
        self.assertIn("ai_chat_view_set_top_status(", header)

    def test_shared_chat_view_implements_left_and_right_chat_bubbles(self) -> None:
        source = AI_CHAT_VIEW_SOURCE.read_text(encoding="utf-8")

        self.assertIn("AI_CHAT_BUBBLE_USER", source)
        self.assertIn("AI_CHAT_BUBBLE_ASSISTANT", source)
        self.assertIn("ai_chat_view_create_spacer(", source)
        self.assertIn("lv_obj_set_scroll_dir(", source)
        self.assertIn("LV_DIR_VER", source)
        self.assertIn("lv_obj_scroll_to_view(", source)
        self.assertIn("lv_obj_set_flex_flow(", source)
        self.assertIn("聊天区", source)


if __name__ == "__main__":
    unittest.main()
