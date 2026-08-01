import unittest

from tests.main_cmake_contract import assert_main_source_globbed
from tests.main_paths import MAIN_CMAKE
from tests.main_paths import RUNTIME_COORDINATOR_HEADER
from tests.main_paths import RUNTIME_COORDINATOR_SOURCE


class RuntimeCoordinatorSourceTests(unittest.TestCase):
    def test_header_exposes_registered_async_protocol(self) -> None:
        header = RUNTIME_COORDINATOR_HEADER.read_text(encoding="utf-8")

        for participant in (
            "HERMES",
            "OFFICIAL_CHAT",
            "NETWORK_PROVISIONING",
            "OTA",
            "SAFETY_MONITOR",
            "BLE_PRESENCE",
        ):
            self.assertIn(
                f"RUNTIME_COORDINATOR_PARTICIPANT_{participant}", header
            )
        self.assertIn("FOREGROUND_EXCLUSIVE", header)
        self.assertIn("BACKGROUND_PREEMPTIBLE", header)
        self.assertIn("runtime_coordinator_register", header)
        self.assertIn("runtime_coordinator_request_foreground", header)
        self.assertIn("runtime_coordinator_cancel_request", header)
        self.assertIn("runtime_coordinator_report_quiesce_result", header)
        self.assertIn("runtime_coordinator_report_start_result", header)
        self.assertIn("runtime_coordinator_get_snapshot", header)

    def test_coordinator_owns_protocol_not_business_resources(self) -> None:
        source = RUNTIME_COORDINATOR_SOURCE.read_text(encoding="utf-8")
        header = RUNTIME_COORDINATOR_HEADER.read_text(encoding="utf-8")
        combined = source + "\n" + header

        self.assertIn("xQueueCreateStatic", source)
        self.assertIn("xQueueReceive", source)
        self.assertIn("xTaskCreateWithCaps", source)
        self.assertIn("MALLOC_CAP_INTERNAL", source)
        self.assertIn("kForegroundTimeoutMs = 5000U", source)
        self.assertIn("kBackgroundTimeoutMs = 2500U", source)
        self.assertIn("request_counter", source)
        self.assertIn("transition_counter", source)
        self.assertIn("provisional_request_generation", source)
        self.assertIn("has_newer_target", source)
        self.assertIn("RUNTIME_COORDINATOR_STATE_DEGRADED_CURRENT", source)
        self.assertIn("RUNTIME_COORDINATOR_STATE_DEGRADED_TARGET", source)

        for token in (
            "OTA_SERVICE_STATE_",
            "network_manager_",
            "official_chat_",
            "memory_watch_service_",
            "danger_detection_service_",
            "esp_wifi_",
        ):
            self.assertNotIn(token, combined)

    def test_coordinator_is_built_and_old_gate_is_gone(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        assert_main_source_globbed(
            self, "services/runtime/runtime_coordinator.c"
        )
        self.assertNotIn("foreground_runtime_gate.c", cmake)


if __name__ == "__main__":
    unittest.main()
