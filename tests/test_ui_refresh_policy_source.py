import unittest
import re

from tests.main_paths import LVGL_TASK_SOURCE
from tests.main_paths import REPO_ROOT
from tests.main_paths import UI_REFRESH_POLICY_HEADER
from tests.main_paths import UI_REFRESH_POLICY_SOURCE


class UiRefreshPolicySourceTests(unittest.TestCase):
    def test_policy_header_exposes_force_refresh_and_touch_api(self) -> None:
        self.assertTrue(
            UI_REFRESH_POLICY_HEADER.exists(),
            "main/ui/ui_refresh_policy.h should exist",
        )
        header = UI_REFRESH_POLICY_HEADER.read_text(encoding="utf-8")

        self.assertIn("void ui_refresh_policy_init(void);", header)
        self.assertIn("void ui_refresh_policy_notify_touch(void);", header)
        self.assertIn("void ui_refresh_policy_notify_activity(void);", header)
        self.assertIn("void ui_refresh_policy_set_force_active(bool enabled);", header)
        self.assertIn(
            "void ui_refresh_policy_set_user_brightness_percent(uint8_t percent);",
            header,
        )
        self.assertIn(
            "uint32_t ui_refresh_policy_adjust_delay(uint32_t next_call_ms);",
            header,
        )
        self.assertIn("ui_refresh_policy_activity_snapshot_t", header)
        self.assertIn(
            "bool ui_refresh_policy_get_activity_snapshot(",
            header,
        )

    def test_policy_source_uses_30s_standby_timeout_gradual_dim_and_500ms_standby_delay(self) -> None:
        self.assertTrue(
            UI_REFRESH_POLICY_SOURCE.exists(),
            "main/ui/ui_refresh_policy.c should exist",
        )
        source = UI_REFRESH_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn("k_standby_timeout_us = 30000LL * 1000LL", source)
        self.assertIn("k_standby_fade_us = 5000LL * 1000LL", source)
        self.assertIn("k_active_delay_ms = 16U", source)
        self.assertIn("k_standby_delay_ms = 500U", source)
        self.assertIn("ui_refresh_policy_compute_standby_brightness", source)
        self.assertIn("return 0U;", source)
        self.assertIn("ui_refresh_policy_notify_touch", source)
        self.assertIn("ui_refresh_policy_notify_activity", source)
        self.assertIn("ui_refresh_policy_set_force_active", source)
        self.assertIn("co5300_panel_set_brightness", source)

    def test_policy_source_adds_ble_provisioning_refresh_throttle(self) -> None:
        source = UI_REFRESH_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "network_manager.h"', source)
        self.assertIn("network_manager_get_state_cached()", source)
        self.assertIn("NETWORK_MANAGER_STATE_PROVISIONING_BLE", source)
        self.assertIn("UI_REFRESH_POLICY_THROTTLE_MODE_PROVISIONING", source)
        self.assertIn("k_provisioning_active_delay_ms = 80U", source)
        self.assertIn("k_provisioning_standby_delay_ms = 500U", source)
        self.assertIn("ui_refresh_policy_compute_state", source)
        self.assertIn("ui_refresh_policy_compute_throttle_mode", source)
        self.assertNotIn("UI_REFRESH_POLICY_STATE_PROVISIONING_THROTTLED", source)
        self.assertNotIn("s_ble_provisioning_throttle_active", source)

    def test_policy_snapshot_is_read_only_and_does_not_drive_refresh(self) -> None:
        header = UI_REFRESH_POLICY_HEADER.read_text(encoding="utf-8")
        source = UI_REFRESH_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn("UI_REFRESH_POLICY_ACTIVITY_ACTIVE", header)
        self.assertIn("UI_REFRESH_POLICY_ACTIVITY_STANDBY", header)
        self.assertIn("UI_REFRESH_POLICY_ACTIVITY_FORCE_ACTIVE", header)
        self.assertIn("UI_REFRESH_POLICY_THROTTLE_NORMAL", header)
        self.assertIn("UI_REFRESH_POLICY_THROTTLE_PROVISIONING", header)
        self.assertIn("target_brightness_percent", header)
        self.assertIn("idle_time_ms", header)
        self.assertIn("bool standby;", header)
        self.assertNotIn("UI_REFRESH_POLICY_ACTIVITY_IDLE_DIM", header)

        match = re.search(
            r"bool ui_refresh_policy_get_activity_snapshot\([^)]*\)\s*\{(?P<body>.*?)\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn("esp_timer_get_time()", body)
        self.assertIn("ui_refresh_policy_get_effective_brightness_percent()", body)
        self.assertNotIn("ui_refresh_policy_poll()", body)
        self.assertNotIn("ui_refresh_policy_set_state", body)
        self.assertNotIn("ui_refresh_policy_set_throttle_mode", body)
        self.assertNotIn("ui_refresh_policy_apply_brightness_if_needed", body)
        self.assertNotIn("co5300_panel_set_brightness", body)
        self.assertNotIn('#include "services/power/power_policy.h"', source)
        self.assertNotIn("power_policy_get_budget", source)
        self.assertNotIn("power_policy_", body)

    def test_lvgl_task_uses_policy_to_bound_lv_timer_handler_delay(self) -> None:
        source = LVGL_TASK_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "ui_refresh_policy.h"', source)
        self.assertIn("ui_refresh_policy_init();", source)
        self.assertIn("ui_refresh_policy_poll();", source)
        self.assertIn("delay_ms = ui_refresh_policy_adjust_delay", source)
        self.assertNotIn("else if (next_call > 500)", source)

    def test_touch_input_notifies_policy_and_slider_updates_user_brightness(self) -> None:
        input_source = (
            REPO_ROOT / "components" / "lvgl_port" / "lv_port_input.c"
        ).read_text(encoding="utf-8")
        events_source = (
            REPO_ROOT / "main" / "ui" / "generated" / "events_init.c"
        ).read_text(encoding="utf-8")
        panel_header = (
            REPO_ROOT
            / "components"
            / "co5300_panel"
            / "include"
            / "co5300_panel.h"
        ).read_text(encoding="utf-8")
        panel_source = (
            REPO_ROOT / "components" / "co5300_panel" / "co5300_panel.c"
        ).read_text(encoding="utf-8")

        self.assertIn("ui_refresh_policy_notify_touch();", input_source)
        self.assertIn("ui_refresh_policy_set_user_brightness_percent", events_source)
        self.assertIn("co5300_panel_set_brightness", panel_header)
        self.assertIn("co5300_panel_get_brightness", panel_header)
        self.assertIn("esp_lcd_panel_co5300_set_brightness", panel_source)
        self.assertIn("0x51", panel_source)


if __name__ == "__main__":
    unittest.main()
