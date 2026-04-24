import unittest

from tests.main_paths import NETWORK_PROVISIONING_ADAPTER_HEADER
from tests.main_paths import NETWORK_PROVISIONING_ADAPTER_SOURCE


class NetworkProvisioningAdapterSourceTests(unittest.TestCase):
    def test_header_exposes_single_transport_adapter_contract(self) -> None:
        header = NETWORK_PROVISIONING_ADAPTER_HEADER.read_text(encoding="utf-8")

        self.assertIn("NETWORK_PROVISIONING_TRANSPORT_NONE", header)
        self.assertIn("NETWORK_PROVISIONING_TRANSPORT_BLE", header)
        self.assertIn("NETWORK_PROVISIONING_TRANSPORT_SOFTAP", header)
        self.assertIn("NETWORK_PROVISIONING_ADAPTER_STATE_IDLE", header)
        self.assertIn("esp_err_t network_provisioning_adapter_init(void);", header)
        self.assertIn("esp_err_t network_provisioning_adapter_start_ble(void);", header)
        self.assertIn(
            "esp_err_t network_provisioning_adapter_start_softap(void);",
            header,
        )
        self.assertIn("esp_err_t network_provisioning_adapter_stop(void);", header)
        self.assertIn(
            "esp_err_t network_provisioning_adapter_switch_transport(",
            header,
        )
        self.assertIn("bool network_provisioning_adapter_is_active(void);", header)
        self.assertIn(
            "network_provisioning_transport_t network_provisioning_adapter_get_transport(void);",
            header,
        )
        self.assertIn(
            "network_provisioning_adapter_status_t network_provisioning_adapter_get_status(void);",
            header,
        )

    def test_source_uses_wifi_prov_manager_lifecycle(self) -> None:
        source = NETWORK_PROVISIONING_ADAPTER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_prov_mgr_init", source)
        self.assertIn("wifi_prov_mgr_start_provisioning", source)
        self.assertIn("wifi_prov_mgr_stop_provisioning", source)
        self.assertIn("wifi_prov_mgr_deinit", source)
        self.assertIn("network_provisioning_adapter_stop", source)
        self.assertIn("network_provisioning_adapter_switch_transport", source)
        self.assertIn("switch_transport", source)
        self.assertIn("xSemaphoreCreateRecursiveMutexStatic", source)
        self.assertIn("xSemaphoreTakeRecursive", source)


if __name__ == "__main__":
    unittest.main()
