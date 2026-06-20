import unittest

from tests.main_paths import (
    AP_PORTAL_ADAPTER_SOURCE,
    AP_PORTAL_ADAPTER_DIR,
    AP_PORTAL_ROUTES_SOURCE,
    AP_PORTAL_WEB_INDEX,
    AP_PORTAL_WEB_JS,
    AP_PORTAL_WEB_PROV_CLIENT,
    AP_PORTAL_WEB_PROTO_BUNDLE,
    CAPTIVE_PORTAL_DNS_SOURCE,
    NETWORK_MANAGER_SOURCE,
    NETWORK_PROVISIONING_ADAPTER_SOURCE,
)


class ApPortalOfficialClientSourceTests(unittest.TestCase):
    def test_ap_portal_web_sources_exist(self) -> None:
        for path in (
            AP_PORTAL_WEB_JS,
            AP_PORTAL_WEB_PROV_CLIENT,
            AP_PORTAL_WEB_PROTO_BUNDLE,
        ):
            self.assertTrue(path.exists(), f"missing expected file: {path}")

    def test_ap_portal_index_uses_module_entry(self) -> None:
        source = AP_PORTAL_WEB_INDEX.read_text(encoding="utf-8")

        self.assertIn('<script type="module" src="/app.js"></script>', source)
        self.assertNotIn('<script src="/app.js"></script>', source)

    def test_ap_portal_app_uses_provisioning_client_entrypoint(self) -> None:
        raw_source = AP_PORTAL_WEB_JS.read_text(encoding="utf-8")
        try:
            source = raw_source.encode("utf-8").decode("unicode-escape")
        except Exception:
            source = raw_source

        self.assertIn(
            'import { createProvisioningClient } from "./prov_client.js";',
            source,
        )
        self.assertIn("const provClient = createProvisioningClient();", source)
        self.assertIn("provClient.getPortalInfo()", source)
        self.assertIn("provClient.scanWifi()", source)
        self.assertIn("provClient.sendWifiConfig(ssid, password)", source)
        self.assertIn("provClient.waitForWifiConnection()", source)
        self.assertIn("info.scanSupported", source)
        self.assertIn("network.ssid", source)
        self.assertIn("network.security", source)
        self.assertIn("network.rssi", source)
        self.assertIn('`扫描完成，共发现 ${networks.length} 个网络。`', source)
        self.assertIn(
            '"凭据已发送，正在等待设备切换到目标 Wi-Fi..."',
            source,
        )
        self.assertIn('"手表已连接 Wi-Fi，可以回到设备查看联网状态。"', source)
        self.assertIn(
            '"设备报告 Wi-Fi 连接失败，请检查密码或路由器信号。"',
            source,
        )
        self.assertIn("waitResult.phase === \"portal_closed\"", source)
        self.assertIn(
            "\"设备已接受凭据并可能正在关闭热点；请切回目标 Wi-Fi 查看手表状态。\"",
            source,
        )
        self.assertIn('wifiList.classList.add("empty")', source)
        self.assertIn('wifiList.classList.remove("empty")', source)
        self.assertNotIn("api_version", source)
        self.assertNotIn("scan_supported", source)
        self.assertNotIn("items", source)
        self.assertNotIn("name ||", source)
        self.assertNotIn("auth", source)
        self.assertNotIn('fetch("/api/scan"', source)
        self.assertNotIn('fetch("/api/configure"', source)
        self.assertNotIn("callPendingApi(", source)

    def test_ap_portal_cmake_embeds_official_client_resources(self) -> None:
        source = (AP_PORTAL_ADAPTER_DIR / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )

        self.assertIn('"web/app.js"', source)
        self.assertIn('"web/prov_client.js"', source)
        self.assertIn('"web/prov_proto_bundle.js"', source)
        self.assertIn("json", source)

    def test_prov_client_references_official_endpoints(self) -> None:
        source = AP_PORTAL_WEB_PROV_CLIENT.read_text(encoding="utf-8")

        self.assertIn('"/proto-ver"', source)
        self.assertIn('"/prov-session"', source)
        self.assertIn('"/prov-scan"', source)
        self.assertIn('"/prov-config"', source)

    def test_prov_client_exposes_scan_config_status_workflow(self) -> None:
        source = AP_PORTAL_WEB_PROV_CLIENT.read_text(encoding="utf-8")

        self.assertIn("async getWifiStatus()", source)
        self.assertIn("async applyWifiConfig()", source)
        self.assertIn("async waitForWifiConnection(", source)
        self.assertIn("async scanWifi()", source)
        self.assertIn("async sendWifiConfig(ssid, password)", source)
        self.assertIn("await applyWifiConfig();", source)
        self.assertIn('phase: "connected"', source)
        self.assertIn('phase: "failed"', source)
        self.assertIn('phase: "portal_closed"', source)
        self.assertIn("isLikelyPortalClosedError", source)
        self.assertIn('WIFI_STATUS_POLL_ATTEMPTS = 15', source)

    def test_prov_proto_bundle_exports_minimal_helpers(self) -> None:
        source = AP_PORTAL_WEB_PROTO_BUNDLE.read_text(encoding="utf-8")

        self.assertIn("export function encodeSessionSetup0Request()", source)
        self.assertIn("export function decodeSessionSetup0Response(buffer)", source)
        self.assertIn("export function encodeScanStartRequest(", source)
        self.assertIn("export function decodeScanResultResponse(buffer)", source)
        self.assertIn("export function encodeSetWifiConfigRequest(", source)
        self.assertIn("export function decodeGetWifiStatusResponse(buffer)", source)

    def test_ap_portal_routes_expose_module_assets_and_legacy_api_notice(self) -> None:
        source = AP_PORTAL_ROUTES_SOURCE.read_text(encoding="utf-8")

        self.assertIn('"/prov_client.js"', source)
        self.assertIn('"/prov_proto_bundle.js"', source)
        self.assertIn('"/api/status"', source)
        self.assertIn('"/api/scan"', source)
        self.assertIn('"/api/configure"', source)
        self.assertIn('"/api/memory-watch/config"', source)
        self.assertIn(
            "Legacy JSON API has been replaced by official provisioning client.",
            source,
        )
        self.assertIn("HTTPD_404_NOT_FOUND", source)
        self.assertIn("303 See Other", source)
        self.assertIn('"Location", "/"', source)
        self.assertIn("Redirect to the captive portal", source)

    def test_ap_portal_httpd_capacity_accounts_for_portal_and_official_endpoints(
        self,
    ) -> None:
        source = AP_PORTAL_ADAPTER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("kPortalMaxUriHandlers = 20", source)
        self.assertIn("config.max_uri_handlers = kPortalMaxUriHandlers;", source)
        self.assertIn("ESP_ERR_HTTPD_HANDLERS_FULL", source)
        self.assertIn(
            "static httpd_handle_t *const s_portal_server_ref = &s_portal_server;",
            source,
        )
        self.assertIn(
            "network_prov_scheme_softap_set_httpd_handle((void *)s_portal_server_ref);",
            source,
        )
        self.assertIn("static esp_err_t ap_portal_adapter_ensure_softap_netif(void);", source)
        self.assertIn("esp_netif_create_default_wifi_ap();", source)
        self.assertIn("ap_portal_adapter_set_dhcp_captive_portal_uri", source)
        self.assertIn("ESP_NETIF_CAPTIVEPORTAL_URI", source)
        self.assertIn("kPortalMutedHttpdTagCount = 3", source)
        self.assertIn('"httpd_uri"', source)
        self.assertIn('"httpd_txrx"', source)
        self.assertIn('"httpd_parse"', source)
        self.assertIn("ap_portal_adapter_mute_httpd_probe_logs", source)
        self.assertIn("ap_portal_adapter_restore_httpd_probe_logs", source)
        self.assertIn("esp_log_level_get(kPortalMutedHttpdTags[index])", source)
        self.assertIn("esp_log_level_set(kPortalMutedHttpdTags[index], ESP_LOG_ERROR);", source)
        self.assertIn("captive_portal_dns_start()", source)
        self.assertIn("captive_portal_dns_stop()", source)

    def test_captive_portal_dns_component_is_wired_to_softap_ip(self) -> None:
        source = CAPTIVE_PORTAL_DNS_SOURCE.read_text(encoding="utf-8")

        self.assertIn('static const char *kWifiApIfKey = "WIFI_AP_DEF";', source)
        self.assertIn('static const char *kWildcardDnsName = "*";', source)
        self.assertIn("esp_netif_get_handle_from_ifkey(kWifiApIfKey)", source)
        self.assertIn("captive_portal_dns_start(void)", source)
        self.assertIn("captive_portal_dns_stop(void)", source)
        self.assertIn("captive_portal_dns_is_running(void)", source)

    def test_softap_transport_bootstrap_starts_and_stops_ap_portal_adapter(self) -> None:
        source = NETWORK_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ap_portal_adapter.h"', source)
        self.assertIn("ret = ap_portal_adapter_start();", source)
        self.assertIn("(void)ap_portal_adapter_stop();", source)
        self.assertIn("active_transport == NETWORK_PROVISIONING_TRANSPORT_SOFTAP", source)

    def test_softap_transport_ensures_default_ap_netif_before_manager_start(self) -> None:
        source = NETWORK_PROVISIONING_ADAPTER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "esp_netif.h"', source)
        self.assertIn('#include "esp_wifi_default.h"', source)
        self.assertIn('static const char *kWifiApIfKey = "WIFI_AP_DEF";', source)
        self.assertIn(
            "static esp_err_t network_provisioning_adapter_ensure_softap_netif(void);",
            source,
        )
        self.assertIn(
            'esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey(kWifiApIfKey);',
            source,
        )
        self.assertIn("ap_netif = esp_netif_create_default_wifi_ap();", source)
        self.assertIn(
            "ret = network_provisioning_adapter_ensure_softap_netif();",
            source,
        )


if __name__ == "__main__":
    unittest.main()
