import unittest

from tests.main_paths import WEATHER_HTTP_CLIENT_HEADER
from tests.main_paths import WEATHER_HTTP_CLIENT_SOURCE
from tests.main_paths import WEATHER_SERVICE_SOURCE


class TimeWeatherSourceTests(unittest.TestCase):
    def test_time_weather_no_longer_references_mp3_player(self) -> None:
        source = WEATHER_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertNotIn('#include "mp3_player.h"', source)
        self.assertNotIn("mp3_player_init(", source)
        self.assertNotIn("mp3_player_play_file(", source)

    def test_weather_refresh_policy_matches_runtime_strategy(self) -> None:
        source = WEATHER_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("kWeatherRefreshNormalSeconds = 60 * 60", source)
        self.assertIn("kWeatherRefreshLowPowerSeconds = 2 * 60 * 60", source)
        self.assertIn("kWeatherRetryAfterFailureSeconds = 5 * 60", source)
        self.assertIn('"services/power/power_policy.h"', source)
        self.assertIn("power_policy_get_budget()", source)
        self.assertIn("POWER_POLICY_STATE_STANDBY", source)
        self.assertIn("POWER_POLICY_DISPLAY_OFF", source)
        self.assertIn("get_weather_retry_elapsed_seconds(refresh_interval_seconds)", source)
        self.assertIn("seconds_since_last_fetch = kWeatherRefreshNormalSeconds", source)

    def test_weather_http_fetch_reports_failure_to_scheduler(self) -> None:
        header = WEATHER_HTTP_CLIENT_HEADER.read_text(encoding="utf-8")
        source = WEATHER_HTTP_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("weather_http_client_fetch", header)
        self.assertIn("esp_err_t weather_http_client_fetch", source)
        self.assertIn("s_last_weather_parse_ok", source)
        self.assertIn(".user_data = out", source)
        self.assertNotIn("s_active_result", source)
        self.assertIn("status_code != 200", source)
        self.assertIn("Weather response parse failed", source)

    def test_service_owns_snapshot_and_http_client_returns_dto(self) -> None:
        service = WEATHER_SERVICE_SOURCE.read_text(encoding="utf-8")
        client = WEATHER_HTTP_CLIENT_SOURCE.read_text(encoding="utf-8")

        self.assertIn("static weather_info_t s_weather_info", service)
        self.assertIn("weather_http_client_fetch(&result)", service)
        self.assertNotIn("esp_http_client", service)
        self.assertNotIn("weather_service_get_info", client)
        self.assertNotIn("weather_service_store_result", client)


if __name__ == "__main__":
    unittest.main()
