import unittest

from tests.main_paths import NETWORK_MANAGER_HEADER
from tests.main_paths import NETWORK_SERVICE_SOURCE
from tests.main_paths import WIFI_CONTROL_HEADER
from tests.main_paths import WIFI_CONTROL_SOURCE


class WifiNetworkRuntimeSourceTests(unittest.TestCase):
    def test_wifi_control_header_exposes_runtime_helpers_without_saved_credentials_api(self) -> None:
        header = WIFI_CONTROL_HEADER.read_text(encoding="utf-8")

        self.assertIn(
            "esp_err_t wifi_control_set_power_save(bool enabled);", header
        )
        self.assertIn(
            "void wifi_control_set_auto_reconnect_enabled(bool enabled);",
            header,
        )
        self.assertIn("bool wifi_control_is_connected(void);", header)
        self.assertIn(
            "esp_err_t wifi_control_get_ip(char *ip_str, size_t ip_str_len);",
            header,
        )
        self.assertNotIn("wifi_control_set_credentials", header)
        self.assertNotIn("wifi_control_has_credentials", header)

    def test_wifi_control_source_maps_power_save_to_esp_wifi_ps(self) -> None:
        source = WIFI_CONTROL_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_err_t wifi_control_set_power_save(bool enabled)", source)
        self.assertIn(
            "ret = esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);",
            source,
        )
        self.assertIn('ESP_LOGI(TAG, "power save enabled=%d"', source)

    def test_network_manager_header_is_primary_saved_wifi_and_reprovision_facade(self) -> None:
        header = NETWORK_MANAGER_HEADER.read_text(encoding="utf-8")

        self.assertIn("network_manager_use_latest_wifi", header)
        self.assertIn("network_manager_disconnect", header)
        self.assertIn("network_manager_reprovision", header)
        self.assertIn("network_manager_get_recent_networks", header)
        self.assertIn("network_manager_connect_recent_by_index", header)
        self.assertNotIn("NETWORK_MANAGER_PROVISIONING_TRANSPORT_AUTO", header)

    def test_network_service_derives_saved_credentials_from_recent_list(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "network_manager_get_recent_networks(NULL, 0, &count)", source
        )
        self.assertNotIn("wifi_provision_has_credentials", source)
        self.assertNotIn("wifi_manager_has_credentials", source)


if __name__ == "__main__":
    unittest.main()
