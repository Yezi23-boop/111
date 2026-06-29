import unittest

from tests.main_paths import FOREGROUND_RUNTIME_GATE_HEADER
from tests.main_paths import FOREGROUND_RUNTIME_GATE_SOURCE
from tests.main_paths import MAIN_CMAKE


class ForegroundRuntimeGateSourceTests(unittest.TestCase):
    def test_gate_header_exposes_minimal_owner_api(self) -> None:
        header = FOREGROUND_RUNTIME_GATE_HEADER.read_text(encoding="utf-8")

        self.assertIn("foreground_runtime_owner_t", header)
        self.assertIn("FOREGROUND_RUNTIME_OWNER_NONE", header)
        self.assertIn("FOREGROUND_RUNTIME_OWNER_HERMES", header)
        self.assertIn("FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING", header)
        self.assertIn("FOREGROUND_RUNTIME_OWNER_OTA", header)
        self.assertIn("FOREGROUND_RUNTIME_OWNER_FUTURE_PAGE", header)
        self.assertIn("foreground_runtime_gate_acquire", header)
        self.assertIn("foreground_runtime_gate_release", header)
        self.assertIn("foreground_runtime_gate_is_active", header)
        self.assertIn("foreground_runtime_gate_current_owner", header)
        self.assertIn("foreground_runtime_gate_quiet_for", header)
        self.assertIn("foreground_runtime_gate_is_quiet", header)
        self.assertIn("ESP-DL / 安全检测属于可抢占", header)

    def test_gate_is_state_only_not_a_resource_manager(self) -> None:
        source = FOREGROUND_RUNTIME_GATE_SOURCE.read_text(encoding="utf-8")
        header = FOREGROUND_RUNTIME_GATE_HEADER.read_text(encoding="utf-8")
        combined = source + "\n" + header

        self.assertIn("portMUX_TYPE s_gate_lock", source)
        self.assertIn("s_current_owner", source)
        self.assertIn("s_quiet_until_us", source)
        self.assertIn("esp_timer_get_time()", source)
        self.assertIn("ESP_ERR_INVALID_STATE", source)
        self.assertIn("foreground_runtime_gate_owner_text", source)

        forbidden_tokens = (
            "malloc(",
            "calloc(",
            "heap_caps_malloc",
            "xTaskCreate",
            "vTaskDelete",
            "vTaskSuspend",
            "esp_wifi",
            "ble_presence_start",
            "espdl_audio_runtime",
            "memory_watch_service",
            "callback",
            "QueueHandle_t",
        )
        for token in forbidden_tokens:
            self.assertNotIn(token, combined)

    def test_gate_is_built_with_main_services(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/foreground_runtime_gate.c",
            cmake,
        )


if __name__ == "__main__":
    unittest.main()
