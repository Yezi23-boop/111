import unittest

from tests.main_paths import UI_WIFI_MANAGEMENT_CONTROLLER_HEADER
from tests.main_paths import UI_WIFI_MANAGEMENT_CONTROLLER_SOURCE


class WifiManagementControllerSourceTests(unittest.TestCase):
    def test_controller_exposes_open_and_init(self) -> None:
        self.assertTrue(UI_WIFI_MANAGEMENT_CONTROLLER_HEADER.exists())
        header = UI_WIFI_MANAGEMENT_CONTROLLER_HEADER.read_text(encoding="utf-8")

        self.assertIn("void wifi_management_controller_init(lv_ui *ui);", header)
        self.assertIn("void wifi_management_controller_open(void);", header)

    def test_source_contains_expected_actions(self) -> None:
        self.assertTrue(UI_WIFI_MANAGEMENT_CONTROLLER_SOURCE.exists())
        source = UI_WIFI_MANAGEMENT_CONTROLLER_SOURCE.read_text(
            encoding="utf-8"
        )

        self.assertIn("Use Saved Wi-Fi", source)
        self.assertIn("Disconnect", source)
        self.assertIn("Reprovision", source)
        self.assertIn("BLE", source)
        self.assertIn("SoftAP", source)
        self.assertIn("Wi-Fi Manager", source)
        self.assertIn("Back", source)
        self.assertIn("network_manager_use_latest_wifi()", source)
        self.assertIn("network_manager_disconnect()", source)
        self.assertIn("network_manager_reprovision()", source)
        self.assertIn("network_manager_get_status(", source)
        self.assertIn("network_manager_set_default_transport(", source)
        self.assertIn("NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE", source)
        self.assertIn("NETWORK_MANAGER_PROVISIONING_TRANSPORT_SOFTAP", source)
        self.assertNotIn("NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO", source)


if __name__ == "__main__":
    unittest.main()
