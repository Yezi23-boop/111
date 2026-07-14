import unittest

from tests.main_paths import (
    APP_ALERT_MANAGER_SOURCE,
    DANGER_DETECTION_SERVICE_SOURCE,
    HAPTIC_ALERT_PLAYER_HEADER,
    HAPTIC_ALERT_PLAYER_SOURCE,
    MAIN_CMAKE,
)


class HapticAlertPlayerSourceTests(unittest.TestCase):
    def test_haptic_player_exposes_minimal_async_api(self) -> None:
        header = HAPTIC_ALERT_PLAYER_HEADER.read_text(encoding="utf-8")

        self.assertIn("haptic_alert_player_init", header)
        self.assertIn("haptic_alert_player_play_initial_danger_once", header)
        self.assertIn("首次危险强震", header)

    def test_haptic_player_uses_short_lived_task_and_dedupes(self) -> None:
        source = HAPTIC_ALERT_PLAYER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("HAPTIC_ALERT_TASK_STACK_SIZE 3072U", source)
        self.assertIn("HAPTIC_ALERT_TASK_PRIORITY 4U", source)
        self.assertIn("static void haptic_alert_player_task(void *arg)", source)
        self.assertIn('#include "esp_heap_caps.h"', source)
        self.assertIn('#include "freertos/idf_additions.h"', source)
        self.assertIn("xTaskCreateWithCaps(\n        haptic_alert_player_task", source)
        self.assertIn("MALLOC_CAP_SPIRAM", source)
        self.assertIn('\"haptic_alert\"', source)
        self.assertIn("vTaskDelete(NULL);", source)
        self.assertIn("bool playing", source)
        self.assertIn("already_playing", source)
        self.assertIn("taskENTER_CRITICAL(&s_haptic_state.lock)", source)
        self.assertIn("taskEXIT_CRITICAL(&s_haptic_state.lock)", source)

    def test_haptic_player_controls_motor_and_forces_off(self) -> None:
        source = HAPTIC_ALERT_PLAYER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "app/board_ds2413_motor.h"', source)
        self.assertIn("board_ds2413_motor_set_enabled(true)", source)
        self.assertIn("board_ds2413_motor_set_enabled(false)", source)
        self.assertIn("haptic motor fallback off failed", source)
        self.assertIn("kInitialDangerOnMs = 220U", source)
        self.assertIn("kInitialDangerGapMs = 90U", source)
        self.assertLess(
            source.index("haptic_alert_player_run_on_segment(kInitialDangerOnMs);"),
            source.index("vTaskDelay(pdMS_TO_TICKS(kInitialDangerGapMs));"),
        )

    def test_haptic_player_respects_power_budget_without_policy_ownership(self) -> None:
        source = HAPTIC_ALERT_PLAYER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/power/power_policy.h"', source)
        self.assertIn("power_policy_get_budget()", source)
        self.assertIn("budget.haptic_alert_allowed", source)
        self.assertIn("haptic skipped by power budget", source)
        self.assertNotIn("power_policy_notify(", source)
        self.assertNotIn("power_policy_set_", source)

    def test_app_alert_manager_initializes_and_triggers_haptic_once(self) -> None:
        source = APP_ALERT_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "haptic_alert_player.h"', source)
        self.assertIn("haptic_alert_player_init();", source)
        self.assertIn("haptic_alert_player_play_initial_danger_once();", source)
        self.assertLess(
            source.index("if (same_source_active)"),
            source.index("haptic_alert_player_play_initial_danger_once();"),
        )
        self.assertLess(
            source.index("if (request->severity == APP_ALERT_SEVERITY_DANGER)"),
            source.index("haptic_alert_player_play_initial_danger_once();"),
        )
        self.assertLess(
            source.index("haptic_alert_player_play_initial_danger_once();"),
            source.index("audio_alert_player_play_warning_once();"),
        )

    def test_build_wires_haptic_player_and_danger_service_stays_decoupled(self) -> None:
        cmake = MAIN_CMAKE.read_text(encoding="utf-8")
        service = DANGER_DETECTION_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn("features/alerts/haptic_alert_player.c", cmake)
        self.assertNotIn("haptic_alert_player", service)
        self.assertNotIn("board_ds2413_motor", service)


if __name__ == "__main__":
    unittest.main()
