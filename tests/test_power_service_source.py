import unittest

from tests.main_paths import POWER_SERVICE_HEADER
from tests.main_paths import POWER_SERVICE_SOURCE


class PowerServiceSourceTests(unittest.TestCase):
    def test_header_exposes_public_polling_api(self) -> None:
        self.assertTrue(POWER_SERVICE_HEADER.exists(), "main/services/power/power_service.h should exist")
        header = POWER_SERVICE_HEADER.read_text(encoding="utf-8")

        self.assertIn("power_state_changed_cb_t", header)
        self.assertIn("esp_err_t power_service_init(void);", header)
        self.assertIn("esp_err_t power_service_start(void);", header)
        self.assertIn("void power_service_register_callback(power_state_changed_cb_t cb);", header)
        self.assertIn("esp_err_t power_service_get_snapshot(board_power_state_t *out_state);", header)
        self.assertIn("const board_power_state_t *power_service_get_state(void);", header)

    def test_source_uses_1s_2s_5s_polling_backoff_and_board_power_contract(self) -> None:
        self.assertTrue(POWER_SERVICE_SOURCE.exists(), "main/services/power/power_service.c should exist")
        source = POWER_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("board_power_refresh", source)
        self.assertIn("board_power_get_cached_state", source)
        self.assertRegex(source, r"delay_ticks\s*=\s*pdMS_TO_TICKS\(1000\);")
        self.assertRegex(
            source,
            r"s_failure_count\s*>=\s*3\s*\?\s*pdMS_TO_TICKS\(5000\)\s*:\s*pdMS_TO_TICKS\(2000\)",
        )

    def test_source_logs_power_state_changes_without_logging_every_poll(self) -> None:
        self.assertTrue(POWER_SERVICE_SOURCE.exists(), "main/services/power/power_service.c should exist")
        source = POWER_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("power_service_log_state_change", source)
        self.assertIn('ESP_LOGI(TAG, "power state changed:', source)
        self.assertRegex(
            source,
            r"if\s*\(\s*state_changed\s*\)\s*\{\s*power_service_log_state_change",
        )
        self.assertNotIn('ESP_LOGI(TAG, "power poll:', source)

    def test_source_filters_small_voltage_jitter_before_logging_state_changes(self) -> None:
        self.assertTrue(POWER_SERVICE_SOURCE.exists(), "main/services/power/power_service.c should exist")
        source = POWER_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("k_voltage_jitter_threshold_mv = 20", source)
        self.assertIn("power_service_mv_changed", source)
        self.assertRegex(
            source,
            r"power_service_mv_changed\(lhs->battery_mv,\s*rhs->battery_mv\)",
        )
        self.assertRegex(
            source,
            r"power_service_mv_changed\(lhs->system_mv,\s*rhs->system_mv\)",
        )
        self.assertNotIn("lhs->battery_mv == rhs->battery_mv", source)
        self.assertNotIn("lhs->system_mv == rhs->system_mv", source)

    def test_source_exposes_out_copy_snapshot_without_i2c_or_state_advance(self) -> None:
        self.assertTrue(POWER_SERVICE_SOURCE.exists(), "main/services/power/power_service.c should exist")
        source = POWER_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("esp_err_t power_service_get_snapshot(board_power_state_t *out_state)", source)
        snapshot_body = source.split(
            "esp_err_t power_service_get_snapshot(board_power_state_t *out_state)", 1
        )[1].split("const board_power_state_t *power_service_get_state(void)", 1)[0]
        self.assertIn("*out_state = s_state_buffers[s_active_state_index];", snapshot_body)
        self.assertIn("taskENTER_CRITICAL(&s_lock);", snapshot_body)
        self.assertNotIn("board_power_refresh", snapshot_body)
        self.assertNotIn("board_power_get_cached_state", snapshot_body)

    def test_source_notifies_power_policy_when_effective_state_changes(self) -> None:
        self.assertTrue(POWER_SERVICE_SOURCE.exists(), "main/services/power/power_service.c should exist")
        source = POWER_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/power/power_policy.h"', source)
        self.assertIn("POWER_POLICY_NOTIFY_POWER_STATE", source)
        self.assertIn("(void)power_policy_notify(POWER_POLICY_NOTIFY_POWER_STATE);", source)


if __name__ == "__main__":
    unittest.main()
