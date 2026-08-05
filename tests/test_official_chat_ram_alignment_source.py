import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SDKCONFIG = REPO_ROOT / "sdkconfig"
OFFICIAL_CHAT_DIR = REPO_ROOT / "components" / "official_chat"
AUDIO_SERVICE_HEADER = OFFICIAL_CHAT_DIR / "audio" / "audio_service.h"
AFE_AUDIO_PROCESSOR = OFFICIAL_CHAT_DIR / "audio" / "processors" / "afe_audio_processor.cc"
AFE_WAKE_WORD = OFFICIAL_CHAT_DIR / "audio" / "wake_words" / "afe_wake_word.cc"
ESP_SSL_SOURCE = OFFICIAL_CHAT_DIR / "net" / "esp_ssl.cc"
OFFICIAL_CHAT_SERVICE_SOURCE = REPO_ROOT / "main" / "services" / "official_chat_service.c"


class OfficialChatRamAlignmentSourceTests(unittest.TestCase):
    def test_sdkconfig_lwip_mailboxes_match_reference_project(self) -> None:
        source = SDKCONFIG.read_text(encoding="utf-8")

        self.assertIn("CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=16", source)
        self.assertIn("CONFIG_LWIP_UDP_RECVMBOX_SIZE=6", source)

    def test_audio_service_stacks_match_reference_project(self) -> None:
        source = AUDIO_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn(
            "constexpr configSTACK_DEPTH_TYPE kAudioInputTaskStackBytes = 6144;",
            source,
        )
        self.assertIn(
            "constexpr configSTACK_DEPTH_TYPE kAudioOutputTaskStackBytes = 4096;",
            source,
        )
        self.assertIn(
            "constexpr configSTACK_DEPTH_TYPE kAudioOpusTaskStackBytes = 24576;",
            source,
        )

    def test_afe_processor_stack_matches_reference_project(self) -> None:
        source = AFE_AUDIO_PROCESSOR.read_text(encoding="utf-8")

        self.assertIn('"afe_proc", 4096, this, 3, &worker_task_handle_', source)

    def test_wake_word_detection_stack_matches_reference_project(self) -> None:
        source = AFE_WAKE_WORD.read_text(encoding="utf-8")

        self.assertIn('"afe_wake", 4096, this, 3, &detection_task_handle_', source)
        self.assertIn(
            "constexpr size_t kWakeWordEncodeTaskStackBytes = 4096 * 6;", source
        )

    def test_ssl_receive_stack_uses_psram_and_reports_heap_pressure(self) -> None:
        source = ESP_SSL_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include <freertos/idf_additions.h>', source)
        self.assertIn("xTaskCreateWithCaps(", source)
        self.assertIn("MALLOC_CAP_SPIRAM)", source)
        self.assertIn("vTaskDeleteWithCaps(nullptr)", source)
        self.assertIn("internal_largest", source)
        self.assertIn("spiram_largest", source)

    def test_chat_text_history_uses_explicit_psram_storage(self) -> None:
        source = OFFICIAL_CHAT_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("official_chat_service_alloc_text_caches", source)
        self.assertIn("kMessageHistoryCapacity, sizeof(*s_message_history)", source)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", source)
        self.assertNotIn("static official_chat_service_message_t s_message_history[8]", source)


if __name__ == "__main__":
    unittest.main()
