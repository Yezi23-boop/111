import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import LVGL_TASK_SOURCE
from tests.main_paths import MAIN_CMAKE
from tests.main_paths import UI_CUSTOM_HEADER
from tests.main_paths import UI_MEMORY_WATCH_CONTROLLER_HEADER
from tests.main_paths import UI_MEMORY_WATCH_CONTROLLER_SOURCE
from tests.main_paths import UI_MEMORY_WATCH_VIEW_HEADER
from tests.main_paths import UI_MEMORY_WATCH_VIEW_SOURCE


class MemoryWatchUiSourceTests(unittest.TestCase):
    def test_main_menu_entry_binds_unused_option_8_without_generated_edit(self) -> None:
        source = UI_MEMORY_WATCH_CONTROLLER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("screen_main_option_8", source)
        self.assertIn("screen_main_user", source)
        self.assertIn("memory_watch_controller_open();", source)
        self.assertIn("lv_obj_add_flag(ui->screen_main_option_8, LV_OBJ_FLAG_CLICKABLE)", source)
        self.assertIn('lv_label_set_text(s_entry_label, "Hermes")', source)
        self.assertIn("记忆手表", source)

    def test_lvgl_task_initializes_and_polls_memory_watch_controller(self) -> None:
        source = LVGL_TASK_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ui/custom/memory_watch_controller.h"', source)
        self.assertIn("memory_watch_controller_init(&guider_ui);", source)
        self.assertIn("memory_watch_controller_poll_ui();", source)

    def test_app_main_starts_memory_watch_owner_without_server_call(self) -> None:
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/memory_watch_service.h"', source)
        self.assertIn("memory_watch_service_init()", source)
        self.assertIn("boot_stage: memory_watch_ready", source)
        self.assertLess(
            source.index("official_chat_service_init()"),
            source.index("memory_watch_service_init()"),
        )

    def test_controller_maps_view_actions_to_service_commands_only(self) -> None:
        source = UI_MEMORY_WATCH_CONTROLLER_SOURCE.read_text(encoding="utf-8")
        header = UI_MEMORY_WATCH_CONTROLLER_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_service_check_health()", source)
        self.assertIn("memory_watch_service_get_snapshot(&snapshot)", source)
        self.assertIn("memory_watch_service_begin_recording()", source)
        self.assertIn("memory_watch_service_send_recording()", source)
        self.assertIn("memory_watch_service_cancel_recording()", source)
        self.assertIn("memory_watch_service_cancel_waiting()", source)
        self.assertIn("memory_watch_service_cancel_clarification()", source)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_UPLOADING", source)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_THINKING", source)
        self.assertIn("Hermes 在线", source)
        self.assertIn("void memory_watch_controller_init(lv_ui *ui);", header)
        self.assertIn("void memory_watch_controller_open(void);", header)
        self.assertIn("void memory_watch_controller_poll_ui(void);", header)

    def test_view_implements_hold_release_and_slide_cancel_callbacks(self) -> None:
        source = UI_MEMORY_WATCH_VIEW_SOURCE.read_text(encoding="utf-8")
        header = UI_MEMORY_WATCH_VIEW_HEADER.read_text(encoding="utf-8")

        self.assertIn("LV_EVENT_PRESSED", source)
        self.assertIn("LV_EVENT_PRESSING", source)
        self.assertIn("LV_EVENT_RELEASED", source)
        self.assertIn("LV_EVENT_PRESS_LOST", source)
        self.assertIn("lv_indev_get_point", source)
        self.assertIn("lv_obj_get_coords", source)
        self.assertIn("press_start_cb", source)
        self.assertIn("release_send_cb", source)
        self.assertIn("slide_cancel_cb", source)
        self.assertIn("cancel_waiting_cb", source)
        self.assertIn("cancel_clarification_cb", source)
        self.assertIn("松开发送", source)
        self.assertIn("松手取消", source)
        self.assertIn("memory_watch_view_apply_model", header)

    def test_memory_watch_ui_keeps_official_chat_and_transport_boundaries(self) -> None:
        combined = "\n".join(
            [
                UI_MEMORY_WATCH_CONTROLLER_SOURCE.read_text(encoding="utf-8"),
                UI_MEMORY_WATCH_CONTROLLER_HEADER.read_text(encoding="utf-8"),
                UI_MEMORY_WATCH_VIEW_SOURCE.read_text(encoding="utf-8"),
                UI_MEMORY_WATCH_VIEW_HEADER.read_text(encoding="utf-8"),
            ]
        )

        self.assertNotIn("official_chat", combined)
        self.assertNotIn("ai_chat_view", combined)
        self.assertNotIn("esp_http_client", combined)
        self.assertNotIn("audio_codec", combined)
        self.assertNotIn("memory_watch_voice_client", combined)

    def test_cmake_and_custom_header_register_memory_watch_ui(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8").replace("\\", "/")
        custom_header = UI_CUSTOM_HEADER.read_text(encoding="utf-8")

        self.assertIn("ui/custom/memory_watch_controller.c", cmake)
        self.assertIn("ui/custom/memory_watch_view.c", cmake)
        self.assertIn('#include "memory_watch_controller.h"', custom_header)


if __name__ == "__main__":
    unittest.main()
