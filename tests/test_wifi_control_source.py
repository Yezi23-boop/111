import unittest

from tests.main_paths import WIFI_CONTROL_HEADER
from tests.main_paths import WIFI_CONTROL_INTERNAL_HEADER
from tests.main_paths import WIFI_CONTROL_SOURCE


class WifiControlSourceTests(unittest.TestCase):
    def test_header_exposes_minimal_sta_runtime_contract(self) -> None:
        header = WIFI_CONTROL_HEADER.read_text(encoding="utf-8")

        self.assertIn("wifi_control_state_t", header)
        self.assertIn("WIFI_CONTROL_STATE_CONNECTED", header)
        self.assertIn("esp_err_t wifi_control_init(void);", header)
        self.assertIn(
            "esp_err_t wifi_control_connect(const char *ssid, const char *password);",
            header,
        )
        self.assertIn("esp_err_t wifi_control_disconnect(void);", header)
        self.assertIn(
            "void wifi_control_set_auto_reconnect_enabled(bool enabled);",
            header,
        )
        self.assertIn(
            "bool wifi_control_is_auto_reconnect_enabled(void);",
            header,
        )
        self.assertIn("bool wifi_control_is_connected(void);", header)
        self.assertIn(
            "esp_err_t wifi_control_get_ip(char *ip_str, size_t ip_str_len);",
            header,
        )
        self.assertIn(
            "esp_err_t wifi_control_set_power_save(bool enabled);",
            header,
        )
        self.assertIn("wifi_control_state_t wifi_control_get_state(void);", header)

    def test_internal_header_keeps_runtime_only_state_fields(self) -> None:
        internal_header = WIFI_CONTROL_INTERNAL_HEADER.read_text(
            encoding="utf-8"
        )

        self.assertIn("WIFI_CONTROL_MAX_RETRY", internal_header)
        self.assertIn("WIFI_CONTROL_STA_IFKEY", internal_header)
        self.assertIn("wifi_control_runtime_t", internal_header)
        self.assertIn("init_in_progress", internal_header)
        self.assertIn("reconnect_suppressed", internal_header)
        self.assertIn("reconnect_after_disconnect", internal_header)
        self.assertNotIn("wifi_prov_mgr", internal_header)
        self.assertNotIn("wifi_provision", internal_header)

    def test_source_stays_on_sta_runtime_control_only(self) -> None:
        source = WIFI_CONTROL_SOURCE.read_text(encoding="utf-8")

        self.assertIn("portMUX_TYPE", source)
        self.assertIn("portENTER_CRITICAL", source)
        self.assertIn("portEXIT_CRITICAL", source)
        self.assertIn("wifi_control_runtime_is_initialized", source)
        self.assertIn("wifi_control_runtime_begin_init", source)
        self.assertIn("wifi_control_runtime_end_init", source)
        self.assertIn("wifi_control_runtime_wait_for_init_completion", source)
        self.assertIn("wifi_control_cleanup_partial_init", source)
        self.assertIn("esp_wifi_init", source)
        self.assertIn("esp_wifi_deinit", source)
        self.assertIn("esp_event_handler_unregister", source)
        self.assertIn("esp_wifi_set_mode(WIFI_MODE_STA)", source)
        self.assertIn("esp_wifi_connect()", source)
        self.assertIn("esp_wifi_disconnect()", source)
        self.assertIn("esp_wifi_set_ps(", source)
        self.assertIn("wifi_control_request_disconnect", source)
        self.assertIn("wifi_control_is_connected", source)
        self.assertIn("wifi_control_set_power_save", source)
        self.assertIn("wifi_control_get_state", source)
        self.assertIn("WIFI_CONTROL_MAX_RETRY", source)
        self.assertIn("wifi_control_runtime_clear_reconnect_markers", source)
        self.assertIn("wifi_control_runtime_set_reconnect_markers", source)
        self.assertNotIn(
            "wifi_control_runtime_set_auto_reconnect_enabled(true);",
            source,
        )
        self.assertNotIn("wifi_prov_mgr", source)
        self.assertNotIn("wifi_provision_start_blecfg", source)
        self.assertNotIn("wifi_provision_start_apcfg", source)
        self.assertNotIn("WIFI_MODE_APSTA", source)
        self.assertNotIn("wifi_control_set_credentials", source)
        self.assertNotIn("nvs_set_str", source)
        self.assertNotIn("nvs_get_str", source)

    def test_connect_failure_clears_suppression_flags(self) -> None:
        source = WIFI_CONTROL_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "wifi_control_runtime_clear_reconnect_markers();\n        wifi_control_runtime_set_state(WIFI_CONTROL_STATE_CONNECT_FAIL);",
            source,
        )

    def test_init_failure_uses_partial_cleanup(self) -> None:
        source = WIFI_CONTROL_SOURCE.read_text(encoding="utf-8")

        self.assertIn("s_wifi_driver_initialized = true;", source)
        self.assertIn("s_wifi_event_handler_registered = true;", source)
        self.assertIn("s_wifi_ip_event_handler_registered = true;", source)
        self.assertIn("s_runtime.init_in_progress = true;", source)
        self.assertIn("wifi_control_runtime_set_initialized(true);", source)
        self.assertIn(
            "ret = wifi_control_ensure_stack_ready();\n    if (ret != ESP_OK)\n    {\n        goto fail;",
            source,
        )
        self.assertIn("wifi_control_runtime_wait_for_init_completion", source)
        self.assertIn("(void)wifi_control_cleanup_partial_init();", source)
        self.assertIn("wifi_control_runtime_end_init();", source)
        self.assertIn("goto fail;", source)


if __name__ == "__main__":
    unittest.main()
