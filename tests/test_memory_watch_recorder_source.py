import unittest

from tests.main_paths import MAIN_CMAKE
from tests.main_paths import MEMORY_WATCH_RECORDER_HEADER
from tests.main_paths import MEMORY_WATCH_RECORDER_SOURCE


class MemoryWatchRecorderSourceTests(unittest.TestCase):
    def test_recorder_header_and_source_exist(self) -> None:
        self.assertTrue(MEMORY_WATCH_RECORDER_HEADER.exists())
        self.assertTrue(MEMORY_WATCH_RECORDER_SOURCE.exists())

    def test_recorder_exposes_blocking_capture_api(self) -> None:
        header = MEMORY_WATCH_RECORDER_HEADER.read_text(encoding="utf-8")

        self.assertIn("memory_watch_recorder_config_t", header)
        self.assertIn("memory_watch_recorder_result_t", header)
        self.assertIn("memory_watch_recorder_should_stop_cb_t", header)
        self.assertIn("memory_watch_recorder_should_abort_cb_t", header)
        self.assertIn("MEMORY_WATCH_RECORDER_OPUS_FRAME_DURATION_MS 60U", header)
        self.assertIn("MEMORY_WATCH_RECORDER_DEFAULT_READ_TIMEOUT_MS 500U", header)
        self.assertIn("uint32_t read_timeout_ms", header)
        self.assertIn("should_abort_cb", header)
        self.assertIn("memory_watch_recorder_capture_ogg_opus", header)
        self.assertIn("memory_watch_ogg_write_cb_t write_cb", header)

    def test_recorder_owns_audio_session_and_background_pause(self) -> None:
        source = MEMORY_WATCH_RECORDER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "audio_codec.h"', source)
        self.assertIn('#include "services/safety/background_service_manager.h"', source)
        self.assertIn("background_service_manager_set_foreground_audio_active", source)
        self.assertIn("true, \"memory_watch_recording\"", source)
        self.assertIn("false, \"memory_watch_recording_done\"", source)
        self.assertIn("audio_codec_init()", source)
        self.assertIn("audio_codec_deinit()", source)
        self.assertIn("audio_codec_acquire_input(", source)
        self.assertIn("AUDIO_CODEC_OWNER_HERMES", source)
        self.assertIn("audio_codec_release_input(AUDIO_CODEC_OWNER_HERMES)", source)

    def test_recorder_encodes_opus_and_uses_ogg_muxer(self) -> None:
        source = MEMORY_WATCH_RECORDER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "encoder/impl/esp_opus_enc.h"', source)
        self.assertIn("esp_opus_enc_open", source)
        self.assertIn("esp_opus_enc_get_frame_size", source)
        self.assertIn("esp_opus_enc_process", source)
        self.assertIn("esp_opus_enc_close", source)
        self.assertIn("ESP_AUDIO_SAMPLE_RATE_16K", source)
        self.assertIn("ESP_AUDIO_MONO", source)
        self.assertIn("ESP_OPUS_ENC_FRAME_DURATION_60_MS", source)
        self.assertIn("memory_watch_ogg_opus_muxer_init", source)
        self.assertIn("memory_watch_ogg_opus_muxer_write_audio_packet", source)

    def test_recorder_converts_hw_pcm_to_16k_mono_without_official_chat(self) -> None:
        source = MEMORY_WATCH_RECORDER_SOURCE.read_text(encoding="utf-8")
        header = MEMORY_WATCH_RECORDER_HEADER.read_text(encoding="utf-8")
        combined = source + "\n" + header

        self.assertIn("memory_watch_recorder_convert_hw_to_opus_frame", source)
        self.assertIn("AUDIO_PLATFORM_HW_SAMPLE_RATE", source)
        self.assertIn("AUDIO_PLATFORM_SR_SAMPLE_RATE", source)
        self.assertIn("AUDIO_PLATFORM_HW_INPUT_CHANNELS", source)
        self.assertNotIn("official_chat", combined)
        self.assertNotIn('#include "lvgl', combined.lower())
        self.assertNotIn("lv_obj", combined)
        self.assertNotIn("esp_http_client", combined)

    def test_recorder_uses_finite_audio_read_timeout(self) -> None:
        source = MEMORY_WATCH_RECORDER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("memory_watch_recorder_normalize_read_timeout", source)
        self.assertIn("memory_watch_recorder_should_abort", source)
        self.assertIn("MEMORY_WATCH_RECORDER_DEFAULT_READ_TIMEOUT_MS", source)
        self.assertIn("read_timeout_ticks", source)
        self.assertNotIn("audio_codec_read(hw_pcm, hw_bytes, &bytes_read, portMAX_DELAY)", source)

    def test_main_cmake_registers_recorder_and_audio_codec_dependency(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/memory_watch/memory_watch_recorder.c",
            cmake,
        )
        self.assertIn("espressif__esp_audio_codec", cmake)


if __name__ == "__main__":
    unittest.main()
