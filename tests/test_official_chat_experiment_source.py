import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_CMAKE = REPO_ROOT / "main" / "CMakeLists.txt"
MAIN_KCONFIG = REPO_ROOT / "main" / "Kconfig.projbuild"
MAIN_FORMAL_ENTRY = REPO_ROOT / "main" / "111.c"
MAIN_EXPERIMENT_ENTRY = REPO_ROOT / "main" / "main_ai_chat_experiment.c"


class OfficialChatExperimentSourceTests(unittest.TestCase):
    def test_experiment_entry_exists_and_uses_shared_service_bootstrap_chain(self) -> None:
        source = MAIN_EXPERIMENT_ENTRY.read_text(encoding="utf-8")

        self.assertIn("nvs_flash_init", source)
        self.assertIn("wifi_provision_init", source)
        self.assertIn("network_service_start()", source)
        self.assertIn("audio_codec_init", source)
        self.assertIn("official_chat_service_init()", source)
        self.assertIn("ai_experiment_ui_start()", source)

    def test_experiment_entry_uses_standalone_ai_ui_instead_of_wait_loop(self) -> None:
        source = MAIN_EXPERIMENT_ENTRY.read_text(encoding="utf-8")

        self.assertIn('#include "network_service.h"', source)
        self.assertIn('#include "ai_experiment_ui.h"', source)
        self.assertNotIn("network_service_is_service_ready()", source)
        self.assertNotIn("official_chat_service_enter_foreground()", source)

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
