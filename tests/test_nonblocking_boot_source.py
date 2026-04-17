import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import HARDWARE_INIT_SOURCE
from tests.main_paths import NETWORK_SERVICE_HEADER
from tests.main_paths import NETWORK_SERVICE_SOURCE
from tests.main_paths import REPO_ROOT


SDKCONFIG = REPO_ROOT / "sdkconfig"


class NonblockingBootSourceTests(unittest.TestCase):
    def test_hardware_init_no_longer_maps_boot_key_to_ble_or_ap_provisioning(
        self,
    ) -> None:
        source = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn('ESP_LOGI(TAG, "按键单击！启动BLE配网模式...");', source)
        self.assertNotIn("network_service_request_ble();", source)
        self.assertNotIn("BUTTON_SINGLE_CLICK", source)
        self.assertNotIn("BUTTON_MULTIPLE_CLICK", source)
        self.assertNotIn("wifi_provision_start_apcfg();", source)
        self.assertNotIn(
            "iot_button_register_cb(gpio_btn_handle, BUTTON_PRESS_DOWN, NULL, button_press_down_cb, NULL);",
            source,
        )

    def test_hardware_init_no_longer_blocks_waiting_for_wifi(self) -> None:
        source = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn("xEventGroupWaitBits(", source)
        self.assertNotIn("portMAX_DELAY", source)
        self.assertIn("wifi_provision_init(", source)

    def test_formal_entry_starts_background_network_service(self) -> None:
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/network_service.h"', source)
        self.assertIn("network_service_start()", source)
        self.assertIn(
            'xTaskCreatePinnedToCore(lvgl_task, "lvgl_task"',
            source,
        )
        self.assertIn(
            '// xTaskCreatePinnedToCore(time_and_weather, "time"',
            source,
        )
        self.assertIn("official_chat_service_init()", source)
        self.assertNotIn("official_chat_bootstrap_task", source)
        self.assertNotIn("network_service_is_service_ready()", source)
        self.assertNotIn("official_chat_service_enter_foreground()", source)

    def test_sdkconfig_does_not_enable_removed_ai_experiment_entry(self) -> None:
        source = SDKCONFIG.read_text(encoding="utf-8")

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
