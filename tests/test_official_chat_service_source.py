import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
OFFICIAL_CHAT_HEADER = REPO_ROOT / "components" / "official_chat" / "include" / "official_chat.h"
OFFICIAL_CHAT_APP = REPO_ROOT / "components" / "official_chat" / "application.cc"
SERVICE_HEADER = REPO_ROOT / "main" / "official_chat_service.h"
SERVICE_SOURCE = REPO_ROOT / "main" / "official_chat_service.c"
MAIN_ENTRY_SOURCE = REPO_ROOT / "main" / "111.c"
AI_UI_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.c"
AI_EXPERIMENT_UI_SOURCE = REPO_ROOT / "main" / "ai_experiment_ui.c"


class OfficialChatServiceSourceTests(unittest.TestCase):
    def test_service_header_and_source_exist(self) -> None:
        self.assertTrue(SERVICE_HEADER.exists())
        self.assertTrue(SERVICE_SOURCE.exists())

    def test_service_wraps_official_chat_lifecycle(self) -> None:
        source = SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "official_chat.h"', source)
        self.assertIn("official_chat_create(", source)
        self.assertIn("official_chat_destroy(", source)
        self.assertIn("official_chat_set_event_callback(", source)
        self.assertIn("official_chat_start(", source)
        self.assertIn("network_service_is_service_ready()", source)
        self.assertIn("official_chat_service_enter_foreground(", source)
        self.assertIn("official_chat_service_shutdown(", source)
        self.assertIn("s_foreground_requested = false;", source)
        self.assertIn("s_shutdown_requested", source)
        self.assertIn("s_shutdown_stop_requested", source)
        self.assertIn("s_shutdown_destroy_deadline_ticks", source)
        self.assertIn("kShutdownTransportQuietPeriodMs", source)
        self.assertIn("xTaskAbortDelay(", source)
        self.assertIn("official_chat_get_state(", source)
        self.assertIn("official_chat_stop_listening(", source)
        self.assertIn("official_chat_prepare_shutdown(", source)
        self.assertIn("OFFICIAL_CHAT_EVENT_USER_TEXT", source)
        self.assertIn("OFFICIAL_CHAT_EVENT_ASSISTANT_TEXT", source)
        self.assertIn("official_chat_service_get_message_count(", source)
        self.assertIn("official_chat_service_get_message(", source)
        self.assertIn("official_chat_service_get_last_user_text(", source)
        self.assertIn("official_chat_service_get_last_assistant_text(", source)

    def test_service_exposes_small_chat_message_queue(self) -> None:
        header = SERVICE_HEADER.read_text(encoding="utf-8")
        source = SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("official_chat_service_message_role_t", header)
        self.assertIn("official_chat_service_message_t", header)
        self.assertIn("size_t official_chat_service_get_message_count(void);",
                      header)
        self.assertIn("esp_err_t official_chat_service_get_message(",
                      header)
        self.assertIn("void official_chat_service_request_shutdown(void);",
                      header)
        self.assertIn("bool official_chat_service_is_shutdown_pending(void);",
                      header)
        self.assertIn("esp_err_t official_chat_service_shutdown(void);", header)
        self.assertIn("s_message_history", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT", source)
        self.assertIn("memmove(", source)
        self.assertIn("s_last_user_text", source)
        self.assertIn("s_last_assistant_text", source)
        self.assertIn("memset(s_message_history, 0, sizeof(s_message_history));",
                      source)
        self.assertIn("s_message_count = 0;", source)
        self.assertIn("s_last_error = ESP_OK;", source)
        self.assertIn("s_chat_handle = NULL;", source)
        self.assertIn("official_chat_set_event_callback(chat_handle, NULL, NULL);",
                      source)
        self.assertIn("ESP_ERR_TIMEOUT", source)
        self.assertIn("OFFICIAL_CHAT_STATE_SPEAKING", source)
        self.assertIn("OFFICIAL_CHAT_STATE_LISTENING", source)
        self.assertIn("OFFICIAL_CHAT_STATE_CONNECTING", source)
        self.assertIn("xTaskGetTickCount()", source)
        self.assertIn("pdMS_TO_TICKS(kShutdownTransportQuietPeriodMs)", source)
        self.assertIn("s_service_state = OFFICIAL_CHAT_SERVICE_STATE_STOPPED;",
                      source)

    def test_service_shutdown_waits_transport_quiet_period_for_idle_session(self) -> None:
        source = SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("official_chat_service_requires_shutdown_quiet_period(",
                      source)
        self.assertIn("OFFICIAL_CHAT_STATE_IDLE", source)
        self.assertIn("shutdown transport quiet period armed", source)
        self.assertIn("if (official_chat_service_requires_shutdown_quiet_period(",
                      source)
        self.assertIn("shutdown waiting for idle before destroy", source)
        self.assertIn("shutdown reached idle, arming destroy quiet period",
                      source)

    def test_official_chat_public_event_surface_exposes_text_events(self) -> None:
        header = OFFICIAL_CHAT_HEADER.read_text(encoding="utf-8")
        app = OFFICIAL_CHAT_APP.read_text(encoding="utf-8")

        self.assertIn("OFFICIAL_CHAT_EVENT_USER_TEXT", header)
        self.assertIn("OFFICIAL_CHAT_EVENT_ASSISTANT_TEXT", header)
        self.assertIn('EmitMessageEvent(OFFICIAL_CHAT_EVENT_USER_TEXT', app)
        self.assertIn('EmitMessageEvent(OFFICIAL_CHAT_EVENT_ASSISTANT_TEXT', app)

    def test_formal_entry_initializes_official_chat_service(self) -> None:
        source = MAIN_ENTRY_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "official_chat_service.h"', source)
        self.assertIn("official_chat_service_init()", source)

    def test_ai_ui_controller_uses_service_for_auto_start_and_status(self) -> None:
        source = AI_UI_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ai_chat_view.h"', source)
        self.assertIn('#include "official_chat_service.h"', source)
        self.assertIn("official_chat_service_enter_foreground()", source)
        self.assertIn("official_chat_service_get_state()", source)
        self.assertIn("ai_chat_view_reload_messages(", source)

    def test_ai_experiment_ui_reads_cached_conversation_text(self) -> None:
        source = AI_EXPERIMENT_UI_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ai_chat_view.h"', source)
        self.assertIn("ai_chat_view_reload_messages(", source)


if __name__ == "__main__":
    unittest.main()
