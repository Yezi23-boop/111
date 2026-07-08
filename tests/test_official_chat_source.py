import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
OFFICIAL_CHAT_DIR = REPO_ROOT / "components" / "official_chat"
UTILS_DIR = REPO_ROOT / "components" / "utils"
OFFICIAL_CHAT_CMAKE = OFFICIAL_CHAT_DIR / "CMakeLists.txt"
OFFICIAL_CHAT_APPLICATION = OFFICIAL_CHAT_DIR / "application.cc"
OFFICIAL_CHAT_MCP = OFFICIAL_CHAT_DIR / "mcp_server.cc"
OFFICIAL_CHAT_MQTT = OFFICIAL_CHAT_DIR / "protocols" / "mqtt_protocol.cc"
OFFICIAL_CHAT_HEADER = OFFICIAL_CHAT_DIR / "include" / "official_chat.h"
OFFICIAL_CHAT_C_API = OFFICIAL_CHAT_DIR / "official_chat_c_api.cc"
OFFICIAL_CHAT_OTA = OFFICIAL_CHAT_DIR / "ota.cc"
OFFICIAL_CHAT_AFE_WAKE_WORD = (
    OFFICIAL_CHAT_DIR / "audio" / "wake_words" / "afe_wake_word.cc"
)
MAIN_MANIFEST = REPO_ROOT / "main" / "idf_component.yml"
MAIN_KCONFIG = REPO_ROOT / "main" / "Kconfig.projbuild"


class OfficialChatSourceTests(unittest.TestCase):
    def test_official_chat_component_skeleton_exists(self) -> None:
        expected_paths = [
            OFFICIAL_CHAT_CMAKE,
            OFFICIAL_CHAT_DIR / "include" / "official_chat.h",
            OFFICIAL_CHAT_DIR / "official_chat_c_api.cc",
            OFFICIAL_CHAT_DIR / "application.cc",
            OFFICIAL_CHAT_DIR / "application.h",
            OFFICIAL_CHAT_DIR / "mcp_server.cc",
            OFFICIAL_CHAT_DIR / "mcp_server.h",
            OFFICIAL_CHAT_DIR / "ota.cc",
            OFFICIAL_CHAT_DIR / "settings.cc",
            OFFICIAL_CHAT_DIR / "protocol_config.cc",
            OFFICIAL_CHAT_DIR / "audio" / "audio_service.cc",
            OFFICIAL_CHAT_DIR / "audio" / "local_audio_codec_adapter.cc",
            OFFICIAL_CHAT_DIR / "audio" / "processors" / "afe_audio_processor.cc",
            OFFICIAL_CHAT_DIR / "audio" / "wake_words" / "afe_wake_word.cc",
            OFFICIAL_CHAT_DIR / "protocols" / "mqtt_protocol.cc",
            OFFICIAL_CHAT_DIR / "protocols" / "websocket_protocol.cc",
            OFFICIAL_CHAT_DIR / "board_metadata" / "esp32-s3-touch-amoled-2.06.json",
            UTILS_DIR / "CMakeLists.txt",
            UTILS_DIR / "include" / "system_util.h",
            UTILS_DIR / "system_util.c",
        ]

        for path in expected_paths:
            self.assertTrue(path.exists(), f"missing expected file: {path}")

    def test_official_chat_component_keeps_required_managed_deps_after_wifi_migration(self) -> None:
        cmake = OFFICIAL_CHAT_CMAKE.read_text(encoding="utf-8")

        self.assertIn("wifi_control", cmake)
        self.assertNotIn("wifi_provision", cmake)
        self.assertNotIn("hal_wifi", cmake)
        self.assertIn("utils", cmake)
        self.assertIn("esp_audio_effects", cmake)
        self.assertIn("esp-sr", cmake)
        self.assertIn("espressif__esp_audio_codec", cmake)

    def test_official_chat_runtime_uses_wifi_control_helpers(self) -> None:
        application_source = OFFICIAL_CHAT_APPLICATION.read_text(encoding="utf-8")
        mcp_source = OFFICIAL_CHAT_MCP.read_text(encoding="utf-8")

        self.assertIn('#include "wifi_control.h"', application_source)
        self.assertNotIn('#include "wifi_provision.h"', application_source)
        self.assertNotIn('#include "hal_wifi.h"', application_source)
        self.assertIn("wifi_control_set_power_save(false);", application_source)
        self.assertIn("wifi_control_set_power_save(true);", application_source)
        self.assertIn("wifi_control_is_connected()", mcp_source)
        self.assertIn("wifi_control_get_ip(", mcp_source)
        self.assertNotIn("wifi_provision_is_connected()", mcp_source)
        self.assertNotIn("wifi_provision_get_ip(", mcp_source)
        self.assertNotIn("hal_wifi_get_connect_state()", mcp_source)
        self.assertNotIn("hal_wifi_get_ip()", mcp_source)

    def test_main_manifest_includes_minimal_managed_dependencies_for_official_chat(self) -> None:
        manifest = MAIN_MANIFEST.read_text(encoding="utf-8")

        self.assertIn("espressif/esp_audio_codec", manifest)
        self.assertIn("espressif/esp_audio_effects", manifest)
        self.assertIn("espressif/esp-sr", manifest)

    def test_official_chat_uses_configurable_udp_audio_stall_timeout(self) -> None:
        source = OFFICIAL_CHAT_MQTT.read_text(encoding="utf-8")
        kconfig = MAIN_KCONFIG.read_text(encoding="utf-8")

        self.assertIn("config OFFICIAL_CHAT_UDP_AUDIO_STALL_TIMEOUT_MS", kconfig)
        self.assertIn("default 5000", kconfig)
        self.assertIn("CONFIG_OFFICIAL_CHAT_UDP_AUDIO_STALL_TIMEOUT_MS", source)
        self.assertNotIn(
            "constexpr int64_t kUdpAudioStallTimeoutUs = 2500000;", source
        )

    def test_official_chat_mqtt_shutdown_relies_on_stop_without_manual_disconnect(self) -> None:
        source = OFFICIAL_CHAT_MQTT.read_text(encoding="utf-8")

        self.assertNotIn("esp_mqtt_client_disconnect(mqtt_client_handle_);",
                         source)
        self.assertNotIn("waiting for mqtt disconnected event during stop",
                         source)
        self.assertNotIn("mqtt disconnect event not observed before stop timeout",
                         source)
        self.assertIn("esp_mqtt_client_stop(mqtt_client_handle_);", source)
        self.assertIn("esp_mqtt_client_destroy(mqtt_client_handle_);", source)

    def test_official_chat_shutdown_fences_worker_reentry(self) -> None:
        header = OFFICIAL_CHAT_HEADER.read_text(encoding="utf-8")
        app = OFFICIAL_CHAT_APPLICATION.read_text(encoding="utf-8")
        c_api = OFFICIAL_CHAT_C_API.read_text(encoding="utf-8")

        self.assertIn("esp_err_t official_chat_prepare_shutdown(", header)
        self.assertIn("return handle->app.PrepareForShutdown();", c_api)
        self.assertIn("esp_err_t Application::PrepareForShutdown()", app)
        self.assertIn("shutting_down_", app)
        self.assertIn("shutdown ignoring toggle chat event", app)
        self.assertIn("shutdown ignoring start listening event", app)
        self.assertIn("shutdown ignoring wake word event", app)
        self.assertIn("shutdown ignoring activation done event", app)
        self.assertIn("StopListening 是 shutdown 收敛路径的一部分", app)
        self.assertIn("xEventGroupSetBits(event_group_, kMainEventStopListening);",
                      app)

    def test_official_chat_exposes_audio_channel_preconnect(self) -> None:
        header = OFFICIAL_CHAT_HEADER.read_text(encoding="utf-8")
        app_header = (OFFICIAL_CHAT_DIR / "application.h").read_text(
            encoding="utf-8"
        )
        app = OFFICIAL_CHAT_APPLICATION.read_text(encoding="utf-8")
        c_api = OFFICIAL_CHAT_C_API.read_text(encoding="utf-8")

        self.assertIn("official_chat_prepare_audio_channel(", header)
        self.assertIn("official_chat_is_audio_channel_ready(", header)
        self.assertIn("esp_err_t PrepareAudioChannel();", app_header)
        self.assertIn("bool IsAudioChannelReady() const;", app_header)
        self.assertIn("kMainEventPrepareAudioChannel", app_header)
        self.assertIn("HandlePrepareAudioChannelEvent", app)
        self.assertIn("ContinuePrepareAudioChannel", app)
        self.assertIn("return handle->app.PrepareAudioChannel();", c_api)
        self.assertIn("return handle->app.IsAudioChannelReady();", c_api)

    def test_official_chat_wake_word_fetch_uses_bounded_shutdown_wait(self) -> None:
        source = OFFICIAL_CHAT_AFE_WAKE_WORD.read_text(encoding="utf-8")

        self.assertIn("kDetectionFetchTimeoutTicks", source)
        self.assertIn("pdMS_TO_TICKS(50)", source)
        self.assertIn(
            "fetch_with_delay(afe_data_, kDetectionFetchTimeoutTicks)",
            source,
        )
        self.assertNotIn("fetch_with_delay(afe_data_, portMAX_DELAY)", source)

    def test_official_chat_audio_encode_queue_aligns_with_official_stl_queue(self) -> None:
        header = (OFFICIAL_CHAT_DIR / "audio" / "audio_service.h").read_text(
            encoding="utf-8"
        )
        source = (OFFICIAL_CHAT_DIR / "audio" / "audio_service.cc").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("class FixedAudioTaskQueue", header)
        self.assertNotIn("FixedAudioTaskQueue audio_encode_queue_;", header)
        self.assertIn(
            "std::deque<std::unique_ptr<AudioTask>> audio_encode_queue_",
            header,
        )
        self.assertIn("auto task = std::move(audio_encode_queue_.front());", source)
        self.assertIn("audio_encode_queue_.pop_front();", source)
        self.assertIn(
            "audio_encode_queue_.size() < kMaxEncodeTasksInQueue",
            source,
        )
        self.assertIn("audio_encode_queue_.push_back(std::move(task));", source)

    def test_official_chat_ota_waits_for_valid_system_time_before_https(self) -> None:
        source = OFFICIAL_CHAT_OTA.read_text(encoding="utf-8")
        header = OFFICIAL_CHAT_HEADER.read_text(encoding="utf-8")
        service = (REPO_ROOT / "main" / "services" / "official_chat_service.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("EnsureSystemTimeValidForTls", source)
        self.assertIn("official_chat_ensure_time_cb_t", header)
        self.assertIn("ensure_time_valid_", source)
        self.assertIn("ensure_time_valid(kSntpSyncTimeoutMs, user_ctx)", source)
        self.assertIn("skip https request due to invalid system time", source)
        self.assertIn("system_time_service_ensure_valid_for_tls(timeout_ms)", service)
        self.assertNotIn("#include <esp_sntp.h>", source)
        self.assertNotIn("esp_sntp_init", source)
        self.assertNotIn("esp_sntp_restart", source)

    def test_official_chat_ota_logs_tls_diagnostics_for_certificate_failures(self) -> None:
        source = OFFICIAL_CHAT_OTA.read_text(encoding="utf-8")

        self.assertIn("LogHttpTlsDiagnostics", source)
        self.assertIn("esp_http_client_get_and_clear_last_tls_error", source)
        self.assertIn("http tls diagnostics stage=", source)
        self.assertIn("mbedtls -0x2700 usually means certificate validation failed", source)

    def test_official_chat_skips_local_sr_model_loader(self) -> None:
        source = OFFICIAL_CHAT_APPLICATION.read_text(encoding="utf-8")

        namespace_end = "}  // namespace official_chat"
        if namespace_end not in source:
            namespace_end = "} // namespace official_chat"
        init_body = source[
            source.index("esp_err_t Application::InitializeAudioService()")
            : source.index(namespace_end)
        ]
        self.assertIn("models_list_.reset();", init_body)
        self.assertIn("不需要本地 wake word", init_body)
        self.assertNotIn('esp_srmodel_init("model")', init_body)
        self.assertNotIn("OfficialChatSrModelPartitionLooksLoadable", source)


if __name__ == "__main__":
    unittest.main()
