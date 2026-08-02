import unittest

from tests.main_paths import REPO_ROOT
from tests.main_paths import SAFETY_MONITOR_POLICY_HEADER
from tests.main_paths import SAFETY_MONITOR_POLICY_SOURCE


class MusicSafetyPolicySourceTests(unittest.TestCase):
    def test_music_has_a_distinct_audio_owner(self) -> None:
        header = (
            REPO_ROOT / "components" / "audio_codec" / "include" /
            "audio_codec.h"
        ).read_text(encoding="utf-8")
        source = (
            REPO_ROOT / "components" / "audio_codec" / "audio_codec.c"
        ).read_text(encoding="utf-8")

        self.assertIn("AUDIO_CODEC_OWNER_MUSIC_PLAYER", header)
        self.assertIn('return "music_player";', source)

    def test_music_playback_is_a_policy_owned_block(self) -> None:
        header = SAFETY_MONITOR_POLICY_HEADER.read_text(encoding="utf-8")
        source = SAFETY_MONITOR_POLICY_SOURCE.read_text(encoding="utf-8")
        controller = (
            REPO_ROOT / "main" / "ui" / "custom" /
            "danger_detection_controller.c"
        ).read_text(encoding="utf-8")

        self.assertIn("SAFETY_MONITOR_POLICY_BLOCK_MUSIC_PLAYBACK", header)
        self.assertIn("music_playback_active", header)
        self.assertIn("safety_monitor_policy_set_music_active", header)
        self.assertIn("SAFETY_MONITOR_POLICY_NOTIFY_MUSIC", source)
        self.assertIn("safety_monitor_policy_set_music_active", source)
        self.assertIn("music_playback_active", source)
        self.assertIn("SAFETY_MONITOR_POLICY_BLOCK_MUSIC_PLAYBACK", controller)


if __name__ == "__main__":
    unittest.main()
