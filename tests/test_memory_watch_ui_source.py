import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import LVGL_TASK_SOURCE
from tests.main_paths import MAIN_CMAKE
from tests.main_paths import REPO_ROOT
from tests.main_paths import UI_CUSTOM_HEADER
from tests.main_paths import UI_EVENTS_INIT_SOURCE
from tests.main_paths import UI_MEMORY_WATCH_CONTROLLER_HEADER
from tests.main_paths import UI_MEMORY_WATCH_CONTROLLER_SOURCE
from tests.main_paths import UI_MEMORY_WATCH_VIEW_HEADER
from tests.main_paths import UI_MEMORY_WATCH_VIEW_SOURCE
from tests.main_paths import WATCH_NOTIFICATION_CENTER_SOURCE


class MemoryWatchUiSourceTests(unittest.TestCase):
    def test_main_menu_entry_binds_unused_option_8_without_generated_edit(self) -> None:
        source = UI_EVENTS_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("screen_main_option_8", source)
        self.assertIn("screen_main_user", source)
        self.assertIn("memory_watch_controller_open();", source)
        self.assertIn("lv_obj_add_flag(ui->screen_main_option_8, LV_OBJ_FLAG_CLICKABLE)", source)

    def test_lvgl_task_initializes_and_polls_memory_watch_controller(self) -> None:
        source = LVGL_TASK_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ui/custom/memory_watch_controller.h"', source)
        self.assertIn("memory_watch_controller_init(&guider_ui);", source)
        self.assertIn("memory_watch_controller_poll_ui();", source)

    def test_app_main_starts_memory_watch_owner_without_server_call(self) -> None:
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/memory_watch/memory_watch_service.h"', source)
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
        self.assertIn('strcmp(snapshot->progress_phase, "recognized")', source)
        self.assertIn('return "已识别你的问题";', source)
        self.assertIn('return "正在搜索";', source)
        self.assertIn('return "正在执行任务";', source)
        self.assertIn('return "正在整理结果";', source)
        self.assertIn("Hermes 在线", source)
        self.assertIn("void memory_watch_controller_init(lv_ui *ui);", header)
        self.assertIn("void memory_watch_controller_open(void);", header)
        self.assertIn("void memory_watch_controller_poll_ui(void);", header)

    def test_controller_destroys_view_after_leaving_hermes_page(self) -> None:
        source = UI_MEMORY_WATCH_CONTROLLER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("s_destroy_timer", source)
        self.assertIn("s_pending_destroy_view", source)
        self.assertIn("memory_watch_controller_schedule_view_destroy", source)
        self.assertIn("memory_watch_controller_destroy_view_cb", source)
        self.assertIn("memory_watch_view_destroy(s_pending_destroy_view)", source)
        self.assertIn("lv_timer_create(memory_watch_controller_destroy_view_cb", source)

        back_section = source.split(
            "static void memory_watch_controller_back"
        )[1].split("static void memory_watch_controller_press_start", 1)[0]
        self.assertIn("lv_screen_load_anim", back_section)
        self.assertIn("memory_watch_controller_schedule_view_destroy();", back_section)
        self.assertIn("memory_watch_service_cancel_recording();", back_section)
        self.assertNotIn("memory_watch_service_cancel_waiting();", back_section)
        self.assertIn("离开 Hermes 页面不等于取消已上传的 Hermes 任务", back_section)

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

    def test_view_supports_swipe_inbox_list_and_read_only_detail(self) -> None:
        source = UI_MEMORY_WATCH_VIEW_SOURCE.read_text(encoding="utf-8")
        header = UI_MEMORY_WATCH_VIEW_HEADER.read_text(encoding="utf-8")

        self.assertIn("MEMORY_WATCH_VIEW_PAGE_INBOX", header)
        self.assertIn("MEMORY_WATCH_VIEW_PAGE_INBOX_DETAIL", header)
        self.assertIn("memory_watch_view_inbox_item_t", header)
        self.assertIn("open_inbox_cb", header)
        self.assertIn("open_voice_cb", header)
        self.assertIn("inbox_back_cb", header)
        self.assertIn("open_inbox_item_cb", header)
        self.assertIn("inbox_unread_count", header)

        self.assertIn("LV_EVENT_GESTURE", source)
        self.assertIn("LV_DIR_LEFT", source)
        self.assertIn("LV_DIR_RIGHT", source)
        self.assertIn("memory_watch_view_rebuild_inbox_list", source)
        self.assertIn("memory_watch_view_update_detail", source)
        self.assertIn("只读查看", source)
        self.assertNotIn("删除", source)
        self.assertNotIn("回复", source)

    def test_view_uses_conversation_stream_and_status_dot(self) -> None:
        source = UI_MEMORY_WATCH_VIEW_SOURCE.read_text(encoding="utf-8")
        header = UI_MEMORY_WATCH_VIEW_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_view_conversation_item_t", header)
        self.assertIn("memory_watch_view_connection_state_t", header)
        self.assertIn("conversation_items", header)
        self.assertIn("conversation_item_count", header)
        self.assertIn("connection_state", header)

        self.assertIn("conversation_list", source)
        self.assertIn("memory_watch_view_rebuild_conversation", source)
        self.assertIn("connection_dot", source)
        self.assertIn("0x2f8f46", source)
        self.assertIn("0xd84a3a", source)
        self.assertIn("0xb8b8b3", source)
        self.assertIn("按住说话开始记录", source)
        self.assertNotIn("user_bubble", source)
        self.assertNotIn("reply_bubble", source)

    def test_controller_keeps_inbox_preview_host_only(self) -> None:
        source = UI_MEMORY_WATCH_CONTROLLER_SOURCE.read_text(encoding="utf-8")
        host_cmake = (
            REPO_ROOT
            / "tools"
            / "ui_preview"
            / "host_runner"
            / "CMakeLists.txt"
        ).read_text(encoding="utf-8")

        self.assertIn("#ifdef AGENT_PREVIEW_HOST", source)
        self.assertIn("s_preview_inbox_items", source)
        self.assertIn("memory_watch_controller_open_inbox", source)
        self.assertIn("memory_watch_controller_open_voice", source)
        self.assertIn("memory_watch_controller_open_inbox_item", source)
        self.assertIn("s_preview_inbox_items[index].read = true", source)
        self.assertIn("AGENT_PREVIEW_HOST=1", host_cmake)
        self.assertNotIn("memory_watch_voice_client_inbox", source)

    def test_controller_reads_conversation_history_from_service_cache(self) -> None:
        source = UI_MEMORY_WATCH_CONTROLLER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("MEMORY_WATCH_SERVICE_CONVERSATION_MAX_ITEMS", source)
        self.assertIn("s_conversation_items", source)
        self.assertIn('#include "esp_heap_caps.h"', source)
        self.assertIn("memory_watch_controller_alloc_psram_caches", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)
        self.assertIn("s_inbox_summaries = (memory_watch_inbox_summary_t *)heap_caps_calloc", source)
        self.assertIn("s_inbox_detail = (memory_watch_inbox_item_t *)heap_caps_calloc", source)
        self.assertIn("s_conversation_revision", source)
        self.assertIn("memory_watch_controller_sync_conversation", source)
        self.assertIn("snapshot->conversation_generation", source)
        self.assertIn("memory_watch_service_copy_conversation_items", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CONVERSATION_USER", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CONVERSATION_HERMES", source)
        self.assertIn("memory_watch_connection_state", source)
        self.assertNotIn("s_conversation_entries", source)
        self.assertNotIn("s_last_user_request_id", source)
        self.assertNotIn("s_last_reply_request_id", source)
        self.assertNotIn("memory_watch_service_history", source)

    def test_notification_center_uses_psram_scratch_not_external_bss_hint(self) -> None:
        source = WATCH_NOTIFICATION_CENTER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "esp_heap_caps.h"', source)
        self.assertIn("static memory_watch_inbox_summary_t *s_nc_scratch = NULL", source)
        self.assertIn("nc_ensure_scratch", source)
        self.assertIn("heap_caps_calloc(", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)
        self.assertNotIn("section(\".ext_ram.bss\")", source)

    def test_controller_reports_memory_watch_foreground_without_owning_transport(self) -> None:
        source = UI_MEMORY_WATCH_CONTROLLER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("memory_watch_service_set_foreground(true)", source)
        self.assertIn("memory_watch_service_set_foreground(false)", source)
        self.assertIn("static bool s_last_foreground = false", source)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_TIMEOUT", source)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_CANCELED", source)
        bubble_section = source.split("watch_nc_notify_hermes_reply(NULL)")[0].rsplit(
            "if (!is_fg)", 1
        )[1]
        self.assertNotIn("MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION", bubble_section)
        self.assertNotIn("memory_watch_ws_client_close", source)
        self.assertNotIn("esp_http_client", source)

    def test_host_runner_observes_sdl_quit_without_consuming_input_events(self) -> None:
        source = (
            REPO_ROOT
            / "tools"
            / "ui_preview"
            / "host_runner"
            / "main.c"
        ).read_text(encoding="utf-8")
        loop_section = source.split("while (running)", 1)[1].split("lv_sdl_quit();", 1)[0]

        self.assertIn("SDL_AddEventWatch(preview_sdl_event_watch, NULL);", source)
        self.assertIn("SDL_DelEventWatch(preview_sdl_event_watch, NULL);", source)
        self.assertIn("SDL_AtomicSet(&s_preview_quit_requested, 1);", source)
        self.assertNotIn("SDL_PollEvent(", loop_section)

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
