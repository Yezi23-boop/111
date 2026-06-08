import unittest

from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MEMORY_WATCH_VOICE_CLIENT_HEADER
from tests.main_paths import MEMORY_WATCH_VOICE_CLIENT_SOURCE


class MemoryWatchVoiceClientSourceTests(unittest.TestCase):
    def test_voice_client_header_and_source_exist(self) -> None:
        self.assertTrue(MEMORY_WATCH_VOICE_CLIENT_HEADER.exists())
        self.assertTrue(MEMORY_WATCH_VOICE_CLIENT_SOURCE.exists())

    def test_voice_client_exposes_runtime_config_and_v1_response(self) -> None:
        header = MEMORY_WATCH_VOICE_CLIENT_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_voice_client_config_t", header)
        self.assertIn("const char *base_url", header)
        self.assertIn("const char *device_id", header)
        self.assertIn("const char *device_token", header)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_DEFAULT_TIMEOUT_MS 120000U", header)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES", header)
        self.assertIn("memory_watch_voice_client_request_t", header)
        self.assertIn("request_id", header)
        self.assertIn("const uint8_t *audio", header)
        self.assertIn("audio_len", header)
        self.assertIn("has_battery_percent", header)
        self.assertIn("has_charging", header)
        self.assertIn("has_rssi", header)
        self.assertIn("memory_watch_voice_client_response_t", header)
        self.assertIn("asr_text", header)
        self.assertIn("reply_text", header)
        self.assertIn("clarification_id", header)
        self.assertIn("error_code", header)
        self.assertIn("memory_watch_voice_client_health_t", header)
        self.assertIn("hermes_status", header)
        self.assertIn("memory_watch_voice_client_get_health", header)
        self.assertIn("memory_watch_voice_client_post_voice_command", header)
        self.assertIn("memory_watch_voice_client_cancel_request", header)

    def test_voice_client_posts_contract_multipart_fields(self) -> None:
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "esp_http_client.h"', source)
        self.assertIn('#include "esp_crt_bundle.h"', source)
        self.assertIn('#include "cJSON.h"', source)
        self.assertIn('"/v1/watch/voice-command"', source)
        self.assertIn('"multipart/form-data; boundary=%s"', source)
        self.assertIn('"Authorization"', source)
        self.assertIn('"Bearer "', source)
        self.assertIn('"Content-Type: audio/ogg', source)
        for field in [
            "request_id",
            "device_id",
            "clarification_id",
            "battery_percent",
            "charging",
            "rssi",
            "firmware_version",
            "locale",
            "timezone",
            "source",
            "ui_state",
        ]:
            self.assertIn(f'"{field}"', source)
        self.assertIn("zh-CN", source)
        self.assertIn("Asia/Shanghai", source)
        self.assertIn("watch_hermes_page", source)

    def test_voice_client_supports_health_and_cancel_contract(self) -> None:
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('"/v1/watch/health?device_id="', source)
        self.assertIn('"/v1/watch/request/"', source)
        self.assertIn('"/cancel"', source)
        self.assertIn("memory_watch_voice_client_build_health_path", source)
        self.assertIn("memory_watch_voice_client_build_cancel_path", source)
        self.assertIn("memory_watch_voice_client_append_url_encoded", source)
        self.assertIn("HTTP_METHOD_GET", source)
        self.assertIn("HTTP_METHOD_POST", source)
        self.assertIn("memory_watch_voice_client_write_cancel_body", source)
        self.assertIn('"device_id"', source)
        self.assertIn("memory_watch_voice_client_parse_health", source)
        self.assertIn("cJSON_GetArraySize(root) != 3", source)
        self.assertIn('root, "hermes_status"', source)
        self.assertIn('strcmp(out_health->device_id, expected_device_id)', source)
        for value in ["ok", "offline", "online"]:
            self.assertIn(f'"{value}"', source)

    def test_voice_client_validates_limits_and_parses_exact_watch_json(self) -> None:
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_REQUEST_ID_MAX_LEN", source)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES", source)
        self.assertIn("memory_watch_voice_client_is_request_id_valid", source)
        self.assertIn("cJSON_ParseWithLength", source)
        self.assertIn("cJSON_GetArraySize(root) != 7", source)
        self.assertIn("cJSON_GetObjectItemCaseSensitive", source)
        self.assertIn('root, "request_id"', source)
        self.assertIn('root, "status"', source)
        self.assertIn('root, "action"', source)
        self.assertIn('root, "asr_text"', source)
        self.assertIn('root, "reply_text"', source)
        self.assertIn('root, "clarification_id"', source)
        self.assertIn('root, "error_code"', source)
        self.assertIn("cJSON_IsNull", source)
        self.assertIn("strncpy", source)
        self.assertIn("memory_watch_voice_client_is_status_allowed", source)
        self.assertIn("memory_watch_voice_client_is_action_allowed", source)
        self.assertIn('strcmp(out_response->request_id, expected_request_id)', source)
        for status in ["done", "error", "timeout", "canceled"]:
            self.assertIn(f'"{status}"', source)
        for action in [
            "memory_saved",
            "reminder_created",
            "question_answered",
            "clarification_needed",
            "no_action",
            "error",
        ]:
            self.assertIn(f'"{action}"', source)

    def test_voice_client_streams_multipart_without_copying_audio_body(self) -> None:
        header = MEMORY_WATCH_VOICE_CLIENT_HEADER.read_text(encoding="utf-8")
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("memory_watch_voice_client_compute_body_len", source)
        self.assertIn("memory_watch_voice_client_write_body", source)
        self.assertIn("memory_watch_voice_client_write_audio_part", source)
        self.assertIn("memory_watch_voice_client_write_all(client, audio, audio_len)", source)
        self.assertNotIn("memory_watch_voice_client_body_t", source)
        self.assertNotIn("memory_watch_voice_client_make_body", source)
        self.assertIn("只允许上传 worker task 调用", header)
        self.assertIn("不得在 owner task 内直接等待 120 秒 HTTP 响应", header)

    def test_voice_client_keeps_secret_and_owner_boundaries(self) -> None:
        header = MEMORY_WATCH_VOICE_CLIENT_HEADER.read_text(encoding="utf-8")
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")
        combined = source + "\n" + header

        self.assertNotIn("HERMES_API_KEY", combined)
        self.assertNotIn("API_SERVER_KEY", combined)
        self.assertNotIn("XIAOMI_API_KEY", combined)
        self.assertNotIn("/v1/responses", combined)
        self.assertNotIn("9119", combined)
        self.assertNotIn("8642", combined)
        self.assertNotIn("Hermes Dashboard", combined)
        self.assertNotIn("official_chat", combined)
        self.assertNotIn('#include "lvgl', combined.lower())
        self.assertNotIn("lv_obj", combined)
        self.assertNotIn("lv_timer", combined)

    def test_main_cmake_registers_voice_client_without_new_dependency(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/memory_watch_voice_client.c",
            cmake,
        )
        self.assertIn("esp_http_client", cmake)
        self.assertIn("esp-tls", cmake)
        self.assertIn("json", cmake)


if __name__ == "__main__":
    unittest.main()
