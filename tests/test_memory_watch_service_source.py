import unittest

from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MEMORY_WATCH_SERVICE_HEADER
from tests.main_paths import MEMORY_WATCH_SERVICE_SOURCE


class MemoryWatchServiceSourceTests(unittest.TestCase):
    def test_service_header_and_source_exist(self) -> None:
        self.assertTrue(MEMORY_WATCH_SERVICE_HEADER.exists())
        self.assertTrue(MEMORY_WATCH_SERVICE_SOURCE.exists())

    def test_service_exposes_v1_snapshot_and_commands(self) -> None:
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_service_state_t", header)
        self.assertIn("MEMORY_WATCH_SERVICE_URL_MAX_BYTES", header)
        self.assertIn("MEMORY_WATCH_SERVICE_DEVICE_TOKEN_MAX_BYTES", header)
        self.assertIn("MEMORY_WATCH_SERVICE_HEALTH_TIMEOUT_MS 5000U", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_READY", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_RECORDING", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_UPLOADING", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_THINKING", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION", header)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_TIMEOUT", header)
        self.assertIn("memory_watch_service_snapshot_t", header)
        self.assertIn("network_ready", header)
        self.assertIn("endpoint_configured", header)
        self.assertIn("hermes_online", header)
        self.assertIn("request_active", header)
        self.assertIn("clarification_active", header)
        self.assertIn("request_id[MEMORY_WATCH_SERVICE_ID_MAX_BYTES]", header)
        self.assertIn("asr_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES]", header)
        self.assertIn("reply_text[MEMORY_WATCH_SERVICE_TEXT_MAX_BYTES]", header)
        self.assertIn("memory_watch_service_endpoint_config_t", header)
        self.assertIn("const char *base_url", header)
        self.assertIn("const char *device_id", header)
        self.assertIn("const char *device_token", header)
        self.assertIn("allow_insecure_http", header)
        self.assertIn("esp_err_t memory_watch_service_init(void);", header)
        self.assertIn("memory_watch_service_configure_endpoint", header)
        self.assertIn("memory_watch_service_check_health", header)
        self.assertIn("memory_watch_service_begin_recording", header)
        self.assertIn("memory_watch_service_send_recording", header)
        self.assertIn("memory_watch_service_cancel_recording", header)
        self.assertIn("memory_watch_service_cancel_waiting", header)
        self.assertIn("memory_watch_service_cancel_clarification", header)
        self.assertIn("memory_watch_service_get_snapshot", header)

    def test_service_uses_owner_task_queue_and_snapshot(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "network_service.h"', source)
        self.assertIn('#include "nvs.h"', source)
        self.assertIn('#include "services/memory_watch_voice_client.h"', source)
        self.assertIn('#include "esp_random.h"', source)
        self.assertIn("xQueueCreateStatic(", source)
        self.assertIn("xQueueSend(", source)
        self.assertIn("xQueueReceive(", source)
        self.assertIn("xTaskCreateStatic(", source)
        self.assertIn("s_upload_worker_queue", source)
        self.assertIn("s_cancel_worker_queue", source)
        self.assertIn("s_health_worker_queue", source)
        self.assertIn("memory_watch_service_upload_worker_task", source)
        self.assertIn("memory_watch_service_cancel_worker_task", source)
        self.assertIn("memory_watch_service_health_worker_task", source)
        self.assertIn("portMUX_TYPE s_snapshot_lock", source)
        self.assertIn("portMUX_TYPE s_endpoint_lock", source)
        self.assertIn("portMUX_TYPE s_worker_lock", source)
        self.assertIn("s_upload_worker_busy", source)
        self.assertIn("memory_watch_service_set_upload_worker_busy", source)
        self.assertIn("memory_watch_service_is_upload_worker_busy", source)
        self.assertIn("memory_watch_service_copy_snapshot(", source)
        self.assertIn("memory_watch_service_handle_command(", source)
        self.assertIn("memory_watch_service_can_begin_from_state(", source)
        self.assertIn("memory_watch_service_copy_client_config(", source)
        self.assertIn("memory_watch_service_make_request_id(", source)
        self.assertIn("before.request_active", source)
        self.assertIn("network_service_is_service_ready()", source)
        self.assertNotIn("static volatile", source)

    def test_service_configures_endpoint_and_health_without_secrets(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")
        combined = source + "\n" + header

        self.assertIn("memory_watch_service_endpoint_state_t", source)
        self.assertIn("memory_watch_service_copy_required_text", source)
        self.assertIn("s_endpoint_config = next_config", source)
        self.assertIn("memory_watch_service_set_endpoint_snapshot(true, false)", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CMD_CHECK_HEALTH", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CMD_HEALTH_DONE", source)
        self.assertIn("memory_watch_service_start_health_job", source)
        self.assertIn("memory_watch_service_handle_health_done", source)
        self.assertIn("before.request_active", source)
        self.assertIn("memory_watch_voice_client_get_health", source)
        self.assertIn("MEMORY_WATCH_SERVICE_HEALTH_TIMEOUT_MS", source)

        health_section = source.split(
            "static void memory_watch_service_handle_check_health"
        )[1].split("static void memory_watch_service_handle_send_recording")[0]
        self.assertIn("memory_watch_service_start_health_job", health_section)
        self.assertNotIn("memory_watch_voice_client_get_health", health_section)

        init_section = source.split("esp_err_t memory_watch_service_init(void)")[1]
        self.assertIn("s_health_worker_task_handle != NULL", init_section)
        self.assertIn("s_health_worker_queue = xQueueCreateStatic", init_section)
        self.assertIn("sizeof(memory_watch_service_health_job_t)", init_section)
        self.assertIn("memory_watch_service_health_worker_task", init_section)
        self.assertNotIn("HERMES_API_KEY", combined)
        self.assertNotIn("API_SERVER_KEY", combined)
        self.assertNotIn("XIAOMI_API_KEY", combined)

    def test_service_loads_runtime_endpoint_config_from_nvs_without_secret_logs(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('kEndpointNvsNamespace = "memory_watch"', source)
        self.assertIn('kEndpointNvsBaseUrlKey = "base_url"', source)
        self.assertIn('kEndpointNvsDeviceIdKey = "device_id"', source)
        self.assertIn('kEndpointNvsDeviceTokenKey = "device_token"', source)
        self.assertIn('kEndpointNvsAllowHttpKey = "allow_http"', source)
        self.assertIn("nvs_open(kEndpointNvsNamespace, NVS_READONLY", source)
        self.assertIn("nvs_get_str(handle, key, dst, &len)", source)
        self.assertIn("nvs_get_u32(handle, kEndpointNvsTimeoutMsKey", source)
        self.assertIn("nvs_get_u8(handle, kEndpointNvsAllowHttpKey", source)
        self.assertIn("memory_watch_service_configure_endpoint(&config)", source)
        self.assertIn("memory_watch_service_load_endpoint_from_nvs();", source)
        self.assertIn("watch endpoint NVS config not found", source)
        self.assertIn("watch endpoint NVS config incomplete", source)
        self.assertNotIn('ESP_LOGI(TAG, "watch endpoint configured from NVS: token', source)
        self.assertNotIn("device_token=%s", source)

    def test_service_generates_contract_request_id_but_does_not_upload_in_owner(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_random()", source)
        self.assertIn('"%s-%08" PRIx32 "-%04" PRIu32', source)
        self.assertIn("s_request_seq", source)
        self.assertIn("memory_watch_service_set_request_id(request_id)", source)
        self.assertIn("case MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION", source)

    def test_service_runs_recorder_and_http_only_in_workers(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/memory_watch_recorder.h"', source)
        self.assertIn('#include "esp_heap_caps.h"', source)
        self.assertIn("memory_watch_service_upload_worker_task", source)
        self.assertIn("memory_watch_service_health_worker_task", source)
        self.assertIn("memory_watch_recorder_capture_ogg_opus", source)
        self.assertIn("memory_watch_voice_client_post_voice_command", source)
        self.assertIn("memory_watch_voice_client_get_health", source)
        self.assertIn("memory_watch_service_audio_write_cb", source)
        self.assertIn("memory_watch_service_should_abort_recording", source)
        self.assertIn(".should_abort_cb = memory_watch_service_should_abort_recording", source)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)

        send_section = source.split(
            "static void memory_watch_service_handle_send_recording"
        )[1].split("static void memory_watch_service_handle_cancel_recording")[0]
        self.assertIn("memory_watch_service_request_record_stop(false)", send_section)
        self.assertNotIn("memory_watch_recorder_capture_ogg_opus", send_section)
        self.assertNotIn("memory_watch_voice_client_post_voice_command", send_section)

        health_worker_section = source.split(
            "static void memory_watch_service_health_worker_task"
        )[1].split("static void memory_watch_service_cancel_worker_task")[0]
        self.assertIn("memory_watch_voice_client_get_health", health_worker_section)

    def test_service_splits_cancel_paths_and_ignores_late_results(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("memory_watch_service_handle_cancel_recording", source)
        self.assertIn("memory_watch_service_handle_cancel_waiting", source)
        self.assertIn("memory_watch_service_request_record_stop(true)", source)
        self.assertIn("memory_watch_service_request_wait_cancel(before.request_id)", source)
        self.assertIn("memory_watch_service_is_wait_canceled_request", source)
        self.assertIn("memory_watch_service_start_cancel_job", source)
        self.assertIn("memory_watch_voice_client_cancel_request", source)
        self.assertIn("memory_watch_service_request_id_matches_current", source)
        self.assertIn("result->cancel_requested", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CMD_WORKER_DONE", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CMD_WORKER_UPLOAD_STARTED", source)

        cancel_waiting_section = source.split(
            "static void memory_watch_service_handle_cancel_waiting"
        )[1].split("static void memory_watch_service_handle_upload_started")[0]
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_RECORDING", cancel_waiting_section)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_ENCODING", cancel_waiting_section)
        self.assertIn("memory_watch_service_request_record_stop(true)", cancel_waiting_section)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_UPLOADING", cancel_waiting_section)
        self.assertIn("MEMORY_WATCH_SERVICE_STATE_THINKING", cancel_waiting_section)
        self.assertIn("memory_watch_service_set_request_active(false)", cancel_waiting_section)

        worker_done_section = source.split(
            "static void memory_watch_service_handle_worker_done"
        )[1].split("static void memory_watch_service_handle_command")[0]
        self.assertLess(
            worker_done_section.index("memory_watch_service_is_wait_canceled_request"),
            worker_done_section.index("memory_watch_service_request_id_matches_current"),
        )

        upload_worker_section = source.split(
            "static void memory_watch_service_upload_worker_task"
        )[1].split("static void memory_watch_service_health_worker_task")[0]
        self.assertIn("memory_watch_service_set_upload_worker_busy(false)", upload_worker_section)
        self.assertIn("memory_watch_service_is_wait_canceled_request(job.request_id)", upload_worker_section)

    def test_service_keeps_official_chat_and_ui_boundaries(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertNotIn("official_chat", source)
        self.assertNotIn("official_chat", header)
        self.assertNotIn("lvgl", source.lower())
        self.assertNotIn("lv_obj", source)
        self.assertNotIn("ai_chat_view", source)

    def test_main_cmake_registers_memory_watch_service(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/memory_watch_service.c",
            cmake,
        )


if __name__ == "__main__":
    unittest.main()
