import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import OFFICIAL_CHAT_SERVICE_HEADER
from tests.main_paths import OFFICIAL_CHAT_SERVICE_SOURCE
from tests.main_paths import REPO_ROOT

OFFICIAL_CHAT_HEADER = REPO_ROOT / "components" / "official_chat" / "include" / "official_chat.h"
OFFICIAL_CHAT_APP = REPO_ROOT / "components" / "official_chat" / "application.cc"
AI_UI_SOURCE = REPO_ROOT / "main" / "ui" / "custom" / "ai_ui_controller.c"


class OfficialChatServiceSourceTests(unittest.TestCase):
    def test_service_header_and_source_exist(self) -> None:
        self.assertTrue(OFFICIAL_CHAT_SERVICE_HEADER.exists())
        self.assertTrue(OFFICIAL_CHAT_SERVICE_SOURCE.exists())

    def test_service_wraps_official_chat_lifecycle(self) -> None:
        source = OFFICIAL_CHAT_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "official_chat.h"', source)
        self.assertIn('#include "services/runtime/runtime_coordinator.h"', source)
        self.assertIn('#include "services/runtime/safety_monitor_policy.h"', source)
        self.assertIn("official_chat_create(", source)
        self.assertIn("official_chat_destroy(", source)
        self.assertIn("official_chat_set_event_callback(", source)
        self.assertIn("official_chat_start(", source)
        self.assertIn("network_service_is_service_ready()", source)
        self.assertIn("official_chat_service_enter_foreground(", source)
        self.assertIn("official_chat_service_shutdown(", source)
        self.assertIn("s_shutdown_requested", source)
        self.assertIn("s_shutdown_stop_requested", source)
        self.assertIn("s_shutdown_destroy_deadline_ticks", source)
        self.assertIn("kShutdownTransportQuietPeriodMs", source)
        self.assertIn("xTaskAbortDelay(", source)
        self.assertIn("xQueueCreateStatic(", source)
        self.assertIn("xQueueSend(", source)
        self.assertIn("xQueueReceive(", source)
        self.assertIn("official_chat_service_handle_command(", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_CMD_ENTER_FOREGROUND", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_CMD_LEAVE_FOREGROUND_AND_STOP",
                      source)
        self.assertIn("RUNTIME_COORDINATOR_PARTICIPANT_OFFICIAL_CHAT", source)
        self.assertIn("runtime_coordinator_request_foreground(", source)
        self.assertIn("runtime_coordinator_report_start_result(", source)
        self.assertIn("runtime_coordinator_report_quiesce_result(", source)
        self.assertIn("official_chat_get_state(", source)
        self.assertIn("official_chat_stop_listening(", source)
        self.assertIn("official_chat_prepare_shutdown(", source)
        self.assertIn("OFFICIAL_CHAT_EVENT_USER_TEXT", source)
        self.assertIn("OFFICIAL_CHAT_EVENT_ASSISTANT_TEXT", source)
        self.assertIn("official_chat_service_get_message_count(", source)
        self.assertIn("official_chat_service_get_message(", source)
        self.assertIn("official_chat_service_get_last_user_text(", source)
        self.assertIn("official_chat_service_get_last_assistant_text(", source)
        self.assertIn(
            "safety_monitor_policy_set_foreground_audio_active(\n"
            "        active, reason)",
            source,
        )
        self.assertIn(
            "official_chat_service_set_foreground_audio_active(\n"
            '                true, "official_chat")',
            source,
        )
        self.assertIn(
            'official_chat_service_set_foreground_audio_active(false, "official_chat")',
            source,
        )

    def test_service_exposes_small_chat_message_queue(self) -> None:
        header = OFFICIAL_CHAT_SERVICE_HEADER.read_text(encoding="utf-8")
        source = OFFICIAL_CHAT_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("official_chat_service_message_role_t", header)
        self.assertIn("official_chat_service_message_t", header)
        self.assertIn("size_t official_chat_service_get_message_count(void);",
                      header)
        self.assertIn("esp_err_t official_chat_service_get_message(",
                      header)
        self.assertIn("void official_chat_service_request_shutdown(void);",
                      header)
        self.assertIn("void official_chat_service_leave_foreground(void);",
                      header)
        self.assertIn("等价于 `official_chat_service_leave_foreground()`",
                      header)
        self.assertIn("bool official_chat_service_is_shutdown_pending(void);",
                      header)
        self.assertIn("esp_err_t official_chat_service_shutdown(void);", header)
        self.assertIn("audio_channel_ready", header)
        self.assertIn("esp_err_t official_chat_service_prepare_audio_channel(void);",
                      header)
        self.assertIn("esp_err_t official_chat_service_start_listening(void);",
                      header)
        self.assertIn("esp_err_t official_chat_service_stop_listening(void);",
                      header)
        self.assertIn("official_chat_service_snapshot_t", header)
        self.assertIn("esp_err_t official_chat_service_get_snapshot(",
                      header)
        self.assertIn("s_message_history", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_USER", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_MESSAGE_ROLE_ASSISTANT", source)
        self.assertIn("memmove(", source)
        self.assertIn("s_last_user_text", source)
        self.assertIn("s_last_assistant_text", source)
        self.assertIn("memset(s_message_history, 0, sizeof(s_message_history));",
                      source)
        self.assertIn("s_message_count = 0;", source)
        self.assertIn("official_chat_service_set_last_error(ESP_OK);",
                      source)
        self.assertIn("s_chat_handle = NULL;", source)
        self.assertIn("official_chat_set_event_callback(chat_handle, NULL, NULL);",
                      source)
        self.assertIn("ESP_ERR_TIMEOUT", source)
        self.assertIn("OFFICIAL_CHAT_STATE_SPEAKING", source)
        self.assertIn("OFFICIAL_CHAT_STATE_LISTENING", source)
        self.assertIn("OFFICIAL_CHAT_STATE_CONNECTING", source)
        self.assertIn("xTaskGetTickCount()", source)
        self.assertIn("pdMS_TO_TICKS(kShutdownTransportQuietPeriodMs)", source)
        self.assertIn("official_chat_service_set_state(OFFICIAL_CHAT_SERVICE_STATE_STOPPED);",
                      source)

    def test_service_exposes_press_to_talk_commands_with_release_wait(self) -> None:
        source = OFFICIAL_CHAT_SERVICE_SOURCE.read_text(encoding="utf-8")
        ui_source = AI_UI_SOURCE.read_text(encoding="utf-8")

        self.assertIn("OFFICIAL_CHAT_SERVICE_CMD_PREPARE_AUDIO_CHANNEL", source)
        self.assertIn("official_chat_prepare_audio_channel(s_chat_handle)",
                      source)
        self.assertIn("official_chat_is_audio_channel_ready(s_chat_handle)",
                      source)
        self.assertIn("official_chat_service_prepare_audio_channel(void)",
                      source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_CMD_START_LISTENING", source)
        self.assertIn("OFFICIAL_CHAT_SERVICE_CMD_STOP_LISTENING", source)
        self.assertIn("official_chat_start_listening(s_chat_handle)", source)
        self.assertIn("official_chat_stop_listening(s_chat_handle)", source)
        self.assertIn(
            "OFFICIAL_CHAT_SERVICE_CMD_STOP_LISTENING, pdMS_TO_TICKS(50)",
            source,
        )
        self.assertIn("return ret;", source)
        self.assertIn("official_chat_service_get_snapshot(&chat_snapshot)",
                      ui_source)
        self.assertIn("official_chat_service_prepare_audio_channel();",
                      ui_source)
        self.assertIn("chat_snapshot.audio_channel_ready", ui_source)
        self.assertIn("official_chat_service_start_listening();", ui_source)
        self.assertIn("official_chat_service_stop_listening();", ui_source)
        self.assertIn("ai_chat_view_set_voice_button_visible(", ui_source)
        self.assertNotIn("network_service_request_portal()", ui_source)

    def test_service_releases_partial_session_when_start_fails(self) -> None:
        source = OFFICIAL_CHAT_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "official_chat_destroy(s_chat_handle);\n"
            "        s_chat_handle = NULL;\n"
            "        official_chat_service_set_last_error(ret);",
            source,
        )
        self.assertIn(
            "official_chat_set_event_callback(s_chat_handle, NULL, NULL);\n"
            "        official_chat_destroy(s_chat_handle);\n"
            "        s_chat_handle = NULL;",
            source,
        )

    def test_coordinator_ack_is_reported_after_owner_work(self) -> None:
        source = OFFICIAL_CHAT_SERVICE_SOURCE.read_text(encoding="utf-8")

        enter_section = source.split(
            "case OFFICIAL_CHAT_SERVICE_CMD_ENTER_FOREGROUND:"
        )[1].split("case OFFICIAL_CHAT_SERVICE_CMD_LEAVE_FOREGROUND_AND_STOP:", 1)[0]
        self.assertIn("runtime_coordinator_request_foreground(", enter_section)
        self.assertIn("s_coordinator_request_generation = request_generation", enter_section)

        shutdown_section = source.split("if (s_shutdown_requested)", 1)[1].split(
            "if (!s_foreground_requested)", 1
        )[0]
        self.assertIn("official_chat_destroy(chat_handle)", shutdown_section)
        self.assertIn("runtime_coordinator_report_quiesce_result(", shutdown_section)
        self.assertLess(
            shutdown_section.index("official_chat_destroy(chat_handle)"),
            shutdown_section.index("runtime_coordinator_report_quiesce_result("),
        )
        self.assertNotIn("foreground_runtime_gate", source)

    def test_ui_exit_is_frontend_leave_not_blocking_shutdown(self) -> None:
        ui_source = AI_UI_SOURCE.read_text(encoding="utf-8")

        self.assertIn("official_chat_service_leave_foreground();", ui_source)
        self.assertNotIn("official_chat_service_shutdown();", ui_source)

    def test_service_uses_snapshot_and_queue_instead_of_volatile_flag_protocol(self) -> None:
        header = OFFICIAL_CHAT_SERVICE_HEADER.read_text(encoding="utf-8")
        source = OFFICIAL_CHAT_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("foreground_active", header)
        self.assertIn("stop_pending", header)
        self.assertIn("last_error", header)
        self.assertIn("portMUX_TYPE s_snapshot_lock", source)
        self.assertIn("official_chat_service_copy_snapshot(", source)
        self.assertIn("official_chat_service_set_lifecycle_intent(",
                      source)
        self.assertNotIn("static volatile", source)

    def test_service_shutdown_waits_transport_quiet_period_for_idle_session(self) -> None:
        source = OFFICIAL_CHAT_SERVICE_SOURCE.read_text(encoding="utf-8")

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
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/official_chat_service.h"', source)
        self.assertIn("official_chat_service_init()", source)

    def test_ai_ui_controller_uses_service_for_auto_start_and_status(self) -> None:
        source = AI_UI_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ai_chat_view.h"', source)
        self.assertIn('#include "services/official_chat_service.h"', source)
        self.assertIn("official_chat_service_enter_foreground()", source)
        self.assertIn("official_chat_service_get_state()", source)
        self.assertIn("ai_chat_view_reload_messages(", source)

if __name__ == "__main__":
    unittest.main()
