import unittest

from tests.main_paths import BLE_CONTROL_HEADER
from tests.main_paths import BLE_CONTROL_SOURCE


class BleControlSourceTests(unittest.TestCase):
    def test_header_exposes_ble_toggle_contract(self) -> None:
        header = BLE_CONTROL_HEADER.read_text(encoding="utf-8")

        self.assertIn("esp_err_t ble_control_init(void);", header)
        self.assertIn(
            "esp_err_t ble_control_set_enabled(bool enabled);",
            header,
        )
        self.assertIn("bool ble_control_is_enabled(void);", header)
        self.assertIn("bool ble_control_is_active(void);", header)
        self.assertIn("esp_err_t ble_control_set_active(bool active);", header)

    def test_source_uses_nvs_pref_and_runtime_active_flag(self) -> None:
        source = BLE_CONTROL_SOURCE.read_text(encoding="utf-8")

        self.assertIn('kBlePrefNamespace = "network_svc"', source)
        self.assertIn('kBlePrefKey = "ble_enabled"', source)
        self.assertIn("StaticSemaphore_t s_ble_mutex_buffer", source)
        self.assertIn("SemaphoreHandle_t s_ble_mutex", source)
        self.assertIn("portMUX_TYPE s_ble_bootstrap_lock", source)
        self.assertIn("xSemaphoreCreateMutexStatic", source)
        self.assertIn("xSemaphoreTake(s_ble_mutex, portMAX_DELAY)", source)
        self.assertIn("xSemaphoreGive(s_ble_mutex)", source)
        self.assertIn("ble_control_store_enabled_pref(enabled);", source)
        self.assertIn(
            "if (ret == ESP_OK)\n    {\n        s_runtime.enabled = enabled;",
            source,
        )
        self.assertIn("ble_control_runtime_t", source)
        self.assertIn("enabled", source)
        self.assertIn("active", source)
        self.assertIn("nvs_set_u8", source)
        self.assertIn("nvs_get_u8", source)
        self.assertNotIn("wifi_prov_mgr", source)
        self.assertNotIn("wifi_provision", source)
        self.assertNotIn("network_provisioning_adapter", source)
        self.assertNotIn("wifi_control", source)
        self.assertNotIn("wifi_provision_start", source)


if __name__ == "__main__":
    unittest.main()
