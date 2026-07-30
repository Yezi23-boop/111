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

        self.assertIn("重试已保存网络", source)
        self.assertIn("断开连接", source)
        self.assertIn("蓝牙配网", source)
        self.assertIn("网页配网", source)
        self.assertIn("Wi-Fi", source)
        self.assertIn("LV_SYMBOL_CLOSE", source)
        self.assertIn("network_manager_use_latest_wifi()", source)
        self.assertIn("network_manager_disconnect()", source)
        self.assertIn("network_service_request_ble()", source)
        self.assertIn("network_service_request_portal()", source)
        self.assertIn("network_service_request_stop_provisioning()", source)
        self.assertIn("network_service_get_snapshot(&service_snapshot)", source)
        self.assertIn("provisioning_transition_pending", source)
        self.assertIn("provisioning_last_error", source)
        self.assertIn("启动中", source)
        self.assertIn("配网失败", source)
        self.assertIn("请先打开蓝牙总开关", source)
        self.assertIn("!status.wifi_connected", source)
        self.assertIn("network_service_get_wifi_status(", source)
        self.assertNotIn("network_manager_start_ble_provisioning()", source)
        self.assertNotIn("network_manager_start_softap_provisioning()", source)
        self.assertNotIn("network_manager_get_status(", source)
        self.assertIn("Turn on Bluetooth from the main switch first", source)
        self.assertIn("ESP_ERR_NOT_FOUND", source)
        self.assertIn("No Saved Wi-Fi", source)
        self.assertIn("Use BLE Provision or AP Web Fallback first", source)
        self.assertIn("saved Wi-Fi retry failed: %s", source)
        self.assertIn("lv_obj_set_pos(back_btn, 324, 20)", source)
        self.assertIn("lv_obj_set_size(back_btn, 44, 44)", source)
        self.assertNotIn("network_manager_set_default_transport(", source)
        self.assertNotIn("NETWORK_SERVICE_PROVISION_TRANSPORT_AUTO", source)


if __name__ == "__main__":
    unittest.main()
