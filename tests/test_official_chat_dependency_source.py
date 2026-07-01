import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
SDKCONFIG = REPO_ROOT / "sdkconfig"
DEPENDENCIES_LOCK = REPO_ROOT / "dependencies.lock"


class OfficialChatDependencySourceTests(unittest.TestCase):
    def test_sdkconfig_contains_minimal_official_chat_keys(self) -> None:
        source = SDKCONFIG.read_text(encoding="utf-8")

        self.assertIn('CONFIG_OFFICIAL_CHAT_LANGUAGE_CODE="zh-CN"', source)
        self.assertIn(
            'CONFIG_OFFICIAL_CHAT_OTA_URL="https://api.tenclass.net/xiaozhi/ota/"',
            source,
        )
        self.assertIn("CONFIG_OFFICIAL_CHAT_UDP_AUDIO_STALL_TIMEOUT_MS=5000", source)
        self.assertIn("CONFIG_LWIP_SO_RCVBUF=y", source)
        self.assertIn("CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=16", source)
        self.assertIn("CONFIG_LWIP_UDP_RECVMBOX_SIZE=6", source)

    def test_sdkconfig_keeps_local_wake_word_model_disabled(self) -> None:
        source = SDKCONFIG.read_text(encoding="utf-8")

        self.assertIn("# CONFIG_USE_DEVICE_AEC is not set", source)
        self.assertIn("# CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS is not set", source)

    def test_sdkconfig_contains_low_risk_ai_memory_strategy_keys(self) -> None:
        source = SDKCONFIG.read_text(encoding="utf-8")

        self.assertIn("CONFIG_SPIRAM_USE_MALLOC=y", source)
        self.assertIn("# CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP is not set", source)
        self.assertIn("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384", source)
        self.assertIn("CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=131072", source)
        self.assertIn("CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y", source)
        self.assertIn("CONFIG_MBEDTLS_DYNAMIC_BUFFER=y", source)
        self.assertNotIn("CONFIG_SPIRAM_USE_CAPS_ALLOC=y", source)

    def test_dependencies_lock_contains_managed_components_needed_by_official_chat(self) -> None:
        source = DEPENDENCIES_LOCK.read_text(encoding="utf-8")

        self.assertIn("espressif/esp_audio_codec", source)
        self.assertIn("espressif/esp_audio_effects", source)
        self.assertIn("espressif/esp-sr", source)


if __name__ == "__main__":
    unittest.main()
