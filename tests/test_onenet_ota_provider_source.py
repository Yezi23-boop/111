import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PROVIDER_SOURCE = (REPO_ROOT / "main/services/ota/onenet_ota_provider.c").read_text(
    encoding="utf-8"
)
PROVIDER_HEADER = (REPO_ROOT / "main/services/ota/onenet_ota_provider.h").read_text(
    encoding="utf-8"
)


class OneNetOtaProviderSourceTests(unittest.TestCase):
    def test_provider_uses_product_auth_and_sota(self) -> None:
        self.assertIn('"products/%s"', PROVIDER_SOURCE)
        self.assertIn('"check?type=2&version=%s"', PROVIDER_SOURCE)
        self.assertIn("s_version", PROVIDER_SOURCE)

    def test_provider_uses_tls_bundle_and_nvs(self) -> None:
        self.assertIn("esp_crt_bundle_attach", PROVIDER_SOURCE)
        self.assertIn('"onenet_ota"', PROVIDER_SOURCE)
        self.assertIn("nvs_set_str", PROVIDER_SOURCE)

    def test_provider_does_not_add_mqtt_or_plain_http_path(self) -> None:
        self.assertNotIn("mqtt_client", PROVIDER_SOURCE)
        self.assertNotIn('"http://', PROVIDER_SOURCE)

    def test_provider_rejects_non_full_package(self) -> None:
        self.assertIn("out_task->package_type == 1", PROVIDER_SOURCE)

    def test_product_credentials_are_compiled_and_session_auth_is_generated(self) -> None:
        self.assertIn('kProductId = "w23kT21Z3x"', PROVIDER_SOURCE)
        self.assertIn("kAccessKey", PROVIDER_SOURCE)
        self.assertNotIn("store_credentials", PROVIDER_HEADER)
        self.assertIn("onenet_ota_provider_prepare_download", PROVIDER_SOURCE)
        self.assertIn("onenet_ota_provider_check_plan", PROVIDER_HEADER)
        self.assertIn("onenet_ota_provider_prepare_plan", PROVIDER_HEADER)
        self.assertIn("onenet_ota_provider_store_pending_plan", PROVIDER_HEADER)
        self.assertIn("/download", PROVIDER_SOURCE)
        self.assertIn("onenet_ota_provider_store_pending", PROVIDER_SOURCE)
        self.assertIn("onenet_ota_provider_report_status", PROVIDER_SOURCE)
        self.assertIn(r'{\"step\":%d}', PROVIDER_SOURCE)
        self.assertIn("step == 100 && code == 20", PROVIDER_SOURCE)
        self.assertIn("step >= 101 && step <= 107", PROVIDER_SOURCE)
        self.assertIn("step >= 201 && step <= 208", PROVIDER_SOURCE)


if __name__ == "__main__":
    unittest.main()
