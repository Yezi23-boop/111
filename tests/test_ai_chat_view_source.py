import pathlib
import unittest

from tests.main_cmake_contract import assert_main_source_globbed


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
AI_CHAT_VIEW_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "ai_chat_view.h"
AI_CHAT_VIEW_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_chat_view.c"


class AiChatViewSourceTests(unittest.TestCase):
    def test_shared_chat_view_files_exist(self) -> None:
        self.assertTrue(AI_CHAT_VIEW_HEADER.exists())
        self.assertTrue(AI_CHAT_VIEW_SOURCE.exists())

    def test_main_cmakelists_registers_shared_chat_view_source(self) -> None:
        assert_main_source_globbed(self, "ui/custom/ai_chat_view.c")

    def test_shared_chat_view_exposes_bubble_oriented_api(self) -> None:
        header = AI_CHAT_VIEW_HEADER.read_text(encoding="utf-8")

        self.assertIn("ai_chat_view_t", header)
        self.assertIn("ai_chat_view_config_t", header)
        self.assertIn("ai_chat_view_create(", header)
        self.assertIn("ai_chat_view_reload_messages(", header)
        self.assertIn("ai_chat_view_scroll_to_bottom(", header)
        self.assertIn("ai_chat_view_set_top_status(", header)
        self.assertIn("voice_press_cb", header)
        self.assertIn("voice_release_cb", header)
        self.assertIn("ai_chat_view_set_voice_button_visible(", header)
        self.assertNotIn("primary_action_text", header)
        self.assertNotIn("primary_action_cb", header)
        self.assertNotIn("ai_chat_view_set_primary_action(", header)

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

    def test_shared_chat_view_implements_large_voice_press_button(self) -> None:
        source = AI_CHAT_VIEW_SOURCE.read_text(encoding="utf-8")

        self.assertIn("voice_btn", source)
        self.assertIn("voice_label", source)
        self.assertIn("ai_chat_view_voice_event_cb", source)
        self.assertIn("LV_EVENT_PRESSED", source)
        self.assertIn("LV_EVENT_RELEASED", source)
        self.assertIn("LV_EVENT_PRESS_LOST", source)
        self.assertIn("voice_press_cb", source)
        self.assertIn("voice_release_cb", source)
        self.assertIn("lv_obj_set_size(view->voice_btn, 300, 54);", source)
        self.assertIn("lv_obj_set_pos(view->voice_btn, 55, 384);", source)
        self.assertIn("松开发送", source)

    def test_shared_chat_view_removes_network_primary_action(self) -> None:
        source = AI_CHAT_VIEW_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("primary_btn", source)
        self.assertNotIn("primary_label", source)
        self.assertNotIn("primary_action_cb", source)
        self.assertNotIn("primary_action_text", source)
        self.assertNotIn("ai_chat_view_set_primary_action", source)
        self.assertNotIn("进入配网", source)

    def test_shared_chat_view_removes_top_icon(self) -> None:
        source = AI_CHAT_VIEW_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("icon_ring", source)
        self.assertNotIn("_ai_RGB565A8_70x70", source)
        self.assertNotIn("lv_image_create(view->icon_ring)", source)
        self.assertIn("lv_obj_set_pos(view->chat_card, 40, 28);", source)
        self.assertIn("lv_obj_set_size(view->chat_card, 330, 332);", source)
        self.assertIn("lv_obj_set_style_bg_opa(view->chat_card, LV_OPA_TRANSP, 0);", source)
        self.assertIn("lv_obj_set_style_border_width(view->chat_scroll, 0, 0);", source)


if __name__ == "__main__":
    unittest.main()
