import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
OFFICIAL_CHAT_DIR = REPO_ROOT / "components" / "official_chat"
UTILS_DIR = REPO_ROOT / "components" / "utils"
OFFICIAL_CHAT_CMAKE = OFFICIAL_CHAT_DIR / "CMakeLists.txt"
OFFICIAL_CHAT_APPLICATION = OFFICIAL_CHAT_DIR / "application.cc"
OFFICIAL_CHAT_MCP = OFFICIAL_CHAT_DIR / "mcp_server.cc"
OFFICIAL_CHAT_MQTT = OFFICIAL_CHAT_DIR / "protocols" / "mqtt_protocol.cc"
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

    def test_official_chat_component_uses_wifi_provision_and_keeps_required_managed_deps(self) -> None:
        cmake = OFFICIAL_CHAT_CMAKE.read_text(encoding="utf-8")

        self.assertIn("wifi_provision", cmake)
        self.assertNotIn("hal_wifi", cmake)
        self.assertIn("utils", cmake)
        self.assertIn("esp_audio_effects", cmake)
        self.assertIn("esp-sr", cmake)
        self.assertIn("espressif__esp_audio_codec", cmake)

    def test_official_chat_runtime_uses_local_wifi_provision_helpers(self) -> None:
        application_source = OFFICIAL_CHAT_APPLICATION.read_text(encoding="utf-8")
        mcp_source = OFFICIAL_CHAT_MCP.read_text(encoding="utf-8")

        self.assertIn('#include "wifi_provision.h"', application_source)
        self.assertNotIn('#include "hal_wifi.h"', application_source)
        self.assertIn("wifi_provision_set_power_save(false);", application_source)
        self.assertIn("wifi_provision_set_power_save(true);", application_source)
        self.assertIn("wifi_provision_is_connected()", mcp_source)
        self.assertIn("wifi_provision_get_ip(", mcp_source)
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


if __name__ == "__main__":
    unittest.main()
