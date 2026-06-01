import unittest

from tests.main_paths import BACKGROUND_SERVICE_MANAGER_SOURCE
from tests.main_paths import BACKGROUND_SERVICE_MANAGER_HEADER
from tests.main_paths import REPO_ROOT
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

    def test_background_manager_delegates_runtime_lifecycle_to_session(self) -> None:
        header = BACKGROUND_SERVICE_MANAGER_HEADER.read_text(encoding="utf-8")
        source = BACKGROUND_SERVICE_MANAGER_SOURCE.read_text(encoding="utf-8")

        self.assertIn('#include "audio_codec.h"', source)
        self.assertIn("background_service_manager_danger_block_reason_t", header)
        self.assertIn("BACKGROUND_SERVICE_MANAGER_DANGER_BLOCK_USER_DISABLED", header)
        self.assertIn("BACKGROUND_SERVICE_MANAGER_DANGER_BLOCK_POLICY", header)
        self.assertIn("BACKGROUND_SERVICE_MANAGER_DANGER_BLOCK_FOREGROUND_AUDIO", header)
        self.assertIn("danger_should_run", header)
        self.assertIn("danger_block_reason", header)
        self.assertIn("danger_blocked_by_foreground_audio", header)
        self.assertIn(
            "background_service_manager_set_foreground_audio_active",
            header,
        )
        self.assertIn("foreground_audio_active", source)
        self.assertIn("audio_codec_get_session_snapshot", source)
        self.assertIn("audio_codec_owner_to_text", source)
        self.assertIn("resource_blocked_change: resource=mic", source)
        self.assertIn("background_target_change: danger_should_run=%d", source)
        self.assertIn("background_service_manager_resolve_danger_block_reason", source)
        self.assertIn("BACKGROUND_SERVICE_MANAGER_DANGER_BLOCK_NONE", source)
        self.assertIn("safety_monitor_session_apply(should_run, reason)", source)
        self.assertIn("safety_monitor_session_get_snapshot()", source)
        self.assertIn("startup_readiness_wait_ui_first_frame(portMAX_DELAY)", source)
        self.assertIn("background_gate_ready: ui_first_frame_ready", source)
        self.assertNotIn("k_boot_defer_ticks", source)
        self.assertNotIn("danger_detection_service_start_with_backend", source)
        self.assertNotIn("danger_detection_service_start()", source)
        self.assertNotIn("danger_detection_service_stop(", source)
        self.assertNotIn("DANGER_DETECTION_BACKEND_ESPDL", source)
        self.assertNotIn("DANGER_DETECTION_STATE_ERROR", source)

    def test_main_cmake_registers_safety_monitor_session_source(self) -> None:
        source = MAIN_CMAKE.read_text(encoding="utf-8")

        self.assertIn(
            "${CMAKE_CURRENT_LIST_DIR}/services/safety_monitor_session.c",
            source,
        )


if __name__ == "__main__":
    unittest.main()
