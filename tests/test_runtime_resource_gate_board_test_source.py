import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MAIN_KCONFIG
from tests.main_paths import RUNTIME_RESOURCE_GATE_BOARD_TEST_HEADER
from tests.main_paths import RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE


class RuntimeResourceGateBoardTestSourceTests(unittest.TestCase):
    def test_board_test_is_kconfig_gated_and_default_off(self) -> None:
        kconfig = MAIN_KCONFIG.read_text(encoding="utf-8")
        source = RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE.read_text(encoding="utf-8")

        self.assertIn("config RUNTIME_RESOURCE_GATE_BOARD_TEST", kconfig)
        self.assertIn("default n", kconfig)
        self.assertIn(
            "config RUNTIME_RESOURCE_GATE_BOARD_TEST_ENABLE_REAL_BLE_TOGGLE",
            kconfig,
        )
        self.assertIn("#if CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST", source)
        self.assertIn(
            "#if CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_ENABLE_REAL_BLE_TOGGLE",
            source,
        )

    def test_board_test_exercises_runtime_gate_paths(self) -> None:
        source = RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE.read_text(encoding="utf-8")
        header = RUNTIME_RESOURCE_GATE_BOARD_TEST_HEADER.read_text(encoding="utf-8")

        self.assertIn("runtime_resource_gate_board_test_start", header)
        self.assertIn("memory_watch_service_set_foreground(true)", source)
        self.assertIn("memory_watch_service_set_foreground(false)", source)
        self.assertIn("FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING", source)
        self.assertIn(
            "background_service_manager_notify_foreground_runtime_changed",
            source,
        )
        self.assertIn("memory_watch_service_check_health", source)
        self.assertIn("memory_watch_service_inbox_poll_now", source)

    def test_board_test_uses_psram_stack_and_is_wired_to_boot(self) -> None:
        source = RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE.read_text(encoding="utf-8")
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        app_main = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn("xTaskCreateWithCaps(", source)
        self.assertIn("kTaskStackCaps", source)
        self.assertIn("MALLOC_CAP_SPIRAM", source)
        self.assertIn("MALLOC_CAP_INTERNAL", source)
        self.assertIn(
            "CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST_ENABLE_REAL_BLE_TOGGLE",
            source,
        )
        self.assertIn("NVS/flash", source)
        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/runtime_gate/runtime_resource_gate_board_test.c",
            cmake,
        )
        self.assertIn(
            '#include "services/runtime_gate/runtime_resource_gate_board_test.h"',
            app_main,
        )
        self.assertIn("runtime_resource_gate_board_test_start()", app_main)

    def test_board_test_does_not_contain_secret_literals(self) -> None:
        combined = (
            RUNTIME_RESOURCE_GATE_BOARD_TEST_SOURCE.read_text(encoding="utf-8")
            + "\n"
            + RUNTIME_RESOURCE_GATE_BOARD_TEST_HEADER.read_text(encoding="utf-8")
        )

        forbidden_tokens = ("sk-", "XIAOMI_API_KEY", "API_SERVER_KEY", "device_token")
        for token in forbidden_tokens:
            self.assertNotIn(token, combined)


if __name__ == "__main__":
    unittest.main()
