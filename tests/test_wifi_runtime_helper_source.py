import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
WIFI_PROVISION_HEADER = (
    REPO_ROOT / "components" / "wifi_provision" / "include" / "wifi_provision.h"
)
WIFI_PROVISION_SOURCE = (
    REPO_ROOT / "components" / "wifi_provision" / "src" / "wifi_provision.c"
)
WIFI_MANAGER_HEADER = (
    REPO_ROOT
    / "components"
    / "wifi_provision"
    / "src"
    / "wifi_driver"
    / "wifi_manager.h"
)
WIFI_MANAGER_SOURCE = (
    REPO_ROOT
    / "components"
    / "wifi_provision"
    / "src"
    / "wifi_driver"
    / "wifi_manager.c"
)
WIFI_MANAGER_PRIVATE = (
    REPO_ROOT
    / "components"
    / "wifi_provision"
    / "src"
    / "wifi_driver"
    / "wifi_manager_private.h"
)


class WifiRuntimeHelperSourceTests(unittest.TestCase):
    def test_wifi_provision_public_header_exposes_runtime_helpers(self) -> None:
        source = WIFI_PROVISION_HEADER.read_text(encoding="utf-8")

        self.assertIn("wifi_provision_start_auto", source)
        self.assertIn("wifi_provision_is_connected", source)
        self.assertIn("wifi_provision_get_ip", source)
        self.assertIn("wifi_provision_set_power_save", source)
        self.assertIn("wifi_provision_set_credentials", source)
        self.assertIn("wifi_provision_has_credentials", source)

    def test_wifi_manager_public_header_exposes_runtime_helpers(self) -> None:
        source = WIFI_MANAGER_HEADER.read_text(encoding="utf-8")

        self.assertIn("wifi_manager_is_connected", source)
        self.assertIn("wifi_manager_get_ip", source)
        self.assertIn("wifi_manager_set_power_save", source)
        self.assertIn("wifi_manager_set_credentials", source)
        self.assertIn("wifi_manager_has_credentials", source)
        self.assertIn("wifi_manager_connect_saved", source)

    def test_wifi_provision_source_forwards_runtime_helpers_to_wifi_manager(self) -> None:
        source = WIFI_PROVISION_SOURCE.read_text(encoding="utf-8")

        self.assertIn("wifi_provision_start_auto", source)
        self.assertIn("wifi_provision_has_credentials()", source)
        self.assertIn("wifi_manager_connect_saved()", source)
        self.assertIn("wifi_provision_start_apcfg()", source)
        self.assertIn("wifi_manager_is_connected()", source)
        self.assertIn("wifi_manager_get_ip(", source)
        self.assertIn("wifi_manager_set_power_save(", source)
        self.assertIn("wifi_manager_set_credentials(", source)
        self.assertIn("wifi_manager_has_credentials()", source)

    def test_wifi_manager_source_persists_credentials_and_exposes_status_helpers(self) -> None:
        source = WIFI_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("#include \"nvs.h\"", source)
        self.assertIn("#include \"nvs_flash.h\"", source)
        self.assertIn("wifi_manager_store_credentials_to_nvs", source)
        self.assertIn("wifi_manager_load_credentials_from_nvs", source)
        self.assertIn("wifi_manager_is_connected", source)
        self.assertIn("wifi_manager_set_power_save", source)
        self.assertIn("wifi_manager_set_credentials", source)
        self.assertIn("wifi_manager_has_credentials", source)
        self.assertIn("wifi_manager_connect_saved", source)

    def test_local_ap_portal_ip_remains_192_168_100_1(self) -> None:
        source = WIFI_MANAGER_PRIVATE.read_text(encoding="utf-8")

        self.assertIn('.ap_ip = "192.168.100.1"', source)

    def test_wifi_manager_ap_applies_custom_ap_ip_before_enabling_ap_mode(self) -> None:
        source = WIFI_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_netif_set_ip_info(ap_netif, &ip_info)", source)
        self.assertIn("esp_netif_get_ip_info(ap_netif, &ip_info)", source)
        self.assertIn("IP2STR(&ip_info.ip)", source)
        self.assertLess(
            source.find("esp_netif_set_ip_info(ap_netif, &ip_info)"),
            source.find("esp_wifi_set_mode(WIFI_MODE_APSTA)"),
        )


if __name__ == "__main__":
    unittest.main()
