import unittest

from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MEMORY_WATCH_SERVICE_HEADER
from tests.main_paths import MEMORY_WATCH_SERVICE_SOURCE


class MemoryWatchServiceSourceTests(unittest.TestCase):
    def test_service_header_and_source_exist(self) -> None:
        self.assertTrue(MEMORY_WATCH_SERVICE_HEADER.exists())
        self.assertTrue(MEMORY_WATCH_SERVICE_SOURCE.exists())

    def test_service_exposes_v1_snapshot_and_commands(self) -> None:
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_service_state_t", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_READY", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_RECORDING", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_UPLOADING", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_THINKING", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_TIMEOUT", header)
        self.assertIn("memory_watch_service_snapshot_t", header)
        self.assertIn("network_ready", header)
        self.assertIn("request_active", header)
        self.assertIn("clarification_active", header)
        self.assertIn("request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES]", header)
        self.assertIn("asr_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES]", header)
        self.assertIn("reply_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES]", header)
        self.assertIn("esp_err_t memory_watch_service_init(void);", header)
        self.assertIn("memory_watch_service_begin_recording", header)
        self.assertIn("memory_watch_service_send_recording", header)
        self.assertIn("memory_watch_service_cancel_recording", header)
        self.assertIn("memory_watch_service_cancel_waiting", header)
        self.assertIn("memory_watch_service_cancel_clarification", header)
        self.assertIn("memory_watch_service_get_snapshot", header)

    def test_service_uses_owner_task_queue_and_snapshot(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "network_service.h"', source)
        self.assertIn("xQueueCreateStatic(", source)
        self.assertIn("xQueueSend(", source)
        self.assertIn("xQueueReceive(", source)
        self.assertIn("xTaskCreateStatic(", source)
        self.assertIn("portMUX_TYPE s_snapshot_lock", source)
        self.assertIn("memory_watch_service_copy_snapshot(", source)
        self.assertIn("memory_watch_service_handle_command(", source)
        self.assertIn("network_service_is_service_ready()", source)
        self.assertNotIn("static volatile", source)

    def test_service_keeps_official_chat_and_ui_boundaries(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertNotIn("official_chat", source)
        self.assertNotIn("official_chat", header)
        self.assertNotIn("lvgl", source.lower())
        self.assertNotIn("lv_obj", source)
        self.assertNotIn("ai_chat_view", source)

    def test_main_cmake_registers_memory_watch_service(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/memory_watch_service.c",
            cmake,
        )


if __name__ == "__main__":
    unittest.main()
