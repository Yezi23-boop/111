import importlib.util
import os
import pathlib
import shutil
import tempfile
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
    def test_python_monitor_flashes_then_captures_with_bounds(self) -> None:
        source = PY_MONITOR.read_text(encoding="utf-8")

        # 新设计：pySerial 直读独占端口，flash 阶段只走 idf.py app-flash，
        # 不再 spawn `idf.py monitor`，避免残留 monitor 抢占端口。
        self.assertIn("import serial", source)
        self.assertIn("exclusive=True", source)
        self.assertIn("acquire_port_lock", source)
        self.assertIn("idf.py -p {args.port} -b {args.flash_baud} app-flash", source)
        self.assertIn("app-flash-monitor", source)

        # 有界采集：观测窗口与 flash 超时分离，观测起点由日志特征触发。
        self.assertIn("duration_seconds", source)
        self.assertIn("flash_timeout_seconds", source)
        self.assertIn("observe_start_trigger", source)
        self.assertIn("timed_out_phase", source)

        # flash 超时兜底：杀进程树。
        self.assertIn("kill_process_tree", source)
        self.assertIn("taskkill.exe", source)

        # 旧实现痕迹（spawn idf.py monitor / 测试标记）必须不存在。
        self.assertNotIn("app-flash monitor", source)
        self.assertNotIn("ESP_IDF_MONITOR_TEST", source)

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

    def test_python_monitor_port_lock_rejects_live_and_reclaims_stale(self) -> None:
        module = load_monitor_module()
        tmp_dir = pathlib.Path(tempfile.mkdtemp(prefix="asm-lock-"))
        self.addCleanup(shutil.rmtree, tmp_dir, ignore_errors=True)

        # 活锁：owner 仍在运行，同一端口二次捕获必须失败（两个 capture 不能
        # 抢同一 COM 口）。
        live_lock = tmp_dir / "COM3.lock"
        live_lock.write_text(str(os.getpid()), encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "already captured"):
            module.acquire_port_lock("COM3", tmp_dir)

        # 死锁：owner 已退出，锁应被回收并由当前进程重新持有。
        stale_lock = tmp_dir / "COM7.lock"
        stale_lock.write_text("99999999", encoding="utf-8")
        acquired = module.acquire_port_lock("COM7", tmp_dir)
        self.assertEqual(acquired, stale_lock)
        self.assertEqual(stale_lock.read_text(encoding="utf-8"), str(os.getpid()))

        # 旧“清扫残留 idf_monitor 进程”机制已被 per-port lock 取代。
        self.assertNotIn("is_residual_monitor_process", PY_MONITOR.read_text(encoding="utf-8"))
        self.assertNotIn("terminate_residual_monitor_processes", PY_MONITOR.read_text(encoding="utf-8"))

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
