import unittest

from tests.main_paths import AP_PORTAL_ADAPTER_SOURCE
from tests.main_paths import AP_PORTAL_ROUTES_SOURCE


class ApPortalHttpApiSourceTests(unittest.TestCase):
    def test_routes_expose_status_scan_and_configure_endpoints(self) -> None:
        source = AP_PORTAL_ROUTES_SOURCE.read_text(encoding="utf-8")

        self.assertIn('/api/status', source)
        self.assertIn('/api/scan', source)
        self.assertIn('/api/configure', source)
        self.assertIn('/api/memory-watch/config', source)
        self.assertIn('\\"scan_supported\\":false', source)
        self.assertIn('\\"configure_supported\\":false', source)
        self.assertIn('\\"memory_watch_config_supported\\":%s', source)
        self.assertIn('\\"error\\":\\"legacy_api_removed\\"', source)
        self.assertIn("410 Gone", source)
        self.assertIn("ap_portal_memory_watch_config_handler", source)
        self.assertIn("memory_watch_config_unavailable", source)
        self.assertIn("501 Not Implemented", source)
        self.assertIn("413 Payload Too Large", source)
        self.assertIn("request_active", source)
        self.assertIn("memory_watch_configured", source)
        self.assertNotIn("device_token=%s", source)

    def test_memory_watch_config_route_uses_callback_without_service_dependency(self) -> None:
        source = AP_PORTAL_ROUTES_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "cJSON.h"', source)
        self.assertIn("httpd_req_recv", source)
        self.assertIn("kMemoryWatchConfigMaxBodyBytes = 768U", source)
        self.assertIn("kMemoryWatchConfigMaxRecvTimeouts = 4U", source)
        self.assertIn("ESP_ERR_TIMEOUT", source)
        self.assertIn("408 Request Timeout", source)
        self.assertIn("request_timeout", source)
        self.assertIn("timeout_count = 0", source)
        self.assertIn("ap_portal_routes_set_memory_watch_config_callback", source)
        self.assertIn("s_memory_watch_config_callback", source)
        self.assertIn("ap_portal_parse_memory_watch_config", source)
        self.assertIn('root, "base_url"', source)
        self.assertIn('root, "device_id"', source)
        self.assertIn('root, "device_token"', source)
        self.assertIn('"timeout_ms"', source)
        self.assertIn('"allow_http"', source)
        self.assertIn('"allow_insecure_http"', source)
        self.assertIn("callback(&config, user_ctx)", source)
        self.assertNotIn("memory_watch_service", source)

    def test_routes_keep_static_assets_and_favicon_handlers(self) -> None:
        source = AP_PORTAL_ROUTES_SOURCE.read_text(encoding="utf-8")

        self.assertIn('.uri = "/"', source)
        self.assertIn('.uri = "/app.js"', source)
        self.assertIn('.uri = "/app.css"', source)
        self.assertIn('.uri = "/favicon.ico"', source)
        self.assertIn("204 No Content", source)

    def test_adapter_reuses_same_httpd_handle_for_softap_provisioning(self) -> None:
        source = AP_PORTAL_ADAPTER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "network_prov_scheme_softap_set_httpd_handle((void *)s_portal_server_ref);",
            source,
        )
        self.assertIn(
            "network_prov_scheme_softap_set_httpd_handle(NULL);", source
        )
        self.assertIn("config.max_uri_handlers = kPortalMaxUriHandlers;", source)
        self.assertIn("config.stack_size = 8192;", source)


if __name__ == "__main__":
    unittest.main()
