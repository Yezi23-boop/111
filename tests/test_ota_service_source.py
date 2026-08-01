from pathlib import Path
import unittest

from tests.main_cmake_contract import assert_main_source_globbed


REPO_ROOT = Path(__file__).resolve().parents[1]
OTA_SERVICE = REPO_ROOT / "main" / "services" / "ota" / "ota_service.c"
OTA_HEADER = REPO_ROOT / "main" / "services" / "ota" / "ota_service.h"
OTA_VIEW = REPO_ROOT / "main" / "ui" / "custom" / "ota_maintenance_view.c"
OTA_BOOT_CHECK = REPO_ROOT / "main" / "services" / "ota" / "ota_boot_check.c"
OTA_BOARD_TEST = REPO_ROOT / "main" / "services" / "ota" / "ota_board_test.c"
MAIN_CMAKE = REPO_ROOT / "main" / "CMakeLists.txt"


class OtaServiceSourceTests(unittest.TestCase):
    def test_service_uses_bounded_queue_and_ui_readiness(self) -> None:
        source = OTA_SERVICE.read_text(encoding="utf-8")
        header = OTA_HEADER.read_text(encoding="utf-8")

        self.assertIn("xQueueCreateStatic", source)
        self.assertIn("kCommandQueueLength = 4", source)
        self.assertIn("kTaskStackBytes = 16384", source)
        self.assertIn("startup_readiness_wait_ui_first_frame(portMAX_DELAY)", source)
        self.assertIn("runtime_coordinator_request_foreground", source)
        self.assertIn("RUNTIME_COORDINATOR_PARTICIPANT_OTA", source)
        self.assertIn("power_policy_set_maintenance_window(true, \"ota\")", source)
        self.assertIn("runtime_coordinator_report_start_result", source)
        self.assertIn("runtime_coordinator_report_quiesce_result", source)
        self.assertIn("OTA_SERVICE_STATE_DOWNLOADING", source)
        self.assertIn("ota_service_check_onenet", source)
        self.assertIn("onenet_ota_provider_report_version", source)
        self.assertIn("onenet_ota_provider_check", source)
        self.assertIn("onenet_ota_provider_store_pending", source)
        self.assertIn("ota_service_report_pending", source)
        self.assertIn("system_time_service_ensure_valid_for_tls", source)
        self.assertIn("OTA_SERVICE_STATE_VERIFYING", source)
        self.assertIn("OTA_SERVICE_STATE_RESTARTING", source)
        self.assertNotIn("foreground_runtime_gate", source)
        self.assertNotIn("background_service_manager", source)
        self.assertNotIn("official_chat_service_set_maintenance_active", source)
        self.assertNotIn("network_service_set_maintenance_active", source)
        self.assertNotIn("audio_app_set_maintenance_active", source)
        self.assertNotIn("mp3_player_set_maintenance_active", source)
        self.assertNotIn("wakeup_evidence_service_set_maintenance_active", source)
        self.assertNotIn("i2c_manager_set_maintenance_active", source)
        self.assertNotIn("ota_service_wait_owner_release", source)
        self.assertIn("ota_service_get_snapshot", header)
        self.assertIn("OTA_SERVICE_STATE_STAGED", header)
        self.assertIn("ota_service_request_activate", header)
        self.assertNotIn("esp_https_ota", source)
        self.assertNotIn("esp_partition_erase_range", source)

    def test_view_keeps_lvgl_calls_in_ui_owner_and_uses_safe_zone(self) -> None:
        source = OTA_VIEW.read_text(encoding="utf-8")

        self.assertIn("ota_maintenance_view_poll", source)
        self.assertIn("ota_service_get_snapshot", source)
        self.assertIn("lv_obj_set_pos(prepare, 40, 380)", source)
        self.assertIn("lv_obj_set_pos(cancel, 210, 380)", source)
        self.assertIn("lv_obj_set_pos(onenet, 40, 320)", source)
        self.assertIn("lv_obj_set_size(onenet, 160, 44)", source)
        self.assertIn("lv_obj_set_size(prepare, 160, 54)", source)
        self.assertIn("lv_obj_set_size(cancel, 160, 54)", source)
        self.assertIn("ota_service_request_remote_manifest", source)
        self.assertIn("ota_service_request_onenet_check", source)
        self.assertIn("OneNET检查", source)
        self.assertIn("ota_service_request_start", source)
        self.assertIn("ota_service_request_activate", source)

    def test_build_includes_service_and_view(self) -> None:
        assert_main_source_globbed(self, "services/ota/ota_service.c")
        assert_main_source_globbed(self, "ui/custom/ota_maintenance_view.c")

    def test_transport_enforces_manifest_and_finish_admission_contract(self) -> None:
        source = (REPO_ROOT / "main" / "services" / "ota" / "ota_transport.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("ota_transport_is_https_url", source)
        self.assertIn("ota_transport_host_matches", source)
        self.assertIn("kMinimumTlsTime", source)
        self.assertIn("ota_transport_version_is_newer", source)
        self.assertIn("esp_https_ota_get_image_size", source)
        self.assertIn("esp_https_ota_get_image_len_read", source)
        self.assertIn("esp_https_ota_is_complete_data_received", source)
        self.assertIn("partial_http_download = config->authorization == NULL", source)
        self.assertIn("max_http_request_size = kOtaPartialRequestBytes", source)
        self.assertIn("kOtaDownloadBufferBytes = 4 * 1024", source)
        self.assertIn("kOtaNoProgressTimeoutUs", source)
        self.assertIn("download stalled: received=%d total=%u", source)
        self.assertIn("ota_resumption", source)
        self.assertIn("ota_image_bytes_written", source)
        self.assertIn("kOtaReconnectMax = 3U", source)
        self.assertIn("download reconnect: attempt=%u offset=%u err=%s", source)
        self.assertIn("ota_transport_hash_partition", source)
        self.assertIn("ota_transport_hash_partition_md5", source)
        self.assertIn("http_client_init_cb", source)
        self.assertIn('"Authorization"', source)
        self.assertIn("OTA_TRANSPORT_CHECKSUM_MD5", source)
        self.assertIn("esp_https_ota_finish", source)
        self.assertIn("esp_https_ota_abort", source)
        self.assertIn("ota_transport_download_to_staging", source)
        self.assertIn("ota_transport_activate_staging", source)
        self.assertIn("ota_transport_abort_staging", source)
        self.assertIn("OTA_TRANSPORT_FAULT_ABORT_AT_20_PERCENT", source)
        self.assertIn("OTA_TRANSPORT_FAULT_ABORT_AT_50_PERCENT", source)
        self.assertIn("OTA_TRANSPORT_FAULT_ABORT_AT_90_PERCENT", source)
        self.assertIn("fault_window: finish_succeeded_before_restart", 
                      (REPO_ROOT / "main" / "services" / "ota" / "ota_service.c").read_text(encoding="utf-8"))

    def test_boot_check_is_early_local_and_rollback_aware(self) -> None:
        source = OTA_BOOT_CHECK.read_text(encoding="utf-8")
        self.assertIn("ESP_OTA_IMG_PENDING_VERIFY", source)
        self.assertIn("esp_ota_mark_app_valid_cancel_rollback", source)
        self.assertIn("esp_ota_mark_app_invalid_rollback_and_reboot", source)
        self.assertIn("/resources/README.md", source)
        self.assertNotIn("network_service", source)

    def test_board_test_is_explicit_and_waits_for_owner_readiness(self) -> None:
        source = OTA_BOARD_TEST.read_text(encoding="utf-8")

        self.assertIn("#if CONFIG_OTA_SERVICE_BOARD_TEST", source)
        self.assertIn("CONFIG_OTA_SERVICE_BOARD_TEST_ONENET", source)
        self.assertIn("network_service_is_service_ready", source)
        self.assertIn("ota_service_request_remote_manifest", source)
        self.assertIn("ota_service_request_onenet_check", source)
        self.assertIn("ota_service_request_start", source)
        self.assertIn("ota_service_request_activate", source)
        self.assertIn("ota_board_test_wait_onenet_result", source)
        self.assertIn("snapshot.onenet_task_valid", source)
        self.assertIn("ota_board_test_wait_staged_result", source)
        self.assertIn("kStagingTimeoutMs = 900000U", source)
        self.assertIn("download progress: %u%% (%u/%u bytes)", source)
        self.assertIn("download staging timeout: progress=%u%%", source)
        self.assertNotIn("esp_https_ota", source)
        self.assertNotIn("ota_test_ca_pem", source)
        self.assertNotIn("OTA_SERVICE_BOARD_TEST_REMOTE_CLOUD", source)


if __name__ == "__main__":
    unittest.main()
