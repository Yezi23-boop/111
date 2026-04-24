import unittest

from tests.main_paths import AP_PORTAL_ADAPTER_SOURCE
from tests.main_paths import AP_PORTAL_ROUTES_SOURCE


class ApPortalHttpApiSourceTests(unittest.TestCase):
    def test_routes_expose_status_scan_and_configure_endpoints(self) -> None:
        source = AP_PORTAL_ROUTES_SOURCE.read_text(encoding="utf-8")

        self.assertIn('/api/status', source)
        self.assertIn('/api/scan', source)
        self.assertIn('/api/configure', source)
        self.assertIn('\\"scan_supported\\":false', source)
        self.assertIn('\\"configure_supported\\":false', source)
        self.assertIn('\\"error\\":\\"not_ready\\"', source)
        self.assertIn("501 Not Implemented", source)

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
            "network_prov_scheme_softap_set_httpd_handle((void *)s_portal_server);",
            source,
        )
        self.assertIn(
            "network_prov_scheme_softap_set_httpd_handle(NULL);", source
        )
        self.assertIn("config.max_uri_handlers = 8;", source)
        self.assertIn("config.stack_size = 8192;", source)


if __name__ == "__main__":
    unittest.main()
