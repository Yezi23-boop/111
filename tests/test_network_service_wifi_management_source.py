import unittest

from tests.main_paths import NETWORK_SERVICE_HEADER
from tests.main_paths import NETWORK_SERVICE_SOURCE


class NetworkServiceWifiManagementSourceTests(unittest.TestCase):
    def test_header_exposes_wifi_management_contract(self) -> None:
        header = NETWORK_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO", header)
        self.assertIn("NETWORK_SERVICE_PROVISION_TRANSPORT_BLE", header)
        self.assertIn("NETWORK_SERVICE_PROVISION_TRANSPORT_AP", header)
        self.assertIn("network_service_wifi_status_t", header)
        self.assertIn(
            "esp_err_t network_service_request_connect_with_saved_credentials(void);",
            header,
        )
        self.assertIn(
            "esp_err_t network_service_request_disconnect(void);",
            header,
        )
        self.assertIn(
            "esp_err_t network_service_request_reprovision(void);",
            header,
        )
        self.assertIn(
            "bool network_service_is_wifi_connected(void);",
            header,
        )
        self.assertIn(
            "esp_err_t network_service_get_wifi_status(network_service_wifi_status_t *status);",
            header,
        )

    def test_source_bridges_wifi_actions_to_network_manager(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("network_service_request_connect_with_saved_credentials", source)
        self.assertIn("network_service_request_disconnect", source)
        self.assertIn("network_service_request_reprovision", source)
        self.assertIn("network_manager_use_latest_wifi()", source)
        self.assertIn("network_manager_disconnect()", source)
        self.assertIn("network_manager_reprovision()", source)
        self.assertNotIn("wifi_provision_disconnect_sta()", source)
        self.assertNotIn("wifi_provision_connect_saved()", source)
        self.assertNotIn("wifi_provision_stop_active_transport()", source)
        self.assertIn("network_service_set_default_provision_transport", source)
        self.assertIn("network_service_get_wifi_status", source)
        self.assertIn("network_manager_get_recent_networks(NULL, 0, &count)", source)
        self.assertIn("network_service_map_transport_to_manager(", source)
        self.assertIn("network_service_map_transport_from_manager(", source)


if __name__ == "__main__":
    unittest.main()
