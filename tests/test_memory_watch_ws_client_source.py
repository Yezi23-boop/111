import unittest

from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MAIN_KCONFIG
from tests.main_paths import MEMORY_WATCH_SERVICE_SOURCE
from tests.main_paths import MEMORY_WATCH_WS_CLIENT_HEADER
from tests.main_paths import MEMORY_WATCH_WS_CLIENT_SOURCE


class MemoryWatchWsClientSourceTests(unittest.TestCase):
    def test_ws_client_header_and_source_exist(self) -> None:
        self.assertTrue(MEMORY_WATCH_WS_CLIENT_HEADER.exists())
        self.assertTrue(MEMORY_WATCH_WS_CLIENT_SOURCE.exists())

    def test_ws_client_exposes_narrow_c_api(self) -> None:
        header = MEMORY_WATCH_WS_CLIENT_HEADER.read_text(encoding="utf-8")

        self.assertIn('MEMORY_WATCH_WS_PATH "/v1/watch/ws"', header)
        self.assertIn("memory_watch_ws_event_kind_t", header)
        self.assertIn("MEMORY_WATCH_WS_EVENT_REQUEST_ACCEPTED", header)
        self.assertIn("MEMORY_WATCH_WS_EVENT_TURN_ASR_READY", header)
        self.assertIn("MEMORY_WATCH_WS_EVENT_TURN_REPLY_MESSAGE", header)
        self.assertIn("MEMORY_WATCH_WS_EVENT_TURN_ERROR", header)
        self.assertIn("memory_watch_ws_event_t", header)
        self.assertIn("kind", header)
        self.assertIn("memory_watch_ws_client_config_t", header)
        self.assertIn("memory_watch_ws_client_connect", header)
        self.assertIn("memory_watch_ws_client_send_audio_start", header)
        self.assertIn("memory_watch_ws_client_send_audio_chunk", header)
        self.assertIn("memory_watch_ws_client_send_audio_end", header)
        self.assertIn("memory_watch_ws_client_send_audio_turn", header)
        self.assertIn("memory_watch_ws_client_send_ack", header)
        self.assertIn("memory_watch_ws_client_close", header)
        self.assertIn("memory_watch_ws_client_is_connected", header)

    def test_ws_client_uses_json_control_and_binary_audio_frames(self) -> None:
        source = MEMORY_WATCH_WS_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "official_chat_websocket_transport.h"', source)
        self.assertNotIn('#include "net/websocket_client.h"', source)
        self.assertIn("official_chat::WebsocketTransport", source)
        self.assertIn('"type"', source)
        for event_type in [
            "auth",
            "audio_start",
            "audio_end",
            "ack",
            "conversation",
        ]:
            self.assertIn(event_type, source)
        self.assertIn('"ogg_opus"', source)
        self.assertIn("g_ws->Send(rendered)", source)
        self.assertIn("g_ws->Send(reinterpret_cast<const char *>(audio), audio_len, true)", source)
        self.assertIn("OnData", source)
        self.assertIn("DispatchJson", source)
        self.assertIn("MapEventKind", source)
        self.assertIn('type == "request_accepted"', source)
        self.assertIn("MEMORY_WATCH_WS_EVENT_REQUEST_ACCEPTED", source)
        self.assertIn("MEMORY_WATCH_WS_EVENT_TURN_ASR_READY", source)
        self.assertIn("MEMORY_WATCH_WS_EVENT_TURN_REPLY_MESSAGE", source)
        self.assertIn("MEMORY_WATCH_WS_EVENT_TURN_ERROR", source)
        self.assertIn("memory_watch_ws_client_send_audio_turn", source)

    def test_ws_client_builds_url_from_watch_endpoint_and_rejects_insecure_by_default(self) -> None:
        source = MEMORY_WATCH_WS_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('"https://"', source)
        self.assertIn('"wss://"', source)
        self.assertIn('"http://"', source)
        self.assertIn('"ws://"', source)
        self.assertIn("!config->allow_insecure_http", source)
        self.assertIn("MEMORY_WATCH_WS_PATH", source)

    def test_ws_client_keeps_secret_and_ui_boundaries(self) -> None:
        combined = (
            MEMORY_WATCH_WS_CLIENT_SOURCE.read_text(encoding="utf-8")
            + "\n"
            + MEMORY_WATCH_WS_CLIENT_HEADER.read_text(encoding="utf-8")
        )

        self.assertNotIn("HERMES_API_KEY", combined)
        self.assertNotIn("API_SERVER_KEY", combined)
        self.assertNotIn("XIAOMI_API_KEY", combined)
        self.assertNotIn("/v1/responses", combined)
        self.assertNotIn("9119", combined)
        self.assertNotIn("8642", combined)
        self.assertNotIn('#include "lvgl', combined.lower())
        self.assertNotIn("lv_obj", combined)
        self.assertNotIn("wake_word", combined)
        self.assertNotIn("SendMcpMessage", combined)

    def test_main_cmake_and_kconfig_register_ws_client(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        kconfig = MAIN_KCONFIG.read_text(encoding="utf-8")

        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/memory_watch_ws_client.cc",
            cmake,
        )
        self.assertIn("official_chat", cmake)
        self.assertIn("config MEMORY_WATCH_WEBSOCKET_ENABLED", kconfig)
        self.assertIn("default y", kconfig)

    def test_service_uses_ws_only_inside_upload_worker_with_http_fallback(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/memory_watch_ws_client.h"', source)
        self.assertIn("StaticEventGroup_t s_ws_wait_event_buffer", source)
        self.assertIn("xEventGroupCreateStatic", source)
        self.assertIn("memory_watch_service_send_voice_over_ws", source)
        self.assertIn("memory_watch_ws_client_send_audio_turn", source)
        self.assertNotIn("memory_watch_ws_client_send_audio_start(job->request_id)", source)
        self.assertNotIn("memory_watch_ws_client_send_audio_chunk(", source)
        self.assertNotIn("memory_watch_ws_client_send_audio_end(job->request_id)", source)
        self.assertIn("kWsAudioChunkBytes = 16U * 1024U", source)
        self.assertIn("#if CONFIG_MEMORY_WATCH_WEBSOCKET_ENABLED", source)
        self.assertIn("memory_watch_voice_client_post_voice_command", source)
        self.assertIn("voice-ws-done", source)
        self.assertIn("voice-http-done", source)

        upload_worker = source.split(
            "static void memory_watch_service_upload_worker_task"
        )[1].split("static void memory_watch_service_health_worker_task")[0]
        self.assertIn("memory_watch_service_send_voice_over_ws", upload_worker)
        self.assertIn("memory_watch_voice_client_post_voice_command", upload_worker)

        handle_send = source.split(
            "static void memory_watch_service_handle_send_recording"
        )[1].split("static void memory_watch_service_handle_cancel_recording")[0]
        self.assertNotIn("memory_watch_service_send_voice_over_ws", handle_send)
        self.assertNotIn("memory_watch_voice_client_post_voice_command", handle_send)

    def test_service_closes_ws_after_pending_request_settles(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        ws_section = source.split(
            "static esp_err_t memory_watch_service_send_voice_over_ws"
        )[1].split("static esp_err_t memory_watch_service_post_worker_result", 1)[0]

        self.assertIn("xEventGroupWaitBits", ws_section)
        self.assertIn("kWsWaitConversationBit", ws_section)
        self.assertIn("kWsWaitErrorBit", ws_section)
        self.assertIn("kWsWaitDisconnectedBit", ws_section)
        self.assertIn("kWsWaitRequestAcceptedBit", ws_section)
        self.assertIn("server_accepted_seen", ws_section)
        disconnected_branch = ws_section.split(
            "if ((bits & kWsWaitDisconnectedBit) != 0)", 1
        )[1].split("if (asr_ready_seen)", 1)[0]
        self.assertIn("server_accepted_seen || asr_ready_seen", disconnected_branch)
        self.assertIn("memory_watch_ws_client_close();", ws_section)
        wait_tail = ws_section.split("xEventGroupWaitBits", 1)[1]
        self.assertIn("memory_watch_ws_client_close();", wait_tail)
        self.assertLess(
            wait_tail.index("memory_watch_ws_client_close();"),
            wait_tail.index("if ((bits & kWsWaitConversationBit) != 0)"),
        )


if __name__ == "__main__":
    unittest.main()
