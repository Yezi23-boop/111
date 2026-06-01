import unittest

from tests.main_paths import REPO_ROOT


BOARD_VALIDATION_SCRIPT = (
    REPO_ROOT / "scripts" / "board" / "validate_safety_monitor_board.ps1"
)


class SafetyMonitorBoardValidationScriptTests(unittest.TestCase):
    def test_script_captures_required_board_evidence_patterns(self) -> None:
        source = BOARD_VALIDATION_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("param(", source)
        self.assertIn("[switch]$ListPorts", source)
        self.assertIn("[switch]$NoFlash", source)
        self.assertIn("Get-CimInstance Win32_SerialPort", source)
        self.assertIn("idf.py -p $boardPort $idfAction", source)
        self.assertIn("boot_stage: startup_sequence_done", source)
        self.assertIn("boot_stage: ui_first_frame_ready", source)
        self.assertIn("background danger detection started", source)
        self.assertIn("INFERENCE #", source)
        self.assertIn("foreground_audio_active: active=1 reason=official_chat", source)
        self.assertIn("resource_blocked_change: resource=mic danger=1", source)
        self.assertIn("danger risk: .* -> ALERTING", source)

    def test_script_requires_explicit_port_when_auto_detect_is_ambiguous(self) -> None:
        source = BOARD_VALIDATION_SCRIPT.read_text(encoding="utf-8")

        self.assertIn('Where-Object { $_.DeviceID -ne "COM1" }', source)
        self.assertIn("Pass -Port explicitly", source)
        self.assertNotIn("COM3", source)


if __name__ == "__main__":
    unittest.main()
