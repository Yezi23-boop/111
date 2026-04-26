import unittest

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
        self.assertIn("void ui_refresh_policy_set_force_active(bool enabled);", header)
        self.assertIn(
            "void ui_refresh_policy_set_user_brightness_percent(uint8_t percent);",
            header,
        )
        self.assertIn(
            "uint32_t ui_refresh_policy_adjust_delay(uint32_t next_call_ms);",
            header,
        )

    def test_policy_source_uses_5s_timeout_16ms_active_100ms_idle_and_40_percent_dim(self) -> None:
        self.assertTrue(
            UI_REFRESH_POLICY_SOURCE.exists(),
            "main/ui/ui_refresh_policy.c should exist",
        )
        source = UI_REFRESH_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn("5000", source)
        self.assertIn("16", source)
        self.assertIn("100", source)
        self.assertIn("40", source)
        self.assertIn("ui_refresh_policy_notify_touch", source)
        self.assertIn("ui_refresh_policy_set_force_active", source)
        self.assertIn("co5300_panel_set_brightness", source)

    def test_policy_source_adds_ble_provisioning_refresh_throttle(self) -> None:
        source = UI_REFRESH_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "network_manager.h"', source)
        self.assertIn("network_manager_get_state_cached()", source)
        self.assertIn("NETWORK_MANAGER_STATE_PROVISIONING_BLE", source)
        self.assertIn("UI_REFRESH_POLICY_THROTTLE_MODE_PROVISIONING", source)
        self.assertIn("k_provisioning_active_delay_ms = 80U", source)
        self.assertIn("k_provisioning_idle_delay_ms = 250U", source)
        self.assertIn("ui_refresh_policy_compute_state", source)
        self.assertIn("ui_refresh_policy_compute_throttle_mode", source)
        self.assertNotIn("UI_REFRESH_POLICY_STATE_PROVISIONING_THROTTLED", source)
        self.assertNotIn("s_ble_provisioning_throttle_active", source)

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
