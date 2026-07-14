import unittest

from tests.main_paths import AUDIO_DIAG_SERVICES_DIR, MAIN_CMAKE


MIC_TEST_SOURCE = AUDIO_DIAG_SERVICES_DIR / "audio_mic_test_service.c"
MIC_TEST_HEADER = AUDIO_DIAG_SERVICES_DIR / "audio_mic_test_service.h"


class AudioMicTestServiceSourceTests(unittest.TestCase):
    def test_public_api_and_snapshot_contract_exist(self) -> None:
        header = MIC_TEST_HEADER.read_text(encoding="utf-8")

        self.assertIn("audio_mic_test_service_start", header)
        self.assertIn("audio_mic_test_service_get_snapshot", header)
        self.assertIn("audio_mic_test_snapshot_t", header)
        self.assertIn("AUDIO_MIC_TEST_STATE_RUNNING", header)
        self.assertIn("AUDIO_MIC_TEST_STATE_PASSED", header)
        self.assertIn("AUDIO_MIC_TEST_STATE_FAILED", header)
        self.assertIn("wav_path", header)
        self.assertIn("json_path", header)
        self.assertIn("channel_stats", header)
        self.assertIn("rms", header)
        self.assertIn("peak", header)
        self.assertIn("zero_percent", header)

    def test_service_uses_one_shot_freertos_task(self) -> None:
        source = MIC_TEST_SOURCE.read_text(encoding="utf-8")

        self.assertIn("xTaskCreate(audio_mic_test_task", source)
        self.assertIn('\"mic_test\"', source)
        self.assertIn("vTaskDelete(NULL);", source)
        self.assertIn("ESP_ERR_INVALID_STATE", source)
        self.assertIn("s_mic_test.running", source)
        self.assertIn("taskENTER_CRITICAL(&s_mic_test.lock)", source)

    def test_service_acquires_audio_owner_and_restores_foreground_audio(self) -> None:
        source = MIC_TEST_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            'background_service_manager_set_foreground_audio_active(\n'
            '        true, "mic_test")',
            source,
        )
        self.assertIn(
            "audio_codec_acquire_input(AUDIO_CODEC_OWNER_AUDIO_RECORDER",
            source,
        )
        self.assertIn("audio_codec_set_record_gain(kRecordGainDb)", source)
        self.assertIn("MIC_TEST: GAIN requested_db=%.1f result=%s", source)
        self.assertIn("audio_codec_read(buffer", source)
        self.assertIn(
            "audio_codec_release_input(AUDIO_CODEC_OWNER_AUDIO_RECORDER)",
            source,
        )
        self.assertIn(
            'background_service_manager_set_foreground_audio_active(\n'
            '            false, "mic_test_done")',
            source,
        )

    def test_service_writes_raw_hardware_wav_and_json_report(self) -> None:
        source = MIC_TEST_SOURCE.read_text(encoding="utf-8")

        self.assertIn('static const char *kOutputDir = "/sdcard/mic_tests";', source)
        self.assertIn("AUDIO_PLATFORM_HW_SAMPLE_RATE", source)
        self.assertIn("AUDIO_PLATFORM_HW_INPUT_CHANNELS", source)
        self.assertIn("AUDIO_PLATFORM_HW_BITS_PER_SAMPLE", source)
        self.assertIn("sd_manager_create_dir(kOutputDir)", source)
        self.assertIn('"%s.tmp"', source)
        self.assertIn("sd_manager_rename_file(tmp_wav_path, snapshot.wav_path)", source)
        self.assertIn("sd_manager_write_file(tmp_path, json, strlen(json))", source)
        self.assertIn("sd_manager_rename_file(tmp_path, snapshot->json_path)", source)

    def test_service_pass_fail_thresholds_are_present(self) -> None:
        source = MIC_TEST_SOURCE.read_text(encoding="utf-8")

        self.assertIn("kPeakPassThreshold = 500U", source)
        self.assertIn("kRmsPassThreshold = 50.0f", source)
        self.assertIn("kZeroPassThreshold = 95.0f", source)
        self.assertIn("snapshot->bytes_read >= minimum_bytes", source)
        self.assertIn("other_channel_has_input", source)
        self.assertIn("通道不匹配", source)
        self.assertIn("MIC_TEST: CH%u", source)
        self.assertIn("MIC_TEST: SUMMARY mic_channel=%u", source)
        self.assertIn("MIC_TEST: DONE status=%s", source)

    def test_main_cmake_includes_service(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn("services/audio_diag/audio_mic_test_service.c", cmake)


if __name__ == "__main__":
    unittest.main()
