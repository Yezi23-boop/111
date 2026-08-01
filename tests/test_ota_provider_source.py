from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]


class OtaProviderSourceTests(unittest.TestCase):
    def test_common_plan_is_source_neutral_and_keeps_delta_fallback_fields(self) -> None:
        plan = (REPO_ROOT / "main/services/ota/ota_update_plan.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("ota_update_plan_t", plan)
        self.assertIn("OTA_UPDATE_SOURCE_ONENET", plan)
        self.assertIn("OTA_UPDATE_SOURCE_REMOTE_MANIFEST", plan)
        self.assertIn("patch_url", plan)
        self.assertIn("baseline_version", plan)
        self.assertIn("target_sha256", plan)
        self.assertIn("authorization", plan)

    def test_adapter_dispatches_by_compile_time_source(self) -> None:
        source = (REPO_ROOT / "main/services/ota/ota_provider.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("CONFIG_OTA_SOURCE_ONENET", source)
        self.assertIn("CONFIG_OTA_SOURCE_REMOTE_MANIFEST", source)
        self.assertIn("onenet_ota_provider_check_plan", source)
        self.assertIn("ota_transport_fetch_manifest", source)
        self.assertIn("ota_provider_prepare_download", source)
        self.assertIn("ota_provider_store_pending", source)
        self.assertIn("ota_provider_report_status", source)
        self.assertIn("ota_provider_clear_pending", source)
        self.assertIn("ota_transport_version_is_newer", source)
        self.assertIn("OTA_PROVIDER_STATUS_DOWNLOAD_FAILURE", source)
        self.assertIn("OTA_PROVIDER_STATUS_ACTIVATE_FAILURE", source)
        self.assertIn("return 107", source)
        self.assertIn("return 206", source)

    def test_service_does_not_parse_provider_protocol_fields(self) -> None:
        source = (REPO_ROOT / "main/services/ota/ota_service.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("ota_update_plan_t plan", source)
        self.assertIn("plan.has_delta", source)
        self.assertNotIn("tid", source)
        self.assertNotIn("onenet_ota_provider_check", source)
        self.assertNotIn("manifest_url", source)

    def test_provider_rejects_non_newer_targets_before_download(self) -> None:
        source = (REPO_ROOT / "main/services/ota/ota_provider.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("provider returned non-newer target", source)
        self.assertIn("ESP_ERR_INVALID_VERSION", source)


if __name__ == "__main__":
    unittest.main()
