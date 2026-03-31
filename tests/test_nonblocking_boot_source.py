import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
HARDWARE_INIT_SOURCE = REPO_ROOT / "main" / "hardware_init.c"
MAIN_ENTRY_SOURCE = REPO_ROOT / "main" / "111.c"
NETWORK_SERVICE_SOURCE = REPO_ROOT / "main" / "network_service.c"
NETWORK_SERVICE_HEADER = REPO_ROOT / "main" / "network_service.h"
SDKCONFIG = REPO_ROOT / "sdkconfig"


class NonblockingBootSourceTests(unittest.TestCase):
    def test_hardware_init_no_longer_blocks_waiting_for_wifi(self) -> None:
        source = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("xEventGroupWaitBits(", source)
        self.assertNotIn("portMAX_DELAY", source)
        self.assertIn("wifi_provision_init(", source)

    def test_formal_entry_starts_background_network_service(self) -> None:
        source = MAIN_ENTRY_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "network_service.h"', source)
        self.assertIn("network_service_start()", source)
        self.assertIn(
            '// xTaskCreatePinnedToCore(lvgl_task, "lvgl_task"',
            source,
        )
        self.assertIn(
            '// xTaskCreatePinnedToCore(time_and_weather, "time"',
            source,
        )
        self.assertIn("official_chat_service_init()", source)
        self.assertIn("network_service_is_service_ready()", source)
        self.assertIn("official_chat_service_enter_foreground()", source)
        self.assertIn('"official_chat_bootstrap"', source)

    def test_sdkconfig_uses_formal_entry_instead_of_ai_experiment(self) -> None:
        source = SDKCONFIG.read_text(encoding="utf-8")

        self.assertIn("# CONFIG_APP_AI_CHAT_EXPERIMENT is not set", source)
        self.assertNotIn("CONFIG_APP_AI_CHAT_EXPERIMENT=y", source)

    def test_network_service_exists_and_probes_ai_service_readiness(self) -> None:
        self.assertTrue(NETWORK_SERVICE_SOURCE.exists())
        self.assertTrue(NETWORK_SERVICE_HEADER.exists())

        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_provision_start_auto()", source)
        self.assertIn("wifi_provision_is_connected()", source)
        self.assertIn("getaddrinfo(", source)
        self.assertIn("api.tenclass.net", source)
        self.assertIn("mqtt.xiaozhi.me", source)
        self.assertIn("network_service_start(", source)


if __name__ == "__main__":
    unittest.main()
