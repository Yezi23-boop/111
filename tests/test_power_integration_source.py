import unittest
import re

from tests.main_paths import APP_MAIN_SOURCE
from tests.main_paths import APP_ALERT_MANAGER_SOURCE
from tests.main_paths import BACKGROUND_SERVICE_MANAGER_SOURCE
from tests.main_paths import DISPLAY_ALERT_ADAPTER_SOURCE
from tests.main_paths import HARDWARE_INIT_SOURCE
from tests.main_paths import LVGL_TASK_SOURCE
from tests.main_paths import NETWORK_SERVICE_SOURCE
from tests.main_paths import POWER_POLICY_HEADER
from tests.main_paths import POWER_POLICY_SOURCE
from tests.main_paths import REPO_ROOT
from tests.main_paths import SLEEP_COORDINATOR_HEADER
from tests.main_paths import SLEEP_COORDINATOR_SOURCE
from tests.main_paths import STARTUP_READINESS_HEADER
from tests.main_paths import STARTUP_READINESS_SOURCE


MAIN_CMAKE = REPO_ROOT / "main" / "CMakeLists.txt"
FORBIDDEN_SLEEP_APIS = (
    "esp_light_sleep_start",
    "esp_deep_sleep_start",
    "esp_sleep_enable_timer_wakeup",
    "esp_sleep_enable_gpio_wakeup",
    "esp_sleep_enable_ext0_wakeup",
    "esp_sleep_enable_ext1_wakeup",
    "esp_sleep_get_wakeup_cause",
)


def _iter_project_source_files():
    for base in (REPO_ROOT / "main", REPO_ROOT / "components"):
        for path in base.rglob("*"):
            if path.suffix.lower() not in {".c", ".cc", ".cpp"}:
                continue
            yield path


def _strip_c_comments_and_strings(source: str) -> str:
    result = []
    i = 0
    length = len(source)
    in_block_comment = False
    in_line_comment = False
    in_string = False
    in_char = False

    while i < length:
        ch = source[i]
        nxt = source[i + 1] if i + 1 < length else ""

        if in_block_comment:
            if ch == "*" and nxt == "/":
                in_block_comment = False
                i += 2
            else:
                i += 1
            continue

        if in_line_comment:
            if ch == "\n":
                in_line_comment = False
                result.append(ch)
            i += 1
            continue

        if in_string:
            if ch == "\\" and i + 1 < length:
                i += 2
                continue
            if ch == '"':
                in_string = False
            i += 1
            continue

        if in_char:
            if ch == "\\" and i + 1 < length:
                i += 2
                continue
            if ch == "'":
                in_char = False
            i += 1
            continue

        if ch == "/" and nxt == "*":
            in_block_comment = True
            i += 2
            continue

        if ch == "/" and nxt == "/":
            in_line_comment = True
            i += 2
            continue

        if ch == '"':
            in_string = True
            i += 1
            continue

        if ch == "'":
            in_char = True
            i += 1
            continue

        result.append(ch)
        i += 1

    return "".join(result)


def _extract_c_function_body(source: str, function_name: str) -> str:
    signature = re.search(
        rf"\b{re.escape(function_name)}\s*\(\s*void\s*\)\s*\{{", source
    )
    if signature is None:
        return ""

    brace_start = source.find("{", signature.end() - 1)
    if brace_start < 0:
        return ""

    depth = 0
    for index in range(brace_start, len(source)):
        ch = source[index]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return source[brace_start + 1 : index]
    return ""


def _extract_cmake_call_body(source: str, call_name: str) -> str:
    match = re.search(
        rf"\b{re.escape(call_name)}\s*\((.*?)\n\)",
        source,
        re.S,
    )
    if match is None:
        return ""
    return match.group(1)


def _extract_named_cmake_set_body(source: str, block_name: str) -> str:
    match = re.search(
        rf"set\s*\(\s*{re.escape(block_name)}\s*(.*?)\n\)",
        source,
        re.S,
    )
    if match is None:
        return ""
    return match.group(1)


class PowerIntegrationSourceTests(unittest.TestCase):
    def test_hardware_init_brings_in_board_power_after_audio_init(self) -> None:
        self.assertTrue(HARDWARE_INIT_SOURCE.exists(), "main/app/hardware_init.c should exist")
        source = _extract_c_function_body(
            _strip_c_comments_and_strings(
                HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")
            ),
            "hardware_init",
        )

        audio_pos = source.find("audio_codec_init()")
        board_pos = source.find("board_power_init()")
        self.assertGreaterEqual(audio_pos, 0)
        self.assertGreaterEqual(board_pos, 0)
        self.assertLess(audio_pos, board_pos)

    def test_hardware_init_logs_boot_power_snapshot_after_board_power_init(self) -> None:
        self.assertTrue(HARDWARE_INIT_SOURCE.exists(), "main/app/hardware_init.c should exist")
        source = HARDWARE_INIT_SOURCE.read_text(encoding="utf-8")
        stripped_source = _strip_c_comments_and_strings(source)
        body = _extract_c_function_body(stripped_source, "hardware_init")

        init_pos = body.find("board_power_init()")
        refresh_pos = body.find("board_power_refresh(")
        self.assertGreaterEqual(init_pos, 0)
        self.assertGreaterEqual(refresh_pos, 0)
        self.assertLess(init_pos, refresh_pos)
        self.assertIn("board_power_log_boot_snapshot", source)
        self.assertIn("Board power boot snapshot:", source)

    def test_app_main_starts_power_service_after_lvgl_task(self) -> None:
        self.assertTrue(APP_MAIN_SOURCE.exists(), "main/app/app_main.c should exist")
        raw_source = APP_MAIN_SOURCE.read_text(encoding="utf-8")
        source = _strip_c_comments_and_strings(raw_source)
        app_main_body = _extract_c_function_body(source, "app_main")

        self.assertIn("xTaskCreatePinnedToCore(lvgl_task,", source)
        self.assertIn("power_service_start()", source)
        self.assertIn("static bool start_display_and_ui(void)", source)
        self.assertIn("if (!start_display_and_ui())", app_main_body)
        self.assertIn("startup_readiness_init()", source)
        self.assertIn("UI task create failed, halting startup sequence", raw_source)
        ui_stage_pos = app_main_body.find("start_display_and_ui()")
        policy_stage_pos = app_main_body.find("start_core_policy()")
        self.assertGreaterEqual(ui_stage_pos, 0)
        self.assertGreaterEqual(policy_stage_pos, 0)
        self.assertLess(ui_stage_pos, policy_stage_pos)

    def test_app_main_does_not_auto_enable_sleep_test(self) -> None:
        raw_source = APP_MAIN_SOURCE.read_text(encoding="utf-8")
        self.assertIn("sleep_coordinator_start()", raw_source)
        self.assertNotIn("SLEEP_COORDINATOR_LIGHT_TEST_ENABLED", raw_source)
        self.assertNotIn("SLEEP_COORDINATOR_MODE_LIGHT_TEST", raw_source)
        self.assertNotIn("sleep_coordinator_set_mode(", raw_source)

    def test_main_cmake_registers_new_sources_and_axp2101_dependency(self) -> None:
        self.assertTrue(MAIN_CMAKE.exists(), "main/CMakeLists.txt should exist")
        source = MAIN_CMAKE.read_text(encoding="utf-8")

        app_srcs = _extract_named_cmake_set_body(source, "app_srcs")
        service_srcs = _extract_named_cmake_set_body(source, "service_srcs")
        register_block = _extract_cmake_call_body(source, "idf_component_register")

        self.assertIn("${CMAKE_CURRENT_LIST_DIR}/app/board_power.c", app_srcs)
        self.assertIn("${CMAKE_CURRENT_LIST_DIR}/services/power_service.c", service_srcs)
        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/sleep_coordinator.c",
            service_srcs,
        )
        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/startup_readiness.c",
            service_srcs,
        )
        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/safety_monitor_session.c",
            service_srcs,
        )
        self.assertRegex(
            register_block,
            r"\bREQUIRES\b[\s\S]*?\baxp2101\b",
        )
        self.assertRegex(
            register_block,
            r"\bREQUIRES\b[\s\S]*?\bwifi_control\b",
        )

    def test_startup_readiness_waits_on_ui_first_frame_signal(self) -> None:
        self.assertTrue(STARTUP_READINESS_SOURCE.exists())
        self.assertTrue(STARTUP_READINESS_HEADER.exists())

        header = STARTUP_READINESS_HEADER.read_text(encoding="utf-8")
        source = STARTUP_READINESS_SOURCE.read_text(encoding="utf-8")
        lvgl_source = LVGL_TASK_SOURCE.read_text(encoding="utf-8")

        self.assertIn("startup_readiness_mark_ui_first_frame_ready", header)
        self.assertIn("startup_readiness_wait_ui_first_frame", header)
        self.assertIn("xEventGroupCreateStatic", source)
        self.assertIn("STARTUP_READINESS_UI_FIRST_FRAME_BIT", source)
        self.assertIn("xEventGroupWaitBits", source)
        self.assertIn("startup_readiness_mark_ui_first_frame_ready();", lvgl_source)
        self.assertLess(
            lvgl_source.index("startup_readiness_mark_ui_first_frame_ready();"),
            lvgl_source.index('ESP_LOGI(TAG, "boot_stage: ui_first_frame_ready")'),
        )

    def test_power_policy_exposes_maintenance_window_without_runtime_ownership(self) -> None:
        header = POWER_POLICY_HEADER.read_text(encoding="utf-8")
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn("power_policy_set_maintenance_window", header)
        self.assertIn("s_maintenance_window_active", source)
        self.assertIn("POWER_POLICY_STATE_STANDBY", source)
        self.assertIn("budget.danger_detection_allowed = false", source)
        self.assertIn("POWER_POLICY_FLAG_MAINTENANCE", source)
        self.assertIn("maintenance_window_%s", source)
        self.assertIn("if (maintenance_window_active)", source)
        self.assertNotIn("danger_detection_service_", source)
        self.assertNotIn("safety_monitor_session_", source)

    def test_power_policy_consumes_ui_activity_snapshot_read_only(self) -> None:
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")
        self.assertIn('#include "ui_refresh_policy.h"', source)
        self.assertIn("ui_refresh_policy_get_activity_snapshot", source)
        self.assertIn("UI_REFRESH_POLICY_ACTIVITY_STANDBY", source)
        self.assertIn("budget->state = POWER_POLICY_STATE_STANDBY", source)
        self.assertIn("budget->network_sync_allowed = false", source)
        self.assertIn("budget->maintenance_allowed = false", source)
        self.assertIn("budget->ui_high_refresh_allowed = false", source)
        self.assertIn("k_light_allowed_idle_time_ms = 5LL * 60LL * 1000LL", source)
        self.assertIn("activity_snapshot.idle_time_ms >= k_light_allowed_idle_time_ms", source)
        self.assertIn("budget->sleep_interval_hint_ms = k_standby_sleep_interval_hint_ms", source)
        self.assertIn("ui_high_refresh=%d", source)

        helper_match = re.search(
            r"static void power_policy_apply_ui_activity_budget[\s\S]*?\n\}\n\n/\*\*",
            source,
        )
        self.assertIsNotNone(helper_match)
        helper_body = helper_match.group(0)
        self.assertNotIn("danger_detection_allowed", helper_body)

        self.assertLess(
            source.index("if (maintenance_window_active)"),
            source.index("power_policy_apply_ui_activity_budget(&budget);"),
        )
        self.assertLess(
            source.index("power_policy_apply_ui_activity_budget(&budget);"),
            source.index("if (budget.external_power_present &&"),
        )

    def test_low_battery_warn_is_flag_only_not_ui_prompt_or_shutdown(self) -> None:
        power_source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")
        background_source = BACKGROUND_SERVICE_MANAGER_SOURCE.read_text(encoding="utf-8")
        app_alert_source = APP_ALERT_MANAGER_SOURCE.read_text(encoding="utf-8")
        display_source = DISPLAY_ALERT_ADAPTER_SOURCE.read_text(encoding="utf-8")
        app_main_source = APP_MAIN_SOURCE.read_text(encoding="utf-8")

        self.assertIn("POWER_POLICY_FLAG_LOW_BATTERY_WARN", power_source)
        self.assertIn("battery_data_valid", power_source)
        self.assertIn("battery_percent", power_source)
        self.assertIn("battery_mv", power_source)
        self.assertNotIn("background_service_manager_sync_low_battery_prompt", background_source)
        self.assertNotIn("app_alert_manager_set_low_battery_warning", background_source)
        self.assertIn("app_alert_manager_set_low_battery_warning", app_alert_source)
        self.assertIn("ui_refresh_policy_notify_activity", app_alert_source)
        self.assertIn("display_alert_adapter_show_low_battery_warning", app_alert_source)
        self.assertIn("display_alert_adapter_hide_low_battery_warning", app_alert_source)
        self.assertIn("display_alert_adapter_show_low_battery_warning", display_source)
        self.assertIn("display_alert_adapter_hide_low_battery_warning", display_source)
        self.assertIn("电量较低", display_source)
        self.assertIn("app_alert_manager_init()", app_main_source)
        self.assertIn("budget.low_battery_warn", power_source)
        self.assertNotIn("budget.state = POWER_POLICY_STATE_LOW_BATTERY_WARN", power_source)
        self.assertNotIn("POWER_POLICY_STATE_LOW_BATTERY_WARN", POWER_POLICY_HEADER.read_text(encoding="utf-8"))
        self.assertNotIn("esp_deep_sleep_start", power_source)
        self.assertNotIn("esp_deep_sleep_start", background_source)

        low_battery_branch = power_source.split("if (budget.low_battery_warn)", 1)[1].split(
            "if (maintenance_window_active)", 1
        )[0]
        self.assertNotIn("budget.state", low_battery_branch)
        self.assertNotIn("sleep_permission", low_battery_branch)
        self.assertNotIn("sleep_interval_hint_ms", low_battery_branch)
        self.assertNotIn("display_budget", low_battery_branch)
        self.assertNotIn("background_budget", low_battery_branch)
        self.assertNotIn("network_sync_allowed = false", low_battery_branch)
        self.assertNotIn("ui_high_refresh_allowed = false", low_battery_branch)
        self.assertNotIn("esp_light_sleep_start", power_source)

    def test_p0_alert_wakes_ui_on_first_raise_and_repeat(self) -> None:
        source = APP_ALERT_MANAGER_SOURCE.read_text(encoding="utf-8")

        first_check = source.index("ESP_RETURN_ON_FALSE(initialized")
        wake_pos = source.index("ui_refresh_policy_notify_activity();")
        repeat_pos = source.index("if (same_source_active)")
        show_pos = source.index("display_alert_adapter_show_danger_overlay")

        self.assertLess(first_check, wake_pos)
        self.assertLess(wake_pos, repeat_pos)
        self.assertLess(wake_pos, show_pos)

    def test_standby_network_budget_enables_wifi_ps_without_disconnect(self) -> None:
        source = NETWORK_SERVICE_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "services/power_policy.h"', source)
        self.assertIn('#include "wifi_control.h"', source)
        self.assertIn("network_service_apply_power_budget", source)
        self.assertIn("POWER_POLICY_STATE_STANDBY", source)
        self.assertIn("wifi_control_set_power_save(power_save)", source)
        self.assertIn("network sync paused by power budget", source)

        helper_match = re.search(
            r"static void network_service_apply_power_budget\(void\)\s*\{(?P<body>.*?)\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(helper_match)
        helper_body = helper_match.group("body")
        self.assertNotIn("network_manager_disconnect", helper_body)
        self.assertNotIn("esp_wifi_stop", helper_body)

    def test_power_policy_does_not_control_ui_refresh_or_display(self) -> None:
        source = _strip_c_comments_and_strings(
            POWER_POLICY_SOURCE.read_text(encoding="utf-8")
        )

        self.assertNotIn("ui_refresh_policy_poll", source)
        self.assertNotIn("co5300_panel", source)
        self.assertNotIn("set_brightness", source)
        self.assertNotIn("lv_timer_handler", source)
        self.assertNotIn("lv_obj", source)

    def test_power_policy_publishes_sleep_budget_without_sleep_api(self) -> None:
        header = POWER_POLICY_HEADER.read_text(encoding="utf-8")
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn("power_policy_sleep_permission_t", header)
        self.assertIn("POWER_POLICY_SLEEP_LIGHT_ALLOWED", header)
        self.assertIn("POWER_POLICY_SLEEP_BLOCKER_UI_FORCE_ACTIVE", header)
        self.assertIn("sleep_interval_hint_ms", header)
        self.assertIn("sleep_permission", source)
        self.assertIn("sleep_blockers", source)
        self.assertIn("power_policy_format_sleep_blockers", source)
        self.assertIn("power_budget_change:", source)
        self.assertIn("POWER_POLICY_SLEEP_LIGHT_ALLOWED", source)
        self.assertIn("k_light_allowed_idle_time_ms = 5LL * 60LL * 1000LL", source)
        self.assertLess(
            source.index("activity_snapshot.idle_time_ms >= k_light_allowed_idle_time_ms"),
            source.index("budget.sleep_permission = POWER_POLICY_SLEEP_LIGHT_ALLOWED"),
        )
        self.assertNotIn("esp_light_sleep_start", source)
        self.assertNotIn("esp_deep_sleep_start", source)

    def test_power_policy_uses_freertos_task_notify_and_budget_version(self) -> None:
        header = POWER_POLICY_HEADER.read_text(encoding="utf-8")
        source = POWER_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn("power_policy_notify_reason_t", header)
        self.assertIn("POWER_POLICY_NOTIFY_UI_ACTIVITY", header)
        self.assertIn("POWER_POLICY_NOTIFY_POWER_STATE", header)
        self.assertIn("POWER_POLICY_NOTIFY_MAINTENANCE", header)
        self.assertIn("esp_err_t power_policy_notify(uint32_t reason);", header)
        self.assertIn("budget_version", header)
        self.assertIn("last_notify_reasons", header)

        self.assertIn('#include "freertos/task.h"', source)
        self.assertIn("static void power_policy_task(void *arg)", source)
        self.assertIn("xTaskCreate(power_policy_task", source)
        self.assertIn("xTaskNotifyWait", source)
        self.assertIn("xTaskNotify(task_handle, reason, eSetBits)", source)
        self.assertIn("k_policy_task_period_ticks = pdMS_TO_TICKS(1000)", source)
        self.assertIn("s_budget_version++", source)
        self.assertIn("power_budget_change: version=%u reasons=0x%08", source)
        self.assertIn("policy task started", source)

        get_budget_match = re.search(
            r"power_policy_budget_t power_policy_get_budget\(void\)\s*\{(?P<body>.*?)\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(get_budget_match)
        get_budget_body = get_budget_match.group("body")
        self.assertIn("power_policy_load_budget()", get_budget_body)
        self.assertNotIn("power_policy_build_budget(power_service_get_state())", get_budget_body)

    def test_sleep_coordinator_is_dry_run_only(self) -> None:
        self.assertTrue(SLEEP_COORDINATOR_HEADER.exists())
        self.assertTrue(SLEEP_COORDINATOR_SOURCE.exists())

        header = SLEEP_COORDINATOR_HEADER.read_text(encoding="utf-8")
        raw_source = SLEEP_COORDINATOR_SOURCE.read_text(encoding="utf-8")
        source = _strip_c_comments_and_strings(raw_source)

        self.assertIn("SLEEP_COORDINATOR_MODE_DRY_RUN", header)
        self.assertIn("sleep_coordinator_start", header)
        self.assertIn("budget_version", header)
        self.assertNotIn("SLEEP_COORDINATOR_MODE_LIGHT_TEST", header)
        self.assertNotIn("SLEEP_COORDINATOR_MODE_DEEP_TEST", header)
        self.assertNotIn("sleep_coordinator_sleep_test_result_t", header)
        self.assertNotIn("sleep_coordinator_get_sleep_test_result", header)
        self.assertIn("power_policy_get_budget()", source)
        self.assertLess(
            source.index("if (mode != SLEEP_COORDINATOR_MODE_DRY_RUN)"),
            source.index("power_policy_get_budget()"),
        )
        self.assertIn("dry_run:", raw_source)
        self.assertIn("budget_version=%u", raw_source)
        self.assertIn("s_sleep_coordinator.budget_version = budget.budget_version", raw_source)
        self.assertNotIn("SLEEP_COORDINATOR_LIGHT_TEST_ENABLED", raw_source)
        self.assertNotIn("SLEEP_COORDINATOR_LIGHT_TEST_OWNER_BLOCKERS_READY", raw_source)
        self.assertNotIn("light_test", raw_source)
        self.assertNotIn("esp_sleep_enable_timer_wakeup", source)
        self.assertNotIn("esp_light_sleep_start", source)
        self.assertNotIn("esp_sleep_get_wakeup_cause", source)
        self.assertIn("ESP_ERR_NOT_SUPPORTED", source)
        self.assertNotIn("ui_refresh_policy", source)
        self.assertNotIn("network_service", source)
        self.assertNotIn("network_manager", source)
        self.assertNotIn("wifi_control", source)
        self.assertNotIn("esp_wifi", source)
        self.assertNotIn("audio_codec", source)
        self.assertNotIn("axp2101", source)
        self.assertNotIn("pcf85063", source)
        self.assertNotIn("esp_deep_sleep_start", source)

    def test_manual_sleep_api_calls_are_absent_from_firmware_sources(self) -> None:
        forbidden_hits = []

        for path in _iter_project_source_files():
            text = path.read_text(encoding="utf-8")
            for api in FORBIDDEN_SLEEP_APIS:
                if api in text:
                    forbidden_hits.append(f"{path}:{api}")

        self.assertEqual(
            forbidden_hits,
            [],
            msg="manual sleep APIs still present: " + ", ".join(forbidden_hits),
        )


if __name__ == "__main__":
    unittest.main()
