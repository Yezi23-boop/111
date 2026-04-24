import unittest

from tests.main_paths import NETWORK_CREDENTIALS_HEADER
from tests.main_paths import NETWORK_CREDENTIALS_SOURCE


class NetworkCredentialsSourceTests(unittest.TestCase):
    def test_header_exposes_recent_three_networks_contract(self) -> None:
        header = NETWORK_CREDENTIALS_HEADER.read_text(encoding="utf-8")

        self.assertIn("#define NETWORK_CREDENTIALS_MAX_NETWORKS 3", header)
        self.assertIn("network_credentials_entry_t", header)
        self.assertIn("esp_err_t network_credentials_init(void);", header)
        self.assertIn("network_credentials_get_latest", header)
        self.assertIn("esp_err_t network_credentials_list(", header)
        self.assertIn("network_credentials_save_or_promote", header)

    def test_source_implements_recent_list_storage_only(self) -> None:
        source = NETWORK_CREDENTIALS_SOURCE.read_text(encoding="utf-8")

        self.assertIn('kCredentialsNamespace = "net_creds"', source)
        self.assertIn('kCredentialsBlobKey = "recent_list"', source)
        self.assertIn("StaticSemaphore_t s_operation_mutex_buffer", source)
        self.assertIn("SemaphoreHandle_t s_operation_mutex", source)
        self.assertIn("xSemaphoreCreateMutexStatic", source)
        self.assertIn("xSemaphoreTake(s_operation_mutex, portMAX_DELAY)", source)
        self.assertIn("xSemaphoreGive(s_operation_mutex)", source)
        self.assertIn("network_credentials_storage_t", source)
        self.assertIn("entries[NETWORK_CREDENTIALS_MAX_NETWORKS]", source)
        self.assertIn("network_credentials_get_latest", source)
        self.assertIn("network_credentials_list", source)
        self.assertIn("network_credentials_save_or_promote", source)
        self.assertIn("network_credentials_store_to_nvs", source)
        self.assertIn("network_credentials_load_from_nvs", source)
        self.assertIn("network_credentials_ssid_matches", source)
        self.assertIn("updated_storage.entries[0]", source)
        self.assertIn("updated_storage.count < NETWORK_CREDENTIALS_MAX_NETWORKS", source)
        self.assertIn("nvs_set_blob", source)
        self.assertIn("nvs_get_blob", source)
        self.assertNotIn("wifi_prov_mgr", source)
        self.assertNotIn("wifi_provision", source)
        self.assertNotIn("esp_wifi_connect", source)
        self.assertNotIn("WIFI_MODE_APSTA", source)
        self.assertNotIn("ble_", source)


if __name__ == "__main__":
    unittest.main()
