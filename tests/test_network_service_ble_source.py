import unittest

from tests.main_paths import NETWORK_SERVICE_HEADER
from tests.main_paths import NETWORK_SERVICE_SOURCE


class NetworkServiceBleSourceTests(unittest.TestCase):
    def test_network_service_prefers_ble_when_no_credentials(self) -> None:
        header = NETWORK_SERVICE_HEADER.read_text(encoding="utf-8")
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("NETWORK_SERVICE_STATE_BLE_PROVISIONING", header)
        self.assertIn("wifi_provision_start_blecfg()", source)
        self.assertIn("wifi_provision_start_auto()", source)
        self.assertIn("network_service_request_portal", source)
        self.assertIn("network_service_request_ble", source)

    def test_network_service_falls_back_to_portal_when_ble_bootstrap_fails(
        self,
    ) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_provision_start_apcfg();", source)
        self.assertIn("NETWORK_SERVICE_STATE_PORTAL_REQUIRED", source)

    def test_network_service_keeps_portal_state_when_ap_is_active(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_provision_is_ap_active()", source)
        self.assertIn("s_portal_requested = true;", source)

    def test_network_service_request_ble_clears_portal_latch(self) -> None:
        header = NETWORK_SERVICE_HEADER.read_text(encoding="utf-8")
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("void network_service_request_ble(void);", header)
        self.assertIn("s_portal_requested = false;", source)
        self.assertIn("s_network_state = NETWORK_SERVICE_STATE_BLE_PROVISIONING;", source)


if __name__ == "__main__":
    unittest.main()
