import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WIFI_PROVISION_HEADER = (
    REPO_ROOT / "components" / "wifi_provision" / "include" / "wifi_provision.h"
)
WIFI_PROVISION_SOURCE = (
    REPO_ROOT / "components" / "wifi_provision" / "src" / "wifi_provision.c"
)


class WifiProvisionBleSourceTests(unittest.TestCase):
    def test_wifi_provision_public_ble_apis_exist(self) -> None:
        header = WIFI_PROVISION_HEADER.read_text(encoding="utf-8")
        source = WIFI_PROVISION_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_provision_start_blecfg", header)
        self.assertIn("wifi_provision_stop_blecfg", header)
        self.assertIn("wifi_provision_is_ble_active", header)
        self.assertIn("wifi_provision_is_ap_active", header)
        self.assertIn("wifi_provision_get_ble_service_name", header)
        self.assertIn("wifi_provision_start_blecfg", source)

    def test_wifi_provision_ap_fallback_stops_ble_transport(self) -> None:
        source = WIFI_PROVISION_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ble_provision_transport_stop();", source)
        self.assertIn("wifi_provision_switch_to_ap_fallback", source)

    def test_wifi_provision_init_checks_task_creation_result(self) -> None:
        header = WIFI_PROVISION_HEADER.read_text(encoding="utf-8")
        source = WIFI_PROVISION_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "esp_err_t wifi_provision_init(wifi_provision_cb_t callback);",
            header,
        )
        self.assertIn("if (task_ret != pdPASS)", source)
        self.assertIn("return ESP_FAIL;", source)


if __name__ == "__main__":
    unittest.main()
