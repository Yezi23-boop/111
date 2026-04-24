import unittest

from tests.main_paths import NETWORK_MANAGER_HEADER
from tests.main_paths import NETWORK_MANAGER_SOURCE


class NetworkManagerSourceTests(unittest.TestCase):
    def test_header_exposes_network_facade_contract(self) -> None:
        header = NETWORK_MANAGER_HEADER.read_text(encoding="utf-8")

        self.assertIn("NETWORK_MANAGER_STATE_CONNECTING_LATEST", header)
        self.assertIn("NETWORK_MANAGER_STATE_PROVISIONING_BLE", header)
        self.assertIn("NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP", header)
        self.assertIn("NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE", header)
        self.assertIn("NETWORK_MANAGER_PROVISIONING_TRANSPORT_SOFTAP", header)
        self.assertNotIn("NETWORK_MANAGER_PROVISIONING_TRANSPORT_AUTO", header)
        self.assertIn("network_manager_use_latest_wifi", header)
        self.assertIn("network_manager_disconnect", header)
        self.assertIn("network_manager_reprovision", header)
        self.assertIn("network_manager_set_ble_enabled", header)
        self.assertIn("network_manager_get_recent_networks", header)
        self.assertIn("network_manager_connect_recent_by_index", header)

    def test_source_contains_latest_failure_fallback_policy(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("CONNECTING_LATEST", source)
        self.assertIn("PROVISIONING_BLE", source)
        self.assertIn("PROVISIONING_SOFTAP", source)
        self.assertIn("network_credentials_get_latest", source)
        self.assertIn("network_provisioning_adapter_start_ble", source)
        self.assertIn("network_provisioning_adapter_start_softap", source)
        self.assertIn("ble_control_is_enabled()", source)
        self.assertIn("WIFI_CONTROL_STATE_CONNECT_FAIL", source)
        self.assertIn("network_manager_task", source)
        self.assertIn("StaticSemaphore_t s_manager_mutex_buffer", source)
        self.assertIn("SemaphoreHandle_t s_manager_mutex", source)
        self.assertIn("portMUX_TYPE s_manager_bootstrap_lock", source)
        self.assertIn("xSemaphoreCreateMutexStatic", source)
        self.assertIn("xSemaphoreTake(s_manager_mutex, portMAX_DELAY)", source)
        self.assertIn("xTaskCreate(network_manager_task", source)
        self.assertIn("network_manager_stop_active_transport", source)
        self.assertIn("network_manager_start_selected_transport_auto()", source)
        self.assertIn("network_manager_connect_entry(&latest, true)", source)

    def test_source_updates_recent_list_only_after_successful_connect(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_control_connect", source)
        self.assertIn("network_credentials_save_or_promote", source)
        self.assertIn("NETWORK_PROVISIONING_ADAPTER_EVENT_WIFI_CRED_RECV", source)
        self.assertIn("s_pending_provisioned_entry_connecting", source)
        self.assertIn("WIFI_CONTROL_STATE_CONNECTED", source)

    def test_source_makes_ble_total_switch_affect_runtime_transport(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_err_t network_manager_set_ble_enabled(bool enabled)", source)
        self.assertIn("ble_control_set_enabled(enabled)", source)
        self.assertIn("if (!enabled)", source)
        self.assertIn("network_manager_stop_active_transport()", source)
        self.assertIn("s_default_transport == NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE", source)
        self.assertIn("ret = network_manager_start_selected_transport();", source)
        self.assertIn("network_provisioning_adapter_get_transport()", source)

    def test_source_allows_idle_boot_when_ble_transport_is_disabled(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("static esp_err_t network_manager_start_selected_transport_auto(void);", source)
        self.assertIn("if (!ble_control_is_enabled())", source)
        self.assertIn("s_state = NETWORK_MANAGER_STATE_IDLE;", source)
        self.assertIn("ret = network_manager_start_selected_transport_auto();", source)

    def test_source_separates_auto_retry_from_manual_saved_wifi_retry(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("bool allow_transport_fallback", source)
        self.assertIn("if (allow_transport_fallback)", source)
        self.assertIn("network_manager_connect_entry(&latest, false)", source)
        self.assertIn("network_manager_connect_entry(&entries[index], false)", source)
        self.assertIn("s_state = NETWORK_MANAGER_STATE_ERROR;", source)


if __name__ == "__main__":
    unittest.main()
