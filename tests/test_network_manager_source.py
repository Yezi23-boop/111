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
        self.assertIn("network_manager_start_ble_provisioning", header)
        self.assertIn("network_manager_start_softap_provisioning", header)
        self.assertIn("network_manager_set_ble_enabled", header)
        self.assertIn("network_manager_get_recent_networks", header)
        self.assertIn("network_manager_connect_recent_by_index", header)

    def test_source_contains_latest_failure_manual_provisioning_policy(self) -> None:
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
        self.assertIn("等待用户明确选择 BLE 配网或 AP 网页兜底", source)
        self.assertIn('#include "ble_presence.h"', source)

    def test_source_updates_recent_list_only_after_successful_connect(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_control_connect", source)
        self.assertIn("network_credentials_save_or_promote", source)
        self.assertIn("NETWORK_PROVISIONING_ADAPTER_EVENT_WIFI_CRED_RECV", source)
        self.assertIn("s_pending_provisioned_entry_connecting", source)
        self.assertIn("WIFI_CONTROL_STATE_CONNECTED", source)
        cred_recv_body = source.split(
            "case NETWORK_PROVISIONING_ADAPTER_EVENT_WIFI_CRED_RECV:", 1
        )[1].split("break;", 1)[0]
        self.assertIn("wifi_control_set_auto_reconnect_enabled(true);", cred_recv_body)
        self.assertLess(
            cred_recv_body.index("wifi_control_set_auto_reconnect_enabled(true);"),
            cred_recv_body.index("wifi_control_connect("),
        )

    def test_source_keeps_ble_switch_separate_from_provisioning_start(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_err_t network_manager_set_ble_enabled(bool enabled)", source)
        self.assertIn("ble_control_set_enabled(enabled)", source)
        self.assertIn("if (!enabled)", source)
        self.assertIn("network_manager_stop_active_transport()", source)
        set_ble_body = source.split(
            "esp_err_t network_manager_set_ble_enabled(bool enabled)", 1
        )[1].split("bool network_manager_is_ble_enabled(void)", 1)[0]
        self.assertNotIn("network_manager_start_selected_transport();", set_ble_body)
        self.assertIn("network_provisioning_adapter_get_transport()", source)
        self.assertIn("network_manager_sync_ble_presence", source)
        self.assertIn("ble_presence_start()", source)
        self.assertIn("ble_presence_stop()", source)
        self.assertIn(
            "static void network_manager_refresh_runtime_state(bool sync_ble_presence)",
            source,
        )
        refresh_body = source.split(
            "static void network_manager_refresh_runtime_state(bool sync_ble_presence)",
            1,
        )[1].split("/**\n * @brief 用一条 recent Wi-Fi", 1)[0]
        self.assertIn("if (sync_ble_presence)", refresh_body)
        self.assertIn("(void)network_manager_sync_ble_presence();", refresh_body)
        get_status_body = source.split(
            "esp_err_t network_manager_get_status(network_manager_status_t *status)", 1
        )[1].split("/**\n * @brief 再次尝试", 1)[0]
        self.assertIn("network_manager_refresh_runtime_state(false);", get_status_body)
        self.assertNotIn("network_manager_refresh_runtime_state(true);", get_status_body)

    def test_source_exposes_explicit_provisioning_entries(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("network_manager_start_ble_provisioning", source)
        self.assertIn("network_manager_start_softap_provisioning", source)
        self.assertIn("network_manager_start_explicit_transport", source)
        self.assertIn("ESP_ERR_INVALID_STATE", source)
        self.assertIn("!ble_control_is_enabled()", source)
        ble_start_body = source.split(
            "case NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE:", 1
        )[1].split("case NETWORK_MANAGER_PROVISIONING_TRANSPORT_SOFTAP:", 1)[0]
        self.assertIn("ble_presence_stop()", ble_start_body)
        self.assertIn("network_provisioning_adapter_start_ble()", ble_start_body)

    def test_source_keeps_auto_path_idle_until_user_selects_provisioning(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("static esp_err_t network_manager_start_selected_transport_auto(void);", source)
        auto_body = source.split(
            "static esp_err_t network_manager_start_selected_transport_auto(void)", 1
        )[1].split("/**\n * @brief 按用户在 Wi-Fi 配网页面点击的入口", 1)[0]
        self.assertIn("s_state = NETWORK_MANAGER_STATE_IDLE;", auto_body)
        self.assertNotIn("network_manager_start_selected_transport();", auto_body)
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
