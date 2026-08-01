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
        self.assertIn("taskENTER_CRITICAL", source)
        self.assertIn("taskEXIT_CRITICAL", source)
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
        self.assertIn("wifi_control_runtime_needs_disconnect_before_connect", source)
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

    def test_cold_boot_connect_does_not_suppress_first_retry(self) -> None:
        source = WIFI_CONTROL_SOURCE.read_text(encoding="utf-8")
        connect_body = source.split(
            "esp_err_t wifi_control_connect(const char *ssid, const char *password)",
            1,
        )[1].split("/**\n * @brief 主动断开当前 STA 连接。", 1)[0]
        helper_body = source.split(
            "static bool wifi_control_runtime_needs_disconnect_before_connect(void)",
            1,
        )[1].split("/**\n * @brief 判断当前是否处于", 1)[0]

        self.assertIn("state == WIFI_CONTROL_STATE_CONNECTING", helper_body)
        self.assertIn("state == WIFI_CONTROL_STATE_CONNECTED", helper_body)
        self.assertIn("bool needs_disconnect_before_connect = false;", connect_body)
        self.assertLess(
            connect_body.index("ret = wifi_control_init();"),
            connect_body.index(
                "needs_disconnect_before_connect =\n        wifi_control_runtime_needs_disconnect_before_connect();"
            ),
        )
        self.assertIn("pre_disconnect=%d", connect_body)
        self.assertIn("if (needs_disconnect_before_connect)", connect_body)
        self.assertIn("ret = wifi_control_request_disconnect(true);", connect_body)
        self.assertIn("else\n    {", connect_body)
        self.assertIn("wifi_control_runtime_clear_reconnect_markers();", connect_body)
        self.assertIn("确保第一次认证失败能进入自动重连分支", connect_body)

    def test_disconnect_log_exposes_reason_and_suppression_state(self) -> None:
        source = WIFI_CONTROL_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_event_sta_disconnected_t", source)
        self.assertIn("reason=%u suppress=%d auto_reconnect=%d retry=%u", source)

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
