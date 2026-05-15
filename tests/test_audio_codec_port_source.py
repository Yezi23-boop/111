import unittest

from tests.main_paths import AUDIO_APP_SOURCE
from tests.main_paths import APP_ALERT_MANAGER_SOURCE
from tests.main_paths import AUDIO_ALERT_PLAYER_SOURCE
from tests.main_paths import REPO_ROOT

AUDIO_CODEC_HEADER = REPO_ROOT / "components" / "audio_codec" / "include" / "audio_codec.h"
AUDIO_PLATFORM_HEADER = REPO_ROOT / "components" / "audio_codec" / "include" / "audio_platform_config.h"
AUDIO_CODEC_SOURCE = REPO_ROOT / "components" / "audio_codec" / "audio_codec.c"
AUDIO_CODEC_BUS_SOURCE = REPO_ROOT / "components" / "audio_codec" / "audio_codec_bus.c"
AUDIO_CODEC_CMAKE = REPO_ROOT / "components" / "audio_codec" / "CMakeLists.txt"
ESPDL_AUDIO_RUNTIME_SOURCE = REPO_ROOT / "components" / "espdl_inference" / "espdl_audio_runtime.cpp"
TRAFFIC_REALTIME_SOURCE = REPO_ROOT / "components" / "traffic_inference" / "traffic_inference_realtime.cc"
MP3_PLAYER_SOURCE = REPO_ROOT / "components" / "mp3_player" / "mp3_player.c"
I2C_MANAGER_HEADER = REPO_ROOT / "components" / "i2c_manager" / "include" / "i2c_manager.h"
OFFICIAL_CHAT_CODEC_ADAPTER_SOURCE = (
    REPO_ROOT / "components" / "official_chat" / "audio" / "local_audio_codec_adapter.cc"
)
OFFICIAL_CHAT_CODEC_ADAPTER_HEADER = (
    REPO_ROOT / "components" / "official_chat" / "audio" / "local_audio_codec_adapter.h"
)


class AudioCodecPortSourceTests(unittest.TestCase):
    def test_audio_codec_component_matches_official_chat_contract(self) -> None:
        header = AUDIO_CODEC_HEADER.read_text(encoding="utf-8")
        self.assertIn("audio_codec_read", header)
        self.assertIn("audio_codec_write", header)
        self.assertIn("audio_codec_flush_output", header)
        self.assertNotIn("audio_codec_get_playback_dev", header)
        self.assertNotIn("audio_codec_get_record_dev", header)

    def test_audio_codec_private_bus_layer_exists(self) -> None:
        self.assertTrue(AUDIO_PLATFORM_HEADER.exists())
        self.assertTrue(AUDIO_CODEC_BUS_SOURCE.exists())
        cmake = AUDIO_CODEC_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"audio_codec_bus.c"', cmake)

    def test_audio_app_uses_audio_codec_wrapper_and_24khz_header(self) -> None:
        source = AUDIO_APP_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "audio_platform_config.h"', source)
        self.assertIn("audio_codec_read(", source)
        self.assertIn('#include "services/background_service_manager.h"', source)
        self.assertIn(
            "background_service_manager_set_foreground_audio_active(\n"
            "        true, \"recording\")",
            source,
        )
        self.assertIn(
            "audio_codec_acquire_input(AUDIO_CODEC_OWNER_AUDIO_RECORDER, 500U)",
            source,
        )
        self.assertIn(
            "audio_codec_release_input(AUDIO_CODEC_OWNER_AUDIO_RECORDER)",
            source,
        )
        self.assertIn(
            "background_service_manager_set_foreground_audio_active(\n"
            "        false",
            source,
        )
        self.assertNotIn("audio_codec_get_record_dev()", source)
        self.assertNotIn("esp_codec_dev_read(", source)
        self.assertIn("AUDIO_PLATFORM_HW_SAMPLE_RATE", source)
        self.assertIn("AUDIO_PLATFORM_HW_INPUT_CHANNELS", source)

    def test_mp3_player_uses_audio_codec_write_wrapper(self) -> None:
        source = MP3_PLAYER_SOURCE.read_text(encoding="utf-8")
        self.assertIn("audio_codec_write(", source)
        self.assertNotIn("audio_codec_get_playback_dev()", source)
        self.assertNotIn("esp_codec_dev_write(", source)

    def test_audio_codec_uses_new_i2c_manager_bus_handle_contract(self) -> None:
        i2c_header = I2C_MANAGER_HEADER.read_text(encoding="utf-8")
        audio_codec = AUDIO_CODEC_SOURCE.read_text(encoding="utf-8")
        self.assertIn("i2c_manager_get_bus_handle", i2c_header)
        self.assertIn("i2c_manager_get_bus_handle()", audio_codec)

    def test_audio_codec_exposes_owner_session_contract(self) -> None:
        header = AUDIO_CODEC_HEADER.read_text(encoding="utf-8")
        source = AUDIO_CODEC_SOURCE.read_text(encoding="utf-8")

        self.assertIn("audio_codec_owner_t", header)
        self.assertIn("audio_codec_acquire_input", header)
        self.assertIn("audio_codec_release_input", header)
        self.assertIn("audio_codec_acquire_output", header)
        self.assertIn("audio_codec_release_output", header)
        self.assertIn("audio_codec_session_snapshot_t", header)
        self.assertIn("audio_codec_get_session_snapshot", header)
        self.assertIn("audio_codec_owner_to_text", header)
        self.assertIn("AUDIO_CODEC_OWNER_ALERT_PLAYER", header)
        self.assertIn("AUDIO_CODEC_OWNER_AUDIO_RECORDER", header)
        self.assertIn("s_lifecycle_ref_count", source)
        self.assertIn("s_input_session_owner", source)
        self.assertIn('return "audio_recorder";', source)
        self.assertIn('return "alert_player";', source)
        self.assertIn("audio_codec_get_session_snapshot", source)
        self.assertIn("xSemaphoreCreateMutex", source)

    def test_realtime_audio_clients_use_input_session_contract(self) -> None:
        espdl_runtime = ESPDL_AUDIO_RUNTIME_SOURCE.read_text(encoding="utf-8")
        traffic_runtime = TRAFFIC_REALTIME_SOURCE.read_text(encoding="utf-8")
        official_chat_adapter = OFFICIAL_CHAT_CODEC_ADAPTER_SOURCE.read_text(
            encoding="utf-8"
        )
        official_chat_adapter_header = OFFICIAL_CHAT_CODEC_ADAPTER_HEADER.read_text(
            encoding="utf-8"
        )

        self.assertIn(
            "audio_codec_acquire_input(AUDIO_CODEC_OWNER_ESPDL_INFERENCE", espdl_runtime
        )
        self.assertIn(
            "audio_codec_release_input(AUDIO_CODEC_OWNER_ESPDL_INFERENCE", espdl_runtime
        )
        self.assertIn(
            "audio_codec_acquire_input(AUDIO_CODEC_OWNER_TRAFFIC_INFERENCE", traffic_runtime
        )
        self.assertIn(
            "audio_codec_release_input(AUDIO_CODEC_OWNER_TRAFFIC_INFERENCE", traffic_runtime
        )
        self.assertIn(
            "audio_codec_acquire_input(\n"
            "        AUDIO_CODEC_OWNER_OFFICIAL_CHAT, kInputSessionTimeoutMs)",
            official_chat_adapter,
        )
        self.assertIn(
            "audio_codec_release_input(AUDIO_CODEC_OWNER_OFFICIAL_CHAT)",
            official_chat_adapter,
        )
        self.assertIn(
            "audio_codec_acquire_output(\n"
            "        AUDIO_CODEC_OWNER_OFFICIAL_CHAT, kOutputSessionTimeoutMs)",
            official_chat_adapter,
        )
        self.assertIn(
            "audio_codec_release_output(AUDIO_CODEC_OWNER_OFFICIAL_CHAT)",
            official_chat_adapter,
        )
        self.assertIn("input_session_acquired_", official_chat_adapter)
        self.assertNotIn("input_enabled_", official_chat_adapter_header)
        self.assertIn("output_session_acquired_", official_chat_adapter_header)
        self.assertNotIn("output_enabled_", official_chat_adapter_header)
        self.assertIn(
            "if (!input_session_acquired_)",
            official_chat_adapter,
        )
        self.assertIn(
            "data.empty() || !output_session_acquired_",
            official_chat_adapter,
        )
        self.assertIn("return input_session_acquired_;", official_chat_adapter)
        self.assertNotIn("!input_enabled_", official_chat_adapter)
        self.assertIn("return output_session_acquired_;", official_chat_adapter)
        self.assertNotIn("!output_enabled_", official_chat_adapter)
        self.assertIn(
            "if (!output_session_acquired_) {\n"
            "    return;\n"
            "  }\n\n"
            "  (void)audio_codec_set_pa_enable(false);",
            official_chat_adapter,
        )
        self.assertIn("EnableOutput(false);", official_chat_adapter)

    def test_p0_alert_and_mp3_player_use_output_session_contract(self) -> None:
        audio_alert_player = AUDIO_ALERT_PLAYER_SOURCE.read_text(encoding="utf-8")
        app_alert_manager = APP_ALERT_MANAGER_SOURCE.read_text(encoding="utf-8")
        mp3_player = MP3_PLAYER_SOURCE.read_text(encoding="utf-8")

        self.assertIn(
            "audio_codec_acquire_output(AUDIO_CODEC_OWNER_ALERT_PLAYER, 500U)",
            audio_alert_player,
        )
        self.assertIn(
            "audio_codec_release_output(AUDIO_CODEC_OWNER_ALERT_PLAYER)",
            audio_alert_player,
        )
        self.assertIn(
            "audio_codec_acquire_output(AUDIO_CODEC_OWNER_AUDIO_PLAYER, 0U)",
            mp3_player,
        )
        self.assertIn(
            "audio_codec_release_output(AUDIO_CODEC_OWNER_AUDIO_PLAYER)",
            mp3_player,
        )
        self.assertIn("app_alert_manager_preempt_normal_audio_output", app_alert_manager)
        self.assertIn("audio_codec_get_session_snapshot", app_alert_manager)
        self.assertIn("snapshot.output_owner != AUDIO_CODEC_OWNER_AUDIO_PLAYER", app_alert_manager)
        self.assertIn("mp3_player_stop()", app_alert_manager)
        self.assertIn("resource_preempt: resource=audio_output", app_alert_manager)


if __name__ == "__main__":
    unittest.main()
