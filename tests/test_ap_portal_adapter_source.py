import unittest

from tests.main_paths import AP_PORTAL_ADAPTER_HEADER
from tests.main_paths import AP_PORTAL_ADAPTER_SOURCE
from tests.main_paths import AP_PORTAL_ROUTES_HEADER
from tests.main_paths import AP_PORTAL_ROUTES_SOURCE
from tests.main_paths import AP_PORTAL_WEB_CSS
from tests.main_paths import AP_PORTAL_WEB_INDEX
from tests.main_paths import AP_PORTAL_WEB_JS


class ApPortalAdapterSourceTests(unittest.TestCase):
    def test_source_reuses_httpd_for_softap_provisioning(self) -> None:
        source = AP_PORTAL_ADAPTER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("httpd_handle_t", source)
        self.assertIn("network_prov_scheme_softap_set_httpd_handle", source)
        self.assertIn("xSemaphoreCreateMutexStatic", source)
        self.assertIn("xSemaphoreTake(s_portal_mutex, portMAX_DELAY)", source)

    def test_header_exposes_portal_server_contract(self) -> None:
        header = AP_PORTAL_ADAPTER_HEADER.read_text(encoding="utf-8")

        self.assertIn("esp_err_t ap_portal_adapter_start(void);", header)
        self.assertIn("esp_err_t ap_portal_adapter_stop(void);", header)
        self.assertIn("httpd_handle_t ap_portal_adapter_get_httpd_handle(void);", header)
        self.assertIn("ap_portal_memory_watch_config_t", header)
        self.assertIn("ap_portal_memory_watch_config_cb_t", header)
        self.assertIn("ap_portal_adapter_set_memory_watch_config_callback", header)
        self.assertIn("ap_portal_memory_watch_configured_cb_t", header)
        self.assertIn("ap_portal_adapter_set_memory_watch_configured_callback", header)
        self.assertNotIn("memory_watch_service", header)

    def test_routes_source_contains_root_handler(self) -> None:
        header = AP_PORTAL_ROUTES_HEADER.read_text(encoding="utf-8")
        source = AP_PORTAL_ROUTES_SOURCE.read_text(encoding="utf-8")

        self.assertIn("ap_portal_routes_register", header)
        self.assertIn("HTTP_GET", source)
        self.assertIn('.uri="/"', source.replace(" ", ""))
        self.assertIn("/app.js", source)
        self.assertIn("/app.css", source)

    def test_web_assets_exist_for_custom_browser_portal(self) -> None:
        self.assertTrue(AP_PORTAL_WEB_INDEX.exists())
        self.assertTrue(AP_PORTAL_WEB_JS.exists())
        self.assertTrue(AP_PORTAL_WEB_CSS.exists())


if __name__ == "__main__":
    unittest.main()
