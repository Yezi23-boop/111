import unittest

from tests.main_paths import HPTTS_HEADER, HPTTS_SOURCE, TIME_WEATHER_SOURCE


class TimeWeatherSourceTests(unittest.TestCase):
    def test_time_weather_no_longer_references_mp3_player(self) -> None:
        source = TIME_WEATHER_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn('#include "mp3_player.h"', source)
        self.assertNotIn("mp3_player_init(", source)
        self.assertNotIn("mp3_player_play_file(", source)

    def test_weather_refresh_policy_matches_runtime_strategy(self) -> None:
        source = TIME_WEATHER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("kWeatherRefreshNormalSeconds = 60 * 60", source)
        self.assertIn("kWeatherRefreshLowPowerSeconds = 2 * 60 * 60", source)
        self.assertIn("kWeatherRetryAfterFailureSeconds = 5 * 60", source)
        self.assertIn('"services/power_policy.h"', source)
        self.assertIn("power_policy_get_budget()", source)
        self.assertIn("POWER_POLICY_STATE_STANDBY", source)
        self.assertIn("POWER_POLICY_DISPLAY_OFF", source)
        self.assertIn("get_weather_retry_elapsed_seconds(refresh_interval_seconds)", source)
        self.assertIn("seconds_since_last_fetch = kWeatherRefreshNormalSeconds", source)

    def test_weather_http_fetch_reports_failure_to_scheduler(self) -> None:
        header = HPTTS_HEADER.read_text(encoding="utf-8")
        source = HPTTS_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_err_t http_rest_with_url(void);", header)
        self.assertIn("esp_err_t http_rest_with_url(void)", source)
        self.assertIn("s_last_weather_parse_ok", source)
        self.assertIn("status_code != 200", source)
        self.assertIn("Weather response parse failed", source)


if __name__ == "__main__":
    unittest.main()
