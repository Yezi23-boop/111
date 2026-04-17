import unittest

from tests.main_paths import NETWORK_SERVICE_HEADER
from tests.main_paths import NETWORK_SERVICE_SOURCE


class NetworkServiceBleSourceTests(unittest.TestCase):
    def test_network_service_exposes_ble_toggle_control_surface(self) -> None:
        header = NETWORK_SERVICE_HEADER.read_text(encoding="utf-8")
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("NETWORK_SERVICE_STATE_BLE_PROVISIONING", header)
        self.assertIn("NETWORK_SERVICE_STATE_BLE_DISABLED", header)
        self.assertIn(
            "esp_err_t network_service_set_ble_enabled(bool enabled);", header
        )
        self.assertIn("bool network_service_is_ble_enabled(void);", header)
        self.assertIn("bool network_service_is_ble_active(void);", header)
        self.assertIn("wifi_provision_start_blecfg()", source)
        self.assertIn("wifi_provision_start_auto()", source)
        self.assertIn("network_service_request_portal", source)
        self.assertIn("network_service_request_ble", source)

    def test_network_service_persists_ble_preference_and_stops_ble_when_disabled(
        self
    ) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('static const char *kBlePrefNamespace = "network_svc";', source)
        self.assertIn('static const char *kBlePrefKey = "ble_enabled";', source)
        self.assertIn("wifi_provision_stop_blecfg()", source)
        self.assertIn("s_ble_enabled = enabled;", source)
        self.assertIn("network_service_store_ble_pref(enabled)", source)
        self.assertIn('ESP_LOGI(TAG, "BLE preference loaded: enabled=%d",', source)
        self.assertIn('ESP_LOGI(TAG, "BLE preference stored: enabled=%d",', source)

    def test_network_service_falls_back_to_portal_when_ble_bootstrap_fails(
        self,
    ) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_provision_start_apcfg();", source)
        self.assertIn("NETWORK_SERVICE_STATE_PORTAL_REQUIRED", source)

    def test_network_service_rejects_enabling_ble_when_credentials_exist(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("if (enabled)", source)
        self.assertIn("wifi_provision_has_credentials()", source)
        self.assertIn("return ESP_ERR_INVALID_STATE;", source)

    def test_network_service_keeps_portal_state_when_ap_is_active(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_provision_is_ap_active()", source)
        self.assertIn("s_portal_requested = true;", source)

    def test_network_service_request_ble_clears_portal_latch(self) -> None:
        header = NETWORK_SERVICE_HEADER.read_text(encoding="utf-8")
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("void network_service_request_ble(void);", header)
        self.assertIn("s_portal_requested = false;", source)
        self.assertIn("network_service_set_state(", source)
        self.assertIn('"explicit BLE enable request"', source)

    def test_network_service_does_not_auto_restart_ble_when_user_disabled_it(
        self,
    ) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("else if (!s_ble_enabled)", source)
        self.assertIn("NETWORK_SERVICE_STATE_BLE_DISABLED", source)
        self.assertIn("wifi_provision_start_blecfg()", source)

    def test_network_service_logs_ble_requests_and_state_transitions(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("network_service_state_name(", source)
        self.assertIn("network_service_set_state(", source)
        self.assertIn('ESP_LOGI(TAG, "network state: %s -> %s (%s)"', source)
        self.assertIn('"set BLE enabled request: enabled=%d active=%d has_credentials=%d portal_requested=%d"', source)


if __name__ == "__main__":
    unittest.main()
