import unittest

from tests.main_cmake_contract import assert_main_source_globbed
from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import MAIN_KCONFIG
from tests.main_paths import RUNTIME_RESOURCE_GATE_BOARD_TEST_HEADER
from tests.main_paths import RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE


class RuntimeCoordinatorBoardTestSourceTests(unittest.TestCase):
    def test_board_test_is_kconfig_gated_and_default_off(self) -> None:
        kconfig = MAIN_KCONFIG.read_text(encoding="utf-8")
        source = RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE.read_text(encoding="utf-8")

        section = kconfig.split("config RUNTIME_RESOURCE_GATE_BOARD_TEST", 1)[1]
        self.assertIn("default n", section)
        self.assertIn("runtime coordinator board state-machine test", section)
        self.assertIn("#if CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST", source)
        self.assertNotIn("ENABLE_REAL_BLE_TOGGLE", source)

    def test_board_test_exercises_protocol_failure_paths(self) -> None:
        source = RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE.read_text(encoding="utf-8")
        header = RUNTIME_RESOURCE_GATE_BOARD_TEST_HEADER.read_text(encoding="utf-8")

        self.assertIn("runtime_resource_gate_board_test_start", header)
        for marker in (
            "latest_request_wins",
            "provisional_request_covered",
            "stale_ack_ignored",
            "owner_reject",
            "current_owner_timeout",
            "current_owner_late_active",
            "background_timeout",
            "grant_failed",
            "rollback_timeout_degraded_target",
            "degraded_target_released",
            "real_hermes_active",
            "real_hermes_released",
            "real_ota_active",
            "real_ota_released",
        ):
            self.assertIn(marker, source)
        self.assertIn("RUNTIME_COORDINATOR_PARTICIPANT_TEST_OWNER", source)
        self.assertIn("RUNTIME_COORDINATOR_PARTICIPANT_TEST_BACKGROUND", source)
        self.assertIn("runtime_coordinator_report_quiesce_result", source)
        self.assertIn("runtime_coordinator_report_start_result", source)
        self.assertIn("memory_watch_service_set_foreground(true)", source)
        self.assertIn("ota_service_request_prepare()", source)
        self.assertIn("heap_caps_get_largest_free_block", source)

    def test_board_test_uses_psram_stack_and_is_wired_to_boot(self) -> None:
        source = RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE.read_text(encoding="utf-8")
        app_main = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn("xTaskCreateWithCaps(", source)
        self.assertIn("MALLOC_CAP_SPIRAM", source)
        assert_main_source_globbed(self, "services/runtime/runtime_resource_gate_board_test.c")
        self.assertIn("runtime_resource_gate_board_test_start()", app_main)

    def test_board_test_does_not_contain_secret_literals(self) -> None:
        combined = (
            RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE.read_text(encoding="utf-8")
            + "\n"
            + RUNTIME_RESOURCE_GATE_BOARD_TEST_HEADER.read_text(encoding="utf-8")
        )
        for token in ("sk-", "XIAOMI_API_KEY", "API_SERVER_KEY", "device_token"):
            self.assertNotIn(token, combined)


if __name__ == "__main__":
    unittest.main()
