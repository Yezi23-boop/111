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
        self.assertIn("network_service_snapshot_t", header)
        self.assertIn(
            "esp_err_t network_service_get_snapshot(network_service_snapshot_t *snapshot);",
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
        self.assertIn(
            "static network_service_wifi_status_t s_wifi_status_snapshot",
            source,
        )
        self.assertIn("network_service_publish_wifi_status(&status)", source)

        getter_body = source.split(
            "esp_err_t network_service_get_wifi_status(network_service_wifi_status_t *status)",
            1,
        )[1].split(
            "esp_err_t network_service_request_connect_with_saved_credentials(void)",
            1,
        )[0]
        self.assertIn("*status = s_wifi_status_snapshot;", getter_body)
        self.assertNotIn("network_manager_get_status(", getter_body)

    def test_source_publishes_snapshot_without_volatile_state_protocol(self) -> None:
        header = NETWORK_SERVICE_HEADER.read_text(encoding="utf-8")
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_connected", header)
        self.assertIn("service_ready", header)
        self.assertIn("probe_active", header)
        self.assertIn("probe_paused_by_budget", header)
        self.assertIn("power_save_applied", header)
        self.assertIn("last_probe_result", header)
        self.assertIn("portMUX_TYPE s_snapshot_lock", source)
        self.assertIn("network_service_copy_snapshot(", source)
        self.assertIn("network_service_get_snapshot(", source)
        self.assertNotIn("static volatile", source)

    def test_cloud_probe_can_pause_by_power_budget_without_disconnect(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("budget.network_sync_allowed", source)
        self.assertIn("network service probe paused by power budget", source)
        self.assertIn("network_service_set_probe_snapshot(", source)
        self.assertIn("probe_paused_by_budget", source)
        self.assertIn("network sync paused by power budget", source)
        probe_body = source.split(
            "static esp_err_t probe_network_services_ready(void)", 1)[1].split(
            "static void network_service_apply_power_budget(void)", 1)[0]
        self.assertNotIn("network_manager_disconnect", probe_body)


if __name__ == "__main__":
    unittest.main()
