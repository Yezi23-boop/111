import unittest

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MAIN_KCONFIG
from tests.main_paths import MEMORY_WATCH_SERVICE_HEADER
from tests.main_paths import MEMORY_WATCH_SERVICE_SOURCE
from tests.main_paths import MEMORY_WATCH_VOICE_CLIENT_HEADER
from tests.main_paths import MEMORY_WATCH_VOICE_CLIENT_SOURCE


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
        self.assertIn("memory_watch_service_save_endpoint_to_nvs", header)
        self.assertIn("memory_watch_service_endpoint_snapshot_t", header)
        self.assertIn("memory_watch_service_copy_endpoint_config", header)
        self.assertIn("memory_watch_service_is_endpoint_configured", header)
        self.assertIn("memory_watch_service_check_health", header)
        self.assertIn("memory_watch_service_begin_recording", header)
        self.assertIn("memory_watch_service_send_recording", header)
        self.assertIn("memory_watch_service_send_text", header)
        self.assertNotIn("memory_watch_service_post_danger_alert", header)
        self.assertIn("memory_watch_service_cancel_recording", header)
        self.assertIn("memory_watch_service_cancel_waiting", header)
        self.assertIn("memory_watch_service_cancel_clarification", header)
        self.assertIn("memory_watch_service_get_snapshot", header)

    def test_service_uses_owner_task_queue_and_snapshot(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/network/network_service.h"', source)
        self.assertIn('#include "nvs.h"', source)
        self.assertIn('#include "services/memory_watch/memory_watch_voice_client.h"', source)
        self.assertIn('#include "esp_random.h"', source)
        self.assertIn("xQueueCreateStatic(", source)
        self.assertIn("xQueueSend(", source)
        self.assertIn("xQueueReceive(", source)
        self.assertIn('#include "freertos/idf_additions.h"', source)
        self.assertIn("kUploadWorkerStackWords = 24576", source)
        self.assertIn("xTaskCreateWithCaps(", source)
        self.assertIn("MALLOC_CAP_SPIRAM", source)
        self.assertNotIn("s_upload_worker_task_stack", source)
        self.assertIn("s_upload_worker_queue", source)
        self.assertIn("s_cancel_worker_queue", source)
        self.assertIn("s_health_worker_queue", source)
        self.assertIn("memory_watch_service_upload_worker_task", source)
        self.assertIn("memory_watch_service_cancel_worker_task", source)
        self.assertIn("memory_watch_service_health_worker_task", source)
        self.assertNotIn("memory_watch_service_alert_worker_task", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CMD_SEND_TEXT", source)
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

    def test_service_exposes_endpoint_snapshot_but_not_alert_worker(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_service_endpoint_snapshot_t", header)
        self.assertIn("memory_watch_service_copy_endpoint_config", header)
        self.assertIn("memory_watch_service_copy_endpoint_config", source)
        self.assertIn("out_config->base_url", source)
        self.assertIn("out_config->device_id", source)
        self.assertIn("out_config->device_token", source)
        self.assertIn("out_config->allow_insecure_http", source)
        self.assertNotIn("memory_watch_service_danger_alert_t", header)
        self.assertNotIn("MEMORY_WATCH_SERVICE_DANGER_TYPE_MAX_BYTES", header)
        self.assertNotIn("MEMORY_WATCH_SERVICE_DANGER_MESSAGE_MAX_BYTES", header)
        self.assertNotIn("s_alert_worker_queue", source)
        self.assertNotIn("kDangerAlertTimeoutMs = 8000U", source)
        self.assertNotIn("memory_watch_service_post_danger_alert", source)
        self.assertNotIn("memory_watch_voice_client_post_danger_alert", source)
        self.assertNotIn("static volatile", source)

    def test_service_configures_endpoint_and_health_without_secrets(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")
        combined = source + "\n" + header

        self.assertIn("CONFIG_MEMORY_WATCH_DEFAULT_ENDPOINT_ENABLED", source)
        self.assertIn("CONFIG_MEMORY_WATCH_DEFAULT_BASE_URL", source)
        self.assertIn("CONFIG_MEMORY_WATCH_DEFAULT_DEVICE_ID", source)
        self.assertIn("CONFIG_MEMORY_WATCH_DEFAULT_DEVICE_TOKEN", source)
        self.assertIn("CONFIG_MEMORY_WATCH_DEFAULT_TIMEOUT_MS", source)
        self.assertIn("CONFIG_MEMORY_WATCH_DEFAULT_ALLOW_HTTP", source)
        self.assertIn("memory_watch_service_load_kconfig_endpoint_default", source)
        self.assertNotIn("memory_watch_dev_endpoint_local.h", source)
        self.assertNotIn("MEMORY_WATCH_DEV_ENDPOINT", source)
        self.assertIn("memory_watch_service_endpoint_state_t", source)
        self.assertIn("memory_watch_service_copy_required_text", source)
        self.assertIn("memory_watch_service_is_safe_endpoint_text", source)
        self.assertIn("memory_watch_service_validate_endpoint_state", source)
        self.assertIn('strncmp(state->base_url, "http://", 7)', source)
        self.assertIn('strncmp(state->base_url, "https://", 8)', source)
        self.assertIn("uses_http && !state->allow_insecure_http", source)
        self.assertIn("*p == '\\r' || *p == '\\n'", source)
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

    def test_service_saves_runtime_endpoint_config_to_nvs_without_secret_logs(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")
        combined = source + "\n" + header

        self.assertIn("memory_watch_service_save_endpoint_to_nvs", header)
        self.assertIn("memory_watch_service_build_endpoint_state", source)
        self.assertIn("nvs_open(kEndpointNvsNamespace, NVS_READWRITE", source)
        self.assertIn("nvs_set_str(handle, kEndpointNvsBaseUrlKey", source)
        self.assertIn("nvs_set_str(handle, kEndpointNvsDeviceIdKey", source)
        self.assertIn("nvs_set_str(handle, kEndpointNvsDeviceTokenKey", source)
        self.assertIn("nvs_set_u32(handle, kEndpointNvsTimeoutMsKey", source)
        self.assertIn("nvs_set_u8(handle, kEndpointNvsAllowHttpKey", source)
        self.assertIn("nvs_commit(handle)", source)
        self.assertIn("watch endpoint config saved to NVS: device_id=%s", source)
        self.assertIn("snapshot.request_active", source)
        self.assertNotIn("device_token=%s", combined)
        self.assertNotIn("HERMES_API_KEY", combined)
        self.assertNotIn("API_SERVER_KEY", combined)
        self.assertNotIn("XIAOMI_API_KEY", combined)

    def test_service_generates_contract_request_id_but_does_not_upload_in_owner(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_random()", source)
        self.assertIn('"%s-%08" PRIx32 "-%04" PRIu32', source)
        self.assertIn("s_request_seq", source)
        self.assertIn("memory_watch_service_set_request_id(request_id)", source)
        self.assertIn("case MEMORY_WATCH_SERVICE_STATE_NEEDS_CLARIFICATION", source)

    def test_service_runs_recorder_and_http_only_in_workers(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/memory_watch/memory_watch_recorder.h"', source)
        self.assertIn('#include "esp_heap_caps.h"', source)
        self.assertIn("memory_watch_service_upload_worker_task", source)
        self.assertIn("memory_watch_service_health_worker_task", source)
        self.assertIn("memory_watch_recorder_capture_ogg_opus", source)
        self.assertIn("memory_watch_voice_client_post_voice_command", source)
        self.assertIn("memory_watch_voice_client_post_text_command", source)
        self.assertIn("memory_watch_voice_client_get_health", source)
        self.assertIn("memory_watch_service_audio_write_cb", source)
        self.assertIn("memory_watch_service_should_abort_recording", source)
        self.assertIn(".should_abort_cb = memory_watch_service_should_abort_recording", source)
        self.assertIn("MEMORY_WATCH_VOICE_CLIENT_MAX_AUDIO_BYTES", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)
        self.assertIn("memory_watch_service_alloc_audio_psram", source)

        audio_alloc_section = source.split(
            "static void *memory_watch_service_alloc_audio_psram"
        )[1].split("static void memory_watch_service_free", 1)[0]
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", audio_alloc_section)
        self.assertNotIn("MALLOC_CAP_INTERNAL", audio_alloc_section)
        self.assertNotIn("heap_caps_malloc(len, MALLOC_CAP_8BIT)", audio_alloc_section)

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

    def test_service_rebinds_worker_client_config_after_queue_copy(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("memory_watch_service_rebind_client_config", source)
        self.assertIn("config->client_config.base_url = config->base_url", source)
        self.assertIn("config->client_config.device_id = config->device_id", source)
        self.assertIn(
            "config->client_config.device_token = config->device_token", source
        )

        for function_name in (
            "memory_watch_service_start_upload_job",
            "memory_watch_service_start_cancel_job",
            "memory_watch_service_start_health_job",
        ):
            section = source.split(f"static esp_err_t {function_name}")[1].split(
                "if (xQueueSend", 1
            )[0]
            self.assertIn(
                "memory_watch_service_rebind_client_config(&job.client_config)",
                section,
            )

        upload_worker_section = source.split(
            "static void memory_watch_service_upload_worker_task"
        )[1].split("static void memory_watch_service_health_worker_task")[0]
        self.assertIn(
            "memory_watch_service_rebind_client_config(&job->client_config)",
            upload_worker_section,
        )

        health_worker_section = source.split(
            "static void memory_watch_service_health_worker_task"
        )[1].split("static void memory_watch_service_cancel_worker_task")[0]
        self.assertIn(
            "memory_watch_service_rebind_client_config(&job.client_config)",
            health_worker_section,
        )

        cancel_worker_section = source.split(
            "static void memory_watch_service_cancel_worker_task"
        )[1].split("static void memory_watch_service_task")[0]
        self.assertIn(
            "memory_watch_service_rebind_client_config(&job.client_config)",
            cancel_worker_section,
        )

        inbox_get_config_section = source.split(
            "static bool memory_watch_service_inbox_get_client_config"
        )[1].split("static void memory_watch_service_inbox_set_meta")[0]
        self.assertIn(
            "memory_watch_service_rebind_client_config(out)",
            inbox_get_config_section,
        )

        inbox_worker_section = source.split(
            "static void memory_watch_service_inbox_worker_task"
        )[1].split("static void memory_watch_service_inbox_merge_staging")[0]
        self.assertIn(
            "memory_watch_service_rebind_client_config(&job.client_config)",
            inbox_worker_section,
        )

    def test_service_retries_background_https_without_dropping_pending_work(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("kBackgroundHttpsRetryIntervalMs", source)
        self.assertIn("s_health_check_pending", source)
        self.assertIn("s_health_retry_next_due_ms", source)
        self.assertIn("memory_watch_service_schedule_health_retry", source)
        self.assertIn("watch endpoint health transient failure: keep_online", source)
        self.assertIn(
            "memory_watch_service_set_endpoint_snapshot(true, before.hermes_online)",
            source,
        )

        inbox_retry_section = source.rsplit(
            "static void memory_watch_service_inbox_handle_worker_result", 1
        )[1].split("esp_err_t memory_watch_service_get_inbox_meta")[0]
        self.assertIn("s_inbox_poll_next_due_ms", inbox_retry_section)
        self.assertIn("memory_watch_service_inbox_set_poll_pending(true)", inbox_retry_section)
        self.assertIn("inbox: poll failed, will retry in", inbox_retry_section)
        self.assertIn("MEMORY_WATCH_INBOX_SYNC_AUTH_ERROR", inbox_retry_section)
        self.assertIn("memory_watch_service_inbox_set_poll_pending(false)", inbox_retry_section)

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
        self.assertIn("memory_watch_service_is_wait_canceled_request(job->request_id)", upload_worker_section)
        self.assertIn("s_upload_worker_job", upload_worker_section)
        self.assertIn("s_upload_worker_result", source)
        self.assertNotIn("memory_watch_service_upload_job_t job;", upload_worker_section)
        self.assertNotIn("memory_watch_service_worker_result_t result", upload_worker_section)

    def test_service_sends_text_via_worker_without_recorder(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_service_send_text", header)
        self.assertIn("MEMORY_WATCH_SERVICE_CMD_SEND_TEXT", source)
        self.assertIn("memory_watch_service_is_safe_user_text", source)
        self.assertIn("memory_watch_service_handle_send_text", source)
        self.assertIn("job.text_command", source)
        self.assertIn("memory_watch_voice_client_text_request_t", source)
        self.assertIn("memory_watch_voice_client_post_text_command", source)
        self.assertIn("CONFIG_MEMORY_WATCH_BOOT_TEXT_SMOKE", source)
        self.assertIn("Kconfig boot text smoke started", source)

        text_handler_section = source.split(
            "static void memory_watch_service_handle_send_text"
        )[1].split("static void memory_watch_service_handle_check_health")[0]
        self.assertIn("memory_watch_service_start_upload_job", text_handler_section)
        self.assertNotIn("memory_watch_recorder_capture_ogg_opus", text_handler_section)
        self.assertNotIn("memory_watch_voice_client_post_text_command", text_handler_section)

        upload_worker_section = source.split(
            "if (job->text_command)"
        )[1].split("else", 1)[0]
        self.assertIn("memory_watch_voice_client_post_text_command", upload_worker_section)
        self.assertNotIn("memory_watch_recorder_capture_ogg_opus", upload_worker_section)

    def test_service_owns_recent_conversation_display_cache(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("MEMORY_WATCH_SERVICE_CONVERSATION_MAX_ITEMS 10U", header)
        self.assertIn("conversation_generation", header)
        self.assertIn("memory_watch_service_conversation_item_t", header)
        self.assertIn("memory_watch_service_copy_conversation_items", header)
        self.assertIn("s_conversation_items", source)
        self.assertIn("s_conversation_item_count", source)
        self.assertIn("s_conversation_generation", source)
        self.assertIn("memory_watch_service_append_conversation_item", source)
        self.assertIn("memory_watch_service_append_response_conversation", source)
        self.assertIn("memmove(&s_conversation_items[0]", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CONVERSATION_USER", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CONVERSATION_HERMES", source)
        append_section = source.split(
            "static void memory_watch_service_append_conversation_item"
        )[1].split("static void memory_watch_service_append_response_conversation")[0]
        self.assertIn("strcmp(s_conversation_items[i].request_id", append_section)
        self.assertIn("strcmp(s_conversation_items[i].text, text) == 0", append_section)

        ws_event_section = source.split(
            "static void memory_watch_service_ws_event_cb"
        )[1].split("static void memory_watch_service_ws_disconnect_cb")[0]
        self.assertNotIn('strcmp(event->type, "asr_result")', ws_event_section)
        self.assertNotIn('strcmp(event->type, "conversation_message")', ws_event_section)
        self.assertNotIn('strcmp(event->type, "error")', ws_event_section)
        self.assertIn("MEMORY_WATCH_WS_EVENT_TURN_ASR_READY", ws_event_section)
        self.assertIn("MEMORY_WATCH_WS_EVENT_REQUEST_ACCEPTED", ws_event_section)
        self.assertIn("MEMORY_WATCH_WS_EVENT_TURN_REPLY_MESSAGE", ws_event_section)
        self.assertIn("MEMORY_WATCH_WS_EVENT_TURN_ERROR", ws_event_section)
        self.assertIn("kWsWaitAsrReadyBit", source)
        self.assertIn("kWsWaitRequestAcceptedBit", source)
        asr_section = ws_event_section.split(
            "MEMORY_WATCH_WS_EVENT_TURN_ASR_READY"
        )[1].split(
            "MEMORY_WATCH_WS_EVENT_TURN_REPLY_MESSAGE", 1
        )[0]
        self.assertIn("memory_watch_service_append_conversation_item", asr_section)
        self.assertIn("MEMORY_WATCH_SERVICE_CONVERSATION_USER", asr_section)
        self.assertIn("xEventGroupSetBits(s_ws_wait_event_group, kWsWaitAsrReadyBit)", asr_section)
        ws_send_section = source.split(
            "static esp_err_t memory_watch_service_send_voice_over_ws"
        )[1].split("static esp_err_t memory_watch_service_post_worker_result", 1)[0]
        self.assertIn("bool asr_ready_seen = false", ws_send_section)
        self.assertIn("bool server_accepted_seen = false", ws_send_section)
        self.assertIn("if (server_accepted_seen || asr_ready_seen)", ws_send_section)
        self.assertIn("kWsWaitAsrReadyBit", ws_send_section)
        self.assertIn(
            "memory_watch_service_append_response_conversation(&result->response)",
            source,
        )

        copy_section = source.split(
            "esp_err_t memory_watch_service_copy_conversation_items"
        )[1].split("bool memory_watch_service_is_endpoint_configured", 1)[0]
        self.assertIn("portENTER_CRITICAL(&s_snapshot_lock)", copy_section)
        self.assertIn("out_items[i] = s_conversation_items[i]", copy_section)
        self.assertNotIn("esp_http_client", copy_section)
        self.assertNotIn("nvs_", copy_section)

    def test_v24_background_sync_owner_path(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_SERVICE_HEADER.read_text(encoding="utf-8")
        client_source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")
        client_header = MEMORY_WATCH_VOICE_CLIENT_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_service_set_foreground", header)
        self.assertIn("MEMORY_WATCH_SERVICE_CMD_SET_FOREGROUND", source)
        self.assertIn('#include "services/runtime_gate/foreground_runtime_gate.h"', source)
        self.assertIn("FOREGROUND_RUNTIME_OWNER_HERMES", source)
        self.assertIn("foreground_runtime_gate_acquire", source)
        self.assertIn("foreground_runtime_gate_release", source)
        self.assertIn("background_service_manager_notify_foreground_runtime_changed", source)
        self.assertIn("MEMORY_WATCH_SERVICE_CMD_CONVERSATION_POLL_DONE", source)
        self.assertIn("kConversationPollIntervalMs = 5000", source)
        self.assertIn("kConversationPollTimeoutMs = 4000U", source)
        self.assertIn("memory_watch_service_conversation_worker_task", source)
        self.assertIn("s_conversation_staging", source)
        self.assertIn("s_conversation_sync_result", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)
        self.assertIn("memory_watch_voice_client_sync", source)
        self.assertIn("memory_watch_voice_client_sync_cursor_t", source)
        self.assertIn("MEMORY_WATCH_SYNC_MODE_BACKGROUND", source)
        self.assertIn("MEMORY_WATCH_SYNC_MODE_FOREGROUND_RECONCILE", source)
        self.assertIn(".pending_request_id =", source)
        self.assertIn(".after_message_id =", source)
        self.assertIn(".max_messages = MEMORY_WATCH_SYNC_DEFAULT_MAX_MESSAGES", source)
        self.assertIn("conversation_pending", source)
        self.assertIn("memory_watch_service_start_conversation_polling", source)
        self.assertIn("memory_watch_service_start_foreground_reconcile", source)
        self.assertIn("memory_watch_ws_client_close();", source)
        self.assertIn("memory_watch_service_is_foreground_active()", source)
        self.assertIn("s_last_seen_conversation_id", source)
        self.assertIn(".last_seen_conversation_id = last_seen_conversation_id", source)
        self.assertIn("conversation_already_appended", source)
        self.assertIn("if (!result->conversation_already_appended)", source)
        self.assertIn("terminal_result.conversation_already_appended = true", source)
        self.assertIn('strcmp(s_conversation_sync_result.session_state, "done") != 0', source)
        self.assertIn('"sync_session_terminal"', source)
        self.assertNotIn("kConversationPendingMaxWaitMs", source)
        self.assertNotIn("s_conversation_poll_started_ms", source)
        self.assertNotIn("conversation_poll_timeout", source)
        self.assertNotIn("memory_watch_voice_client_conversation_poll(", source)

        self.assertIn("memory_watch_conversation_message_t", client_header)
        self.assertIn("MEMORY_WATCH_CONVERSATION_MAX_MESSAGES 20U", client_header)
        self.assertNotIn("MEMORY_WATCH_CONVERSATION_RESPONSE_MAX_BYTES", client_header)
        self.assertNotIn("memory_watch_conversation_poll_result_t", client_header)
        self.assertNotIn("memory_watch_voice_client_conversation_poll", client_header)
        self.assertNotIn("memory_watch_voice_client_conversation_poll(", client_source)
        self.assertNotIn("memory_watch_voice_client_build_conversation_path", client_source)
        self.assertNotIn("memory_watch_voice_client_parse_conversation_poll", client_source)
        self.assertNotIn('"/v1/watch/conversation?device_id="', client_source)
        self.assertNotIn('"&after="', client_source)
        self.assertIn('"messages"', client_source)

    def test_v24_voice_client_exposes_unified_sync_contract(self) -> None:
        client_source = MEMORY_WATCH_VOICE_CLIENT_SOURCE.read_text(encoding="utf-8")
        client_header = MEMORY_WATCH_VOICE_CLIENT_HEADER.read_text(encoding="utf-8")
        combined = client_source + "\n" + client_header

        self.assertIn("memory_watch_sync_mode_t", client_header)
        self.assertIn("MEMORY_WATCH_SYNC_MODE_BACKGROUND", client_header)
        self.assertIn("MEMORY_WATCH_SYNC_MODE_FOREGROUND_RECONCILE", client_header)
        self.assertIn("memory_watch_voice_client_sync_cursor_t", client_header)
        self.assertIn("memory_watch_voice_client_sync_result_t", client_header)
        self.assertIn("memory_watch_sync_inbox_summary_t", client_header)
        self.assertIn("MEMORY_WATCH_SYNC_DEFAULT_MAX_MESSAGES 10U", client_header)
        self.assertIn("MEMORY_WATCH_SYNC_RESPONSE_MAX_BYTES", client_header)
        self.assertIn("memory_watch_voice_client_sync", client_header)

        self.assertIn('"/v1/watch/sync?device_id="', client_source)
        self.assertIn('"&mode="', client_source)
        self.assertIn('"background"', client_source)
        self.assertIn('"foreground_reconcile"', client_source)
        self.assertIn('"&pending_request_id="', client_source)
        self.assertIn('"&after_message_id="', client_source)
        self.assertIn('"&max_messages="', client_source)
        self.assertIn('"schema_version"', client_source)
        self.assertIn('"conversation"', client_source)
        self.assertIn('"has_pending"', client_source)
        self.assertIn('"session_state"', client_source)
        self.assertIn('"inbox"', client_source)
        self.assertIn('"unread_count"', client_source)
        self.assertIn('"latest_unread"', client_source)
        self.assertIn('"notification_id"', client_source)
        self.assertIn("memory_watch_voice_client_parse_sync", client_source)
        self.assertIn("memory_watch_voice_client_parse_sync_latest_unread", client_source)
        self.assertIn("memory_watch_voice_client_is_public_session_state", client_source)
        self.assertIn("MEMORY_WATCH_SYNC_RESPONSE_MAX_BYTES + 1U", client_source)
        self.assertIn("memory_watch_voice_client_alloc", client_source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", client_source)
        self.assertIn("memory_watch_voice_client_perform_http_json", client_source)
        self.assertNotIn("poll_after_ms", combined)
        self.assertNotIn("after_inbox_id", combined)
        self.assertNotIn("max_inbox_items", combined)
        self.assertNotIn("accepted", combined)
        self.assertNotIn("asr_ready", combined)

    def test_main_kconfig_exposes_memory_watch_defaults_without_secret_value(self) -> None:
        kconfig = MAIN_KCONFIG.read_text(encoding="utf-8")

        self.assertIn('menu "AI Memory Watch"', kconfig)
        self.assertIn("config MEMORY_WATCH_DEFAULT_ENDPOINT_ENABLED", kconfig)
        self.assertIn("default y", kconfig)
        self.assertIn("config MEMORY_WATCH_DEFAULT_BASE_URL", kconfig)
        self.assertIn('default "https://watch.934000.xyz"', kconfig)
        self.assertIn("config MEMORY_WATCH_DEFAULT_DEVICE_ID", kconfig)
        self.assertIn('default "watch-001"', kconfig)
        self.assertIn("config MEMORY_WATCH_DEFAULT_DEVICE_TOKEN", kconfig)
        self.assertIn('default ""', kconfig)
        self.assertIn("config MEMORY_WATCH_DEFAULT_TIMEOUT_MS", kconfig)
        self.assertIn("default 120000", kconfig)
        self.assertIn("config MEMORY_WATCH_DEFAULT_ALLOW_HTTP", kconfig)
        self.assertIn("default n", kconfig)
        self.assertIn("config MEMORY_WATCH_BOOT_HEALTH_CHECK", kconfig)
        self.assertIn("config MEMORY_WATCH_BOOT_TEXT_SMOKE", kconfig)
        self.assertNotIn("WATCH_DEVICE_TOKENS", kconfig)
        self.assertNotIn("MIMO_ASR_API_KEY", kconfig)
        self.assertNotIn("HERMES_API_KEY", kconfig)

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
            "${CMAKE_CURRENT_LIST_DIR}/services/memory_watch/memory_watch_service.c",
            cmake,
        )

    def test_app_main_registers_ap_portal_endpoint_config_callback(self) -> None:
        source = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ap_portal_adapter.h"', source)
        self.assertIn("app_memory_watch_portal_config_cb", source)
        self.assertIn("ap_portal_adapter_set_memory_watch_config_callback", source)
        self.assertIn("memory_watch_service_save_endpoint_to_nvs", source)
        self.assertIn("app_memory_watch_portal_configured_cb", source)
        self.assertIn("ap_portal_adapter_set_memory_watch_configured_callback", source)
        self.assertIn("memory_watch_service_is_endpoint_configured", source)
        self.assertNotIn("HERMES_API_KEY", source)
        self.assertNotIn("API_SERVER_KEY", source)
        self.assertNotIn("XIAOMI_API_KEY", source)

    def test_conversation_text_truncation_keeps_utf8_boundary(self) -> None:
        source = MEMORY_WATCH_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("UTF-8 continuation bytes", source)
        self.assertIn("((const uint8_t *)src)[copy_len] & 0xC0U", source)
        self.assertIn("memory_watch_service_fill_pending_response(result, job->request_id)", source)
        self.assertIn("本地前台等待期限只控制 WS 资源占用", source)
        self.assertIn("if (server_accepted_seen || asr_ready_seen)", source)


if __name__ == "__main__":
    unittest.main()
