import unittest

from tests.main_paths import NETWORK_MANAGER_SOURCE
from tests.main_paths import NETWORK_PROVISIONING_ADAPTER_HEADER
from tests.main_paths import NETWORK_PROVISIONING_ADAPTER_SOURCE


class NetworkProvisioningTransportSourceTests(unittest.TestCase):
    def test_adapter_header_exposes_transport_status_and_event_callback(self) -> None:
        header = NETWORK_PROVISIONING_ADAPTER_HEADER.read_text(encoding="utf-8")

        self.assertIn("NETWORK_PROVISIONING_TRANSPORT_BLE", header)
        self.assertIn("NETWORK_PROVISIONING_TRANSPORT_SOFTAP", header)
        self.assertIn("NETWORK_PROVISIONING_ADAPTER_STATE_ACTIVE_BLE", header)
        self.assertIn("NETWORK_PROVISIONING_ADAPTER_STATE_ACTIVE_SOFTAP", header)
        self.assertIn("network_provisioning_adapter_event_cb_t", header)
        self.assertIn(
            "NETWORK_PROVISIONING_ADAPTER_EVENT_WIFI_CRED_RECV", header
        )
        self.assertIn(
            "esp_err_t network_provisioning_adapter_set_event_callback(",
            header,
        )

    def test_adapter_source_keeps_official_manager_start_stop_deinit_lifecycle(self) -> None:
        source = NETWORK_PROVISIONING_ADAPTER_SOURCE.read_text(
            encoding="utf-8"
        )

        self.assertIn('service_name =\n        (transport == NETWORK_PROVISIONING_TRANSPORT_BLE) ? "NET_PROV_BLE"', source)
        self.assertIn(': "NET_PROV_AP";', source)
        self.assertIn("wifi_prov_mgr_start_provisioning(", source)
        self.assertIn("wifi_prov_mgr_stop_provisioning();", source)
        self.assertIn("ret = wifi_prov_mgr_deinit();", source)
        self.assertIn("network_provisioning_adapter_stop_manager", source)

    def test_adapter_source_uses_stop_then_start_for_transport_switch(self) -> None:
        source = NETWORK_PROVISIONING_ADAPTER_SOURCE.read_text(
            encoding="utf-8"
        )

        self.assertIn(
            "ret = network_provisioning_adapter_stop_manager();", source
        )
        self.assertIn(
            "ret = network_provisioning_adapter_start_transport(\n            NETWORK_PROVISIONING_TRANSPORT_BLE);",
            source,
        )
        self.assertIn(
            "ret = network_provisioning_adapter_start_transport(\n            NETWORK_PROVISIONING_TRANSPORT_SOFTAP);",
            source,
        )

    def test_network_manager_registers_adapter_callback_and_persists_recent_after_success(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "network_provisioning_adapter_set_event_callback(", source
        )
        self.assertIn("network_manager_on_provisioning_event", source)
        self.assertIn(
            "NETWORK_PROVISIONING_ADAPTER_EVENT_WIFI_CRED_RECV", source
        )
        self.assertIn("network_credentials_save_or_promote(", source)
        self.assertIn("wifi_control_connect(", source)


if __name__ == "__main__":
    unittest.main()
