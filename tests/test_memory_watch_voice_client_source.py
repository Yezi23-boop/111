import json
import unittest

from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MEMORY_WATCH_VOICE_CLIENT_HEADER
from tests.main_paths import MEMORY_WATCH_VOICE_CLIENT_SOURCE
from tests.main_paths import REPO_ROOT


WATCH_CONTRACT_V1 = (
    REPO_ROOT / "server" / "watch_voice_endpoint" / "watch_contract.v1.json"
)


def load_watch_contract_v1() -> dict:
    return json.loads(WATCH_CONTRACT_V1.read_text(encoding="utf-8"))


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
        self.assertIn("allow_insecure_http", header)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_DEFAULT_TIMEOUT_MS 120000U", header)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES", header)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_MAX_TEXT_BYTES", header)
        self.assertIn("memory_watch_voice_client_request_t", header)
        self.assertIn("request_id", header)
        self.assertIn("const uint8_t *audio", header)
        self.assertIn("audio_len", header)
        self.assertIn("has_battery_percent", header)
        self.assertIn("has_charging", header)
        self.assertIn("has_rssi", header)
        self.assertIn("memory_watch_voice_client_response_t", header)
        self.assertIn("memory_watch_voice_client_text_request_t", header)
        self.assertIn("asr_text", header)
        self.assertIn("reply_text", header)
        self.assertIn("clarification_id", header)
        self.assertIn("error_code", header)
        self.assertIn("memory_watch_voice_client_health_t", header)
        self.assertIn("hermes_status", header)
        self.assertIn("memory_watch_voice_client_get_health", header)
        self.assertIn("memory_watch_voice_client_post_voice_command", header)
        self.assertIn("memory_watch_voice_client_post_text_command", header)
        self.assertIn("memory_watch_voice_client_post_danger_alert", header)
        self.assertIn("memory_watch_voice_client_cancel_request", header)

    def test_voice_client_posts_contract_multipart_fields(self) -> None:
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "esp_http_client.h"', source)
        self.assertIn('#include "esp_crt_bundle.h"', source)
        self.assertIn('#include "cJSON.h"', source)
        self.assertIn('"/v1/watch/voice-command"', source)
        self.assertIn('"/v1/watch/text-command"', source)
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

    def test_voice_client_posts_text_command_contract_fields(self) -> None:
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_VOICE_CLIENT_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_voice_client_text_request_t", header)
        self.assertIn("const char *text", header)
        self.assertIn("memory_watch_voice_client_validate_text_request", source)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_MAX_TEXT_BYTES", source)
        self.assertIn("memory_watch_voice_client_compute_text_body_len", source)
        self.assertIn("memory_watch_voice_client_write_text_body", source)
        self.assertIn("memory_watch_voice_client_write_text_command_body", source)
        self.assertIn("memory_watch_voice_client_post_text_command", source)
        self.assertIn('"/v1/watch/text-command"', source)
        self.assertIn('"text"', source)
        self.assertIn("memory_watch_voice_client_perform_http_json", source)

    def test_voice_client_posts_danger_alert_contract(self) -> None:
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_VOICE_CLIENT_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_voice_client_danger_alert_request_t", header)
        self.assertIn("memory_watch_voice_client_danger_alert_response_t", header)
        self.assertIn("memory_watch_voice_client_post_danger_alert", header)
        self.assertIn('"/v1/watch/alerts"', source)
        self.assertIn("memory_watch_voice_client_validate_danger_alert_request", source)
        self.assertIn("memory_watch_voice_client_write_json_body", source)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_ALERT", source)
        for field in [
            "device_id",
            "danger_type",
            "danger_prob",
            "alert_sequence",
            "message",
            "firmware_version",
        ]:
            self.assertIn(f'\\"{field}\\":', source)

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
        self.assertIn('strcmp(out_health->status, "ok") != 0', source)
        self.assertIn('strcmp(out_health->hermes_status, "online") != 0', source)
        for value in ["ok", "offline", "online"]:
            self.assertIn(f'"{value}"', source)

    def test_voice_client_validates_limits_and_parses_exact_watch_json(self) -> None:
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_REQUEST_ID_MAX_LEN", source)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES", source)
        self.assertIn("memory_watch_voice_client_is_request_id_valid", source)
        self.assertIn("memory_watch_voice_client_request_id_matches_device", source)
        self.assertIn("request_id[device_len] == '-'", source)
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

    def test_voice_client_matches_server_watch_contract_v1(self) -> None:
        contract = load_watch_contract_v1()
        header = MEMORY_WATCH_VOICE_CLIENT_HEADER.read_text(encoding="utf-8")
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")
        combined = source + "\n" + header

        endpoints = contract["endpoints"]
        self.assertIn(f'"{endpoints["voice_command"]["path"]}"', source)
        self.assertIn(f'"{endpoints["text_command"]["path"]}"', source)
        self.assertIn(
            f'"{endpoints["health"]["path"]}?device_id="',
            source,
        )
        cancel_prefix, cancel_suffix = endpoints["cancel"]["path"].split(
            "{request_id}"
        )
        self.assertIn(f'"{cancel_prefix}"', source)
        self.assertIn(f'"{cancel_suffix}"', source)
        self.assertIn('"Content-Type: audio/ogg', source)

        request = contract["request"]
        self.assertIn(
            f"MEMORY_WATCH_VOICE_CLIENT_REQUEST_ID_MAX_LEN "
            f"{request['request_id_pattern'].split('{1,')[1].split('}')[0]}U",
            header,
        )
        self.assertEqual(request["max_audio_bytes"], 6 * 1024 * 1024)
        self.assertIn(
            "#define MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES "
            "(6U * 1024U * 1024U)",
            header,
        )
        for field in request["voice_command_fields"]:
            if field == "audio":
                self.assertIn('name=\\"audio\\"', source)
            else:
                self.assertIn(f'"{field}"', source)
        self.assertIn(request["locale"], source)
        self.assertIn(request["timezone"], source)
        self.assertIn(request["source"], source)
        self.assertIn(
            f"#define MEMORY_WATCH_VOICE_CLIENT_MAX_TEXT_BYTES "
            f"{request['max_text_chars'] * 2 + 32}U",
            header,
        )
        for field in request["text_command_fields"]:
            self.assertIn(f'"{field}"', source)

        response = contract["response"]
        self.assertIn(
            f"cJSON_GetArraySize(root) != {len(response['required'])}",
            source,
        )
        for field in response["required"]:
            self.assertIn(f'root, "{field}"', source)
        for status in response["status_enum"]:
            self.assertIn(f'"{status}"', source)
        for action in response["action_enum"]:
            self.assertIn(f'"{action}"', source)

        timeouts = contract["timeouts"]
        self.assertIn(
            f"MEMORY_WATCH_VOICE_CLIENT_DEFAULT_TIMEOUT_MS "
            f"{timeouts['watch_wait_seconds'] * 1000}U",
            header,
        )
        for forbidden in contract["security"]["esp32_must_not_store"]:
            self.assertNotIn(forbidden, combined)
        for forbidden in contract["security"]["esp32_must_not_call"]:
            self.assertNotIn(forbidden, combined)

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

    def test_voice_client_rejects_plain_http_by_default(self) -> None:
        source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("uses_http && !config->allow_insecure_http", source)
        self.assertIn("uses_https", source)
        self.assertIn("拒绝明文 HTTP", MEMORY_WATCH_VOICE_CLIENT_HEADER.read_text(encoding="utf-8"))

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
