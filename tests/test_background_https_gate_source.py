import unittest

from tests.main_paths import BACKGROUND_HTTPS_GATE_HEADER
from tests.main_paths import BACKGROUND_HTTPS_GATE_SOURCE
from tests.main_paths import HPTTS_SOURCE
from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MEMORY_WATCH_VOICE_CLIENT_SOURCE


class BackgroundHttpsGateSourceTests(unittest.TestCase):
    def test_gate_header_exposes_low_priority_reasons(self) -> None:
        header = BACKGROUND_HTTPS_GATE_HEADER.read_text(encoding="utf-8")

        self.assertIn("background_https_gate_reason_t", header)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_HEALTH", header)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_SYNC", header)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_INBOX", header)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_MARK_READ", header)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_WEATHER", header)
        self.assertIn("background_https_gate_acquire", header)
        self.assertIn("background_https_gate_release", header)
        self.assertIn("background_https_gate_quiet_for", header)
        self.assertIn("前台", header)

    def test_gate_is_token_only_not_network_worker(self) -> None:
        source = BACKGROUND_HTTPS_GATE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("xSemaphoreCreateBinaryStatic", source)
        self.assertIn("xSemaphoreTake(s_token, wait_ticks)", source)
        self.assertIn("xSemaphoreGive(s_token)", source)
        self.assertIn("s_quiet_until_us", source)
        self.assertIn("esp_timer_get_time()", source)

        forbidden_tokens = (
            "esp_http_client",
            "xTaskCreate",
            "vTaskDelete",
            "esp_wifi",
            "ble_presence",
            "memory_watch_service",
        )
        for token in forbidden_tokens:
            self.assertNotIn(token, source)

    def test_background_http_call_sites_are_gated(self) -> None:
        client_source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")
        weather_source = HPTTS_SOURCE.read_text(encoding="utf-8")
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/background_https_gate.c",
            cmake,
        )
        self.assertIn('#include "services/background_https_gate.h"', client_source)
        self.assertIn("memory_watch_voice_client_perform_background_http_json", client_source)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_HEALTH", client_source)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_SYNC", client_source)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_INBOX", client_source)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_MEMORY_WATCH_MARK_READ", client_source)
        self.assertIn('#include "services/background_https_gate.h"', weather_source)
        self.assertIn("BACKGROUND_HTTPS_GATE_REASON_WEATHER", weather_source)

        voice_body = client_source.split(
            "esp_err_t memory_watch_voice_client_post_voice_command", 1
        )[1].split("esp_err_t memory_watch_voice_client_post_text_command", 1)[0]
        cancel_body = client_source.split(
            "esp_err_t memory_watch_voice_client_cancel_request", 1
        )[1].split("esp_err_t memory_watch_voice_client_post_voice_command", 1)[0]
        text_body = client_source.split(
            "esp_err_t memory_watch_voice_client_post_text_command", 1
        )[1].split("/* ── 以下为 inbox 窄 HTTP client", 1)[0]

        self.assertNotIn("memory_watch_voice_client_perform_background_http_json", voice_body)
        self.assertNotIn("memory_watch_voice_client_perform_background_http_json", cancel_body)
        self.assertNotIn("memory_watch_voice_client_perform_background_http_json", text_body)


if __name__ == "__main__":
    unittest.main()
