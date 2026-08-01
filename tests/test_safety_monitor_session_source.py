import unittest

from tests.main_cmake_contract import assert_main_source_globbed
from tests.main_paths import REPO_ROOT
from tests.main_paths import SAFETY_MONITOR_POLICY_HEADER
from tests.main_paths import SAFETY_MONITOR_POLICY_SOURCE
from tests.main_paths import SAFETY_MONITOR_SESSION_HEADER
from tests.main_paths import SAFETY_MONITOR_SESSION_SOURCE


MAIN_CMAKE = REPO_ROOT / "main" / "CMakeLists.txt"


class SafetyMonitorSessionSourceTests(unittest.TestCase):
    def test_session_module_owns_danger_runtime_lifecycle(self) -> None:
        header = SAFETY_MONITOR_SESSION_HEADER.read_text(encoding="utf-8")
        source = SAFETY_MONITOR_SESSION_SOURCE.read_text(encoding="utf-8")

        self.assertIn("safety_monitor_session_apply", header)
        self.assertIn("safety_monitor_session_get_snapshot", header)
        self.assertIn("danger_detection_service_start()", source)
        self.assertIn("danger_detection_service_stop(0U)", source)
        self.assertIn("DANGER_DETECTION_STATE_ERROR", source)
        self.assertIn("safety_monitor_session_recover_error", source)
        self.assertIn("safety_monitor_session_snapshot_is_running", source)
        self.assertIn("snapshot->state == DANGER_DETECTION_STATE_STOPPING", source)
        self.assertIn("safety_monitor_session_store(ret, true)", source)
        self.assertIn("previous_runtime_running", source)
        self.assertIn("k_start_retry_ticks", source)

    def test_safety_policy_delegates_runtime_lifecycle_to_session(self) -> None:
        header = SAFETY_MONITOR_POLICY_HEADER.read_text(encoding="utf-8")
        source = SAFETY_MONITOR_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "audio_codec.h"', source)
        self.assertIn("safety_monitor_policy_block_reason_t", header)
        self.assertIn("SAFETY_MONITOR_POLICY_BLOCK_USER_DISABLED", header)
        self.assertIn("SAFETY_MONITOR_POLICY_BLOCK_POWER", header)
        self.assertIn("SAFETY_MONITOR_POLICY_BLOCK_FOREGROUND_AUDIO", header)
        self.assertIn("SAFETY_MONITOR_POLICY_BLOCK_RUNTIME_COORDINATOR", header)
        self.assertIn("should_run", header)
        self.assertIn("block_reason", header)
        self.assertIn("blocked_by_runtime_coordinator", header)
        self.assertIn('#include "services/runtime/runtime_coordinator.h"', source)
        self.assertIn("safety_monitor_policy_set_foreground_audio_active", header)
        self.assertIn("foreground_audio_active", source)
        self.assertIn("audio_codec_get_session_snapshot", source)
        self.assertIn("audio_codec_owner_to_text", source)
        self.assertIn("AUDIO_CODEC_OWNER_HERMES", source)
        self.assertIn("safety_monitor_policy_request_quiesce", source)
        self.assertIn("runtime_coordinator_report_quiesce_result", source)
        self.assertIn("safety_monitor_policy_resolve_block", source)
        self.assertIn("SAFETY_MONITOR_POLICY_BLOCK_NONE", source)
        self.assertIn("safety_monitor_session_apply(should_run, reason)", source)
        self.assertIn("safety_monitor_session_get_snapshot()", source)
        self.assertIn("startup_readiness_wait_ui_first_frame(portMAX_DELAY)", source)
        self.assertIn("ready: ui_first_frame_ready", source)
        self.assertNotIn("k_boot_defer_ticks", source)
        self.assertNotIn("danger_detection_service_start_with_backend", source)
        self.assertNotIn("danger_detection_service_start()", source)
        self.assertNotIn("danger_detection_service_stop(", source)
        self.assertNotIn("DANGER_DETECTION_BACKEND_ESPDL", source)
        self.assertNotIn("DANGER_DETECTION_STATE_ERROR", source)
        self.assertNotIn("vTaskSuspend", source)
        self.assertNotIn("vTaskDelete", source)

    def test_safety_policy_uses_notify_plus_periodic_fallback(self) -> None:
        header = SAFETY_MONITOR_POLICY_HEADER.read_text(encoding="utf-8")
        source = SAFETY_MONITOR_POLICY_SOURCE.read_text(encoding="utf-8")

        self.assertIn("safety_monitor_policy_notify_power_changed", header)
        self.assertIn("SAFETY_MONITOR_POLICY_NOTIFY_USER", source)
        self.assertIn("SAFETY_MONITOR_POLICY_NOTIFY_AUDIO", source)
        self.assertIn("SAFETY_MONITOR_POLICY_NOTIFY_POWER", source)
        self.assertIn("xTaskNotify(task_handle, reasons, eSetBits)", source)
        self.assertIn("xTaskNotifyWait(0U, UINT32_MAX, &reasons", source)
        self.assertIn("kPolicyPollTicks = pdMS_TO_TICKS(1000)", source)
        self.assertIn("SAFETY_MONITOR_POLICY_NOTIFY_PERIODIC", source)
        self.assertIn("safety_monitor_policy_apply(\"startup\")", source)

    def test_power_policy_only_notifies_safety_policy_on_budget_change(self) -> None:
        source = (
            REPO_ROOT / "main" / "services" / "power" / "power_policy.c"
        ).read_text(
            encoding="utf-8"
        )

        self.assertIn('#include "services/runtime/safety_monitor_policy.h"', source)
        self.assertIn("safety_monitor_policy_notify_power_changed()", source)
        self.assertNotIn("safety_monitor_policy_set_enabled", source)
        self.assertNotIn("safety_monitor_session_apply", source)

    def test_main_cmake_registers_safety_monitor_session_source(self) -> None:
        assert_main_source_globbed(self, "services/safety/safety_monitor_session.c")


if __name__ == "__main__":
    unittest.main()
