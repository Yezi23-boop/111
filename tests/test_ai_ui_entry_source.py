import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
EVENTS_INIT_SOURCE = REPO_ROOT / "main" / "ui" / "generated" / "events_init.c"
LVGL_TASK_SOURCE = REPO_ROOT / "main" / "lvgl_task.c"
CUSTOM_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "custom.h"
AI_UI_HEADER = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.h"
AI_UI_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.c"
AI_CHAT_VIEW_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_chat_view.c"


class AiUiEntrySourceTests(unittest.TestCase):
    def test_generated_ai_entry_still_routes_to_handwritten_ai_page(self) -> None:
        source = EVENTS_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ai_ui_open();", source)

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

        self.assertIn('#include "ai_chat_view.h"', source)
        self.assertIn('#include "network_service.h"', source)
        self.assertIn('#include "official_chat_service.h"', source)
        self.assertIn("network_service_get_state()", source)
        self.assertIn("network_service_request_portal()", source)
        self.assertIn("lv_timer_create(", source)
        self.assertIn("进入配网", source)
        self.assertIn("ai_chat_view_create(", source)
        self.assertIn("ai_chat_view_reload_messages(", source)
        self.assertIn("official_chat_service_request_shutdown(", source)
        self.assertIn("official_chat_service_is_shutdown_pending()", source)
        self.assertIn("s_exit_requested", source)
        self.assertIn("ai_ui_ensure_screen_created();", source)
        self.assertIn("ai_ui_complete_exit_to_main(", source)
        self.assertIn("ai_ui_destroy_screen(", source)
        self.assertIn("ai_ui_destroy_screen_cb(", source)
        self.assertIn("s_view = NULL;", source)
        self.assertIn("lv_timer_delete(s_status_timer);", source)
        self.assertIn("s_status_timer = NULL;", source)
        self.assertIn("lv_screen_load_anim(s_ui->screen_main",
                      source)
        self.assertIn("LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0,", source)
        self.assertIn("true);", source)
        self.assertIn("lv_timer_create(ai_ui_destroy_screen_cb, 350, NULL);",
                      source)
        self.assertIn("s_pending_destroy_view = view_to_destroy;", source)
        self.assertIn("ai_chat_view_destroy(s_pending_destroy_view);", source)
        self.assertNotIn("ai_chat_view_destroy(s_view);", source)
        self.assertIn("正在退出", source)
        self.assertIn("official_chat_service_get_state() ==", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_STATE_STOPPED", source)
        self.assertIn("ai_ui_open(void)", source)

    def test_ai_chat_view_source_builds_scrollable_bubble_list(self) -> None:
        self.assertTrue(AI_CHAT_VIEW_SOURCE.exists())
        source = AI_CHAT_VIEW_SOURCE.read_text(encoding="utf-8")

        self.assertIn("lv_obj_set_scroll_dir(", source)
        self.assertIn("LV_DIR_VER", source)
        self.assertIn("ai_chat_view_reload_messages(", source)
        self.assertIn("ai_chat_view_create_spacer(", source)
        self.assertIn("AI_CHAT_BUBBLE_USER", source)
        self.assertIn("AI_CHAT_BUBBLE_ASSISTANT", source)
        self.assertIn("official_chat_service_get_message_count(", source)
        self.assertIn("official_chat_service_get_message(", source)
        self.assertIn("lv_obj_scroll_to_view(", source)
        self.assertIn("聊天区", source)


if __name__ == "__main__":
    unittest.main()
