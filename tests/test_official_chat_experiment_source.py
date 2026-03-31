import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_CMAKE = REPO_ROOT / "main" / "CMakeLists.txt"
MAIN_KCONFIG = REPO_ROOT / "main" / "Kconfig.projbuild"
MAIN_FORMAL_ENTRY = REPO_ROOT / "main" / "111.c"
MAIN_EXPERIMENT_ENTRY = REPO_ROOT / "main" / "main_ai_chat_experiment.c"


class OfficialChatExperimentSourceTests(unittest.TestCase):
    def test_experiment_entry_exists_and_uses_minimal_local_bootstrap_chain(self) -> None:
        source = MAIN_EXPERIMENT_ENTRY.read_text(encoding="utf-8")

        self.assertIn("nvs_flash_init", source)
        self.assertIn("wifi_provision_init", source)
        self.assertIn("wifi_provision_start_auto", source)
        self.assertIn("wait_for_network_services_ready", source)
        self.assertIn("audio_codec_init", source)
        self.assertIn("official_chat_create", source)
        self.assertIn("official_chat_start", source)
        self.assertIn("wifi_provision_is_connected", source)

    def test_experiment_entry_waits_for_official_service_dns_before_start(self) -> None:
        source = MAIN_EXPERIMENT_ENTRY.read_text(encoding="utf-8")

        self.assertIn("#include <lwip/netdb.h>", source)
        self.assertIn("getaddrinfo(", source)
        self.assertIn("api.tenclass.net", source)
        self.assertIn("mqtt.xiaozhi.me", source)
        self.assertLess(
            source.find("wait_for_network_services_ready()"),
            source.find("audio_codec_init()"),
        )

    def test_main_kconfig_exposes_official_chat_and_experiment_switches(self) -> None:
        source = MAIN_KCONFIG.read_text(encoding="utf-8")

        self.assertIn('menu "Official Chat"', source)
        self.assertIn("config USE_AUDIO_PROCESSOR", source)
        self.assertIn("config USE_DEVICE_AEC", source)
        self.assertIn("config SEND_WAKE_WORD_DATA", source)
        self.assertIn("config OFFICIAL_CHAT_OTA_URL", source)
        self.assertIn("config OFFICIAL_CHAT_LANGUAGE_CODE", source)
        self.assertIn("config APP_AI_CHAT_EXPERIMENT", source)

    def test_main_cmake_switches_between_formal_and_experiment_entries(self) -> None:
        source = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn("main_ai_chat_experiment.c", source)
        self.assertIn("111.c", source)
        self.assertIn("CONFIG_APP_AI_CHAT_EXPERIMENT", source)

    def test_formal_entry_stays_in_repository(self) -> None:
        self.assertTrue(MAIN_FORMAL_ENTRY.exists())


if __name__ == "__main__":
    unittest.main()
