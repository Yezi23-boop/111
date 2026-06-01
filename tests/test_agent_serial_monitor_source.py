import pathlib
import unittest
import importlib.util


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
PY_MONITOR = REPO_ROOT / "scripts" / "board" / "agent_serial_monitor.py"
PS_MONITOR = REPO_ROOT / "scripts" / "board" / "agent_serial_monitor.ps1"


class AgentSerialMonitorSourceTests(unittest.TestCase):
    def test_python_monitor_wraps_idf_monitor_with_bounded_capture(self) -> None:
        source = PY_MONITOR.read_text(encoding="utf-8")

        self.assertIn("ESP_IDF_MONITOR_TEST", source)
        self.assertIn("idf.py -p {args.port}", source)
        self.assertIn("app-flash monitor", source)
        self.assertIn(" --no-reset", source)
        self.assertIn("duration_seconds", source)
        self.assertIn("flash_timeout_seconds", source)
        self.assertIn("observe_start_trigger", source)
        self.assertIn("observe_started", source)
        self.assertIn("timed_out_phase", source)
        self.assertIn("kill_process_tree", source)
        self.assertIn("taskkill.exe", source)

    def test_python_monitor_writes_agent_readable_summary_json(self) -> None:
        source = PY_MONITOR.read_text(encoding="utf-8")

        self.assertIn("summary_path", source)
        self.assertIn("json.dumps(summary, ensure_ascii=False, indent=2)", source)
        self.assertIn("AGENT_SERIAL_MONITOR_STATUS", source)
        self.assertIn("AGENT_SERIAL_MONITOR_LOG", source)
        self.assertIn("AGENT_SERIAL_MONITOR_SUMMARY", source)
        self.assertIn('"custom_evidence"', source)
        self.assertIn('"tail"', source)
        self.assertIn('"reset_on_start"', source)
        self.assertIn('"flash_timeout_seconds"', source)
        self.assertIn('"quiet_console"', source)
        self.assertIn('"observe_started"', source)
        self.assertIn('"observe_trigger"', source)

    def test_python_monitor_extracts_project_evidence_and_fatal_patterns(self) -> None:
        source = PY_MONITOR.read_text(encoding="utf-8")

        self.assertIn('"rtc_bootstrap_ok"', source)
        self.assertIn('"sntp_sync_ok"', source)
        self.assertIn('"rtc_writeback"', source)
        self.assertIn('"startup_done"', source)
        self.assertIn('"guru_meditation"', source)
        self.assertIn('"flash_checksum_mismatch"', source)

    def test_python_monitor_supports_custom_patterns_and_summary_tail(self) -> None:
        source = PY_MONITOR.read_text(encoding="utf-8")

        self.assertIn("parse_custom_patterns", source)
        self.assertIn("safe_pattern_name", source)
        self.assertIn("add_pattern", source)
        self.assertIn("--pattern", source)
        self.assertIn("--literal-pattern", source)
        self.assertIn("name=regex", source)
        self.assertIn("re.escape(value)", source)
        self.assertIn("--tail-lines", source)
        self.assertIn("tail_lines(lines, args.tail_lines)", source)
        self.assertIn("--no-reset", source)
        self.assertIn("--quiet-console", source)

    def test_python_monitor_treats_empty_named_regex_as_literal(self) -> None:
        spec = importlib.util.spec_from_file_location("agent_serial_monitor", PY_MONITOR)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        patterns = module.parse_custom_patterns(
            ["reason=", "wifi_reason=reason=[0-9]+"],
            ["reason=201"],
        )

        self.assertEqual(patterns["reason"], "reason=")
        self.assertEqual(patterns["wifi_reason"], "reason=[0-9]+")
        self.assertEqual(patterns["reason_201"], "reason=201")

    def test_python_monitor_detects_observation_start(self) -> None:
        spec = importlib.util.spec_from_file_location("agent_serial_monitor", PY_MONITOR)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        self.assertEqual(
            module.observe_start_trigger("ESP-ROM:esp32s3-20210327"),
            "boot_rom_seen",
        )
        self.assertEqual(
            module.observe_start_trigger("I (123) main_task: Calling app_main()"),
            "app_main_called",
        )
        self.assertIsNone(module.observe_start_trigger("Writing at 0x123456... (79 %)"))

    def test_powershell_wrapper_lists_ports_and_auto_resolves_board_port(self) -> None:
        source = PS_MONITOR.read_text(encoding="utf-8")

        self.assertIn("Get-CimInstance Win32_SerialPort", source)
        self.assertIn("Resolve-BoardPort", source)
        self.assertIn('Where-Object { $_.DeviceID -ne "COM1" }', source)
        self.assertIn("--duration-seconds", source)
        self.assertIn("--flash-timeout-seconds", source)
        self.assertIn("--idf-export-ps1", source)
        self.assertIn("[string[]]$Pattern", source)
        self.assertIn("[string[]]$LiteralPattern", source)
        self.assertIn("[int]$FlashTimeoutSeconds = 180", source)
        self.assertIn("[int]$TailLines = 120", source)
        self.assertIn("[switch]$NoReset", source)
        self.assertIn("[switch]$QuietConsole", source)
        self.assertIn("--tail-lines", source)
        self.assertIn("--pattern", source)
        self.assertIn("--literal-pattern", source)
        self.assertIn("--no-reset", source)
        self.assertIn("--quiet-console", source)


if __name__ == "__main__":
    unittest.main()
