import unittest

from tests.main_paths import BLE_PRESENCE_HEADER
from tests.main_paths import BLE_PRESENCE_SOURCE


class BlePresenceSourceTests(unittest.TestCase):
    def test_header_exposes_presence_lifecycle(self) -> None:
        header = BLE_PRESENCE_HEADER.read_text(encoding="utf-8")

        self.assertIn("esp_err_t ble_presence_start(void);", header)
        self.assertIn("esp_err_t ble_presence_stop(void);", header)
        self.assertIn("bool ble_presence_is_active(void);", header)
        self.assertIn("不会启动 Wi-Fi provisioning GATT 服务", header)

    def test_source_uses_nimble_advertising_without_provisioning_service(self) -> None:
        source = BLE_PRESENCE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("nimble_port_init()", source)
        self.assertIn("nimble_port_freertos_init(ble_presence_host_task)", source)
        self.assertIn("ble_gap_adv_start", source)
        self.assertIn("BLE_GAP_CONN_MODE_NON", source)
        self.assertIn("host_synced", source)
        self.assertIn("can_retry_advertising", source)
        self.assertIn("ESP32S3-723C", source)
        self.assertIn("nimble_port_stop()", source)
        self.assertIn("nimble_port_deinit()", source)
        is_active_body = source.split("bool ble_presence_is_active(void)", 1)[1]
        self.assertIn("active = s_runtime.advertising;", is_active_body)
        self.assertNotIn("active = s_runtime.initialized;", is_active_body)
        self.assertNotIn("network_prov", source)


if __name__ == "__main__":
    unittest.main()
