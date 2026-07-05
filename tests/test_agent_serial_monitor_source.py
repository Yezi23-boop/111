import importlib.util
import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PY_MONITOR = REPO_ROOT / "scripts" / "board" / "agent_serial_monitor.py"
PS_MONITOR = REPO_ROOT / "scripts" / "board" / "agent_serial_monitor.ps1"


def load_monitor_module():
    spec = importlib.util.spec_from_file_location("agent_serial_monitor", PY_MONITOR)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AgentSerialMonitorSourceTests(unittest.TestCase):
    def test_python_monitor_wraps_idf_monitor_with_bounded_capture(self) -> None:
        source = PY_MONITOR.read_text(encoding="utf-8")

        self.assertIn("ESP_IDF_MONITOR_TEST", source)
        self.assertIn("idf.py -p {args.port}", source)
        self.assertIn("app-flash monitor", source)
        self.assertIn("duration_seconds", source)
        self.assertIn("flash_timeout_seconds", source)
        self.assertIn("observe_start_trigger", source)
        self.assertIn("timed_out_phase", source)
        self.assertIn("kill_process_tree", source)
        self.assertIn("taskkill.exe", source)

    def test_python_monitor_summary_is_capture_metadata_not_health_judgment(self) -> None:
        source = PY_MONITOR.read_text(encoding="utf-8")

        self.assertIn('"status": "captured"', source)
        self.assertIn('"line_count"', source)
        self.assertIn('"capture_stop_reason"', source)
        self.assertIn('"panic_log_seen"', source)
        self.assertIn('"log_path"', source)
        self.assertIn('"summary_path"', source)
        self.assertIn("AGENT_SERIAL_MONITOR_CAPTURE_STOP_REASON", source)
        self.assertIn("AGENT_SERIAL_MONITOR_PANIC_LOG_SEEN", source)

        self.assertNotIn("HARD_FATAL_PATTERNS", source)
        self.assertNotIn("DIAGNOSTIC_PATTERNS", source)
        self.assertNotIn("PRESET_PATTERNS", source)
        self.assertNotIn('"hard_fatal_count"', source)
        self.assertNotIn('"diagnostic_event_count"', source)
        self.assertNotIn('"fatal"', source)
        self.assertNotIn('"diagnostic_events"', source)
        self.assertNotIn('"custom_evidence"', source)
        self.assertNotIn('"program_crashed"', source)

    def test_python_monitor_panic_detector_requires_structure(self) -> None:
        module = load_monitor_module()

        started, confirmed = module.update_panic_detector("panic mentioned in text", False)
        self.assertFalse(started)
        self.assertFalse(confirmed)

        started, confirmed = module.update_panic_detector("Guru Meditation Error: Core  0 panic'ed", False)
        self.assertTrue(started)
        self.assertFalse(confirmed)

        started, confirmed = module.update_panic_detector("Backtrace: 0x403...", started)
        self.assertTrue(started)
        self.assertTrue(confirmed)

    def test_python_monitor_detects_observation_start(self) -> None:
        module = load_monitor_module()

        self.assertEqual(
            module.observe_start_trigger("ESP-ROM:esp32s3-20210327"),
            "boot_rom_seen",
        )
        self.assertEqual(
            module.observe_start_trigger("I (123) main_task: Calling app_main()"),
            "app_main_called",
        )
        self.assertIsNone(module.observe_start_trigger("Writing at 0x123456... (79 %)"))

    def test_python_monitor_residual_monitor_cleanup_is_retained(self) -> None:
        module = load_monitor_module()

        self.assertTrue(
            module.is_residual_monitor_process(
                {
                    "ProcessId": 101,
                    "CommandLine": "python D:/esp-idf/tools/idf_monitor.py -p COM3",
                },
                current_pid=999,
            )
        )
        self.assertEqual(module.extract_monitor_port("idf.py -p COM7 app-flash monitor"), "COM7")
        self.assertEqual(module.extract_monitor_port("idf_monitor.py --port COM12"), "COM12")
        self.assertFalse(
            module.is_residual_monitor_process(
                {
                    "ProcessId": 105,
                    "CommandLine": "python scripts/board/agent_serial_monitor.py idf.py -p COM7 monitor",
                },
                current_pid=999,
            )
        )

        kill_results = module.terminate_residual_monitor_processes(
            [{"pid": "not-a-pid", "port": "COM7", "name": "idf_monitor.py"}]
        )
        self.assertEqual(kill_results[0]["error"], "invalid_pid")
        self.assertFalse(kill_results[0]["terminated"])

    def test_powershell_wrapper_lists_ports_and_avoids_pattern_filters(self) -> None:
        source = PS_MONITOR.read_text(encoding="utf-8")

        self.assertIn("Get-CimInstance Win32_SerialPort", source)
        self.assertIn("Resolve-BoardPort", source)
        self.assertIn('Where-Object { $_.DeviceID -ne "COM1" }', source)
        self.assertIn("[switch]$AutoDetectEsp", source)
        self.assertIn("Invoke-Esp32S3Probe", source)
        self.assertIn("$Python -m esptool", source)
        self.assertIn("--duration-seconds", source)
        self.assertIn("--flash-timeout-seconds", source)
        self.assertIn("--idf-export-ps1", source)
        self.assertIn("--no-reset", source)
        self.assertIn("--quiet-console", source)
        self.assertIn("--stream-console", source)

        self.assertNotIn("[string[]]$Preset", source)
        self.assertNotIn("[string[]]$Pattern", source)
        self.assertNotIn("[string[]]$LiteralPattern", source)
        self.assertNotIn("--preset", source)
        self.assertNotIn("--pattern", source)
        self.assertNotIn("--literal-pattern", source)
        self.assertNotIn("--allow-no-boot", source)


if __name__ == "__main__":
    unittest.main()
