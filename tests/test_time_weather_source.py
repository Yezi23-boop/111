import unittest

from tests.main_paths import TIME_WEATHER_SOURCE


class TimeWeatherSourceTests(unittest.TestCase):
    def test_time_weather_no_longer_references_mp3_player(self) -> None:
        source = TIME_WEATHER_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn('#include "mp3_player.h"', source)
        self.assertNotIn("mp3_player_init(", source)
        self.assertNotIn("mp3_player_play_file(", source)


if __name__ == "__main__":
    unittest.main()
