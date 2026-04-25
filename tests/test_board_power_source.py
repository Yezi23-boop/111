import unittest

from tests.main_paths import BOARD_POWER_HEADER
from tests.main_paths import BOARD_POWER_SOURCE


class BoardPowerSourceTests(unittest.TestCase):
    def test_header_models_cached_board_power_state_without_low_battery(self) -> None:
        self.assertTrue(BOARD_POWER_HEADER.exists(), "main/app/board_power.h should exist")
        header = BOARD_POWER_HEADER.read_text(encoding="utf-8")

        self.assertIn("board_power_state_t", header)
        self.assertIn("bool available;", header)
        self.assertIn("battery_data_valid", header)
        self.assertIn("snapshot_stale", header)
        self.assertIn("bool charging;", header)
        self.assertIn("bool discharging;", header)
        self.assertIn("bool external_power_present;", header)
        self.assertIn("bool battery_present;", header)
        self.assertIn("uint16_t battery_mv;", header)
        self.assertIn("uint16_t system_mv;", header)
        self.assertIn("uint8_t battery_percent;", header)
        self.assertIn("esp_err_t board_power_init(void);", header)
        self.assertIn("esp_err_t board_power_refresh(board_power_state_t *state);", header)
        self.assertIn("const board_power_state_t *board_power_get_cached_state(void);", header)
        self.assertNotIn("low_battery", header)

    def test_source_maps_axp2101_snapshot_into_board_state_cache(self) -> None:
        self.assertTrue(BOARD_POWER_SOURCE.exists(), "main/app/board_power.c should exist")
        source = BOARD_POWER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "axp2101.h"', source)
        self.assertIn("axp2101_read_snapshot", source)
        self.assertIn("battery_data_valid", source)
        self.assertIn("snapshot_stale", source)
        self.assertIn("board_power_get_cached_state", source)
        self.assertNotIn("low_battery", source)


if __name__ == "__main__":
    unittest.main()
