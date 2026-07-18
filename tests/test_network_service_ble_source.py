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
        self.assertIn("network_manager_start()", source)
        self.assertIn("network_manager_set_ble_enabled(enabled)", source)
        self.assertIn("network_service_request_portal", source)
        self.assertIn("network_service_request_ble", source)

    def test_network_service_bridges_ble_queries_and_requests_to_network_manager(
        self,
    ) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("network_manager_set_ble_enabled(enabled)", source)
        self.assertIn("network_manager_is_ble_enabled()", source)
        self.assertIn("network_manager_is_ble_active()", source)
        self.assertIn("network_manager_start_ble_provisioning()", source)
        self.assertIn("network_manager_start_softap_provisioning()", source)
        self.assertIn("NETWORK_MANAGER_PROVISIONING_TRANSPORT_BLE", source)
        self.assertIn("NETWORK_MANAGER_PROVISIONING_TRANSPORT_SOFTAP", source)

    def test_ble_toggle_is_reconciled_asynchronously_by_network_owner(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = NETWORK_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("ble_desired_enabled", header)
        self.assertIn("ble_applied_enabled", header)
        self.assertIn("ble_runtime_ready", header)
        self.assertIn("ble_transition_pending", header)
        self.assertIn("ble_last_error", header)
        self.assertIn("s_ble_desired_enabled", source)
        self.assertIn("s_ble_request_generation", source)
        self.assertIn("network_service_reconcile_ble", source)
        self.assertIn("xTaskNotifyGive(s_network_task_handle)", source)
        self.assertIn("ulTaskNotifyTake(pdTRUE", source)
        self.assertIn("FOREGROUND_RUNTIME_OWNER_BLE_PROVISIONING", source)
        self.assertIn("foreground_runtime_gate_acquire", source)
        self.assertIn("foreground_runtime_gate_release", source)
        self.assertIn("MALLOC_CAP_INTERNAL", source)
        self.assertIn("xTaskCreatePinnedToCoreWithCaps(", source)
        self.assertIn("vTaskDeleteWithCaps(completed_worker)", source)
        self.assertIn("vTaskSuspend(NULL)", source)
        self.assertIn("s_ble_transition_completed", source)
        self.assertIn("kBleRetryDelayMs", source)
        self.assertIn("ret == ESP_ERR_NO_MEM", source)

        public_section = source.split(
            "esp_err_t network_service_set_ble_enabled"
        )[1].split("bool network_service_is_ble_enabled", 1)[0]
        self.assertIn("s_ble_desired_enabled = enabled", public_section)
        self.assertNotIn("network_manager_set_ble_enabled(enabled)", public_section)
        self.assertNotIn("foreground_runtime_gate_acquire", public_section)

        task_section = source.split("static void network_service_task")[1].split(
            "esp_err_t network_service_start", 1
        )[0]
        self.assertIn("network_service_reconcile_ble", task_section)

    def test_network_service_maps_softap_provisioning_to_portal_required(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("network_service_map_manager_state(", source)
        self.assertIn("NETWORK_MANAGER_STATE_PROVISIONING_SOFTAP", source)
        self.assertIn("NETWORK_SERVICE_STATE_PORTAL_REQUIRED", source)

    def test_network_service_background_task_mirrors_manager_state_and_probes_cloud(
        self,
    ) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("network_manager_get_status(&status)", source)
        self.assertIn("probe_network_services_ready()", source)
        self.assertIn("NETWORK_SERVICE_STATE_WIFI_READY", source)
        self.assertIn("NETWORK_SERVICE_STATE_SERVICE_READY", source)

    def test_network_service_request_ble_uses_explicit_ble_provisioning_path(
        self,
    ) -> None:
        header = NETWORK_SERVICE_HEADER.read_text(encoding="utf-8")
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("void network_service_request_ble(void);", header)
        self.assertIn("network_manager_start_ble_provisioning()", source)
        request_ble_body = source.split("void network_service_request_ble(void)", 1)[
            1
        ]
        self.assertNotIn("network_manager_set_ble_enabled(true)", request_ble_body)
        self.assertNotIn("network_manager_set_default_transport(", request_ble_body)

    def test_network_service_logs_ble_requests_and_state_transitions(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("network_service_state_name(", source)
        self.assertIn("network_service_set_state(", source)
        self.assertIn('ESP_LOGI(TAG, "network state: %s -> %s (%s)"', source)
        self.assertIn(
            '"queue BLE enable request: enabled=%d generation=%u"', source
        )


if __name__ == "__main__":
    unittest.main()
