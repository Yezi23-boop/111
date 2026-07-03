#!/usr/bin/env python3
"""Agent-friendly ESP-IDF serial monitor wrapper.

This tool keeps `idf.py monitor` as the low-level transport, but adds the parts
agents repeatedly need: bounded capture windows, stable log file names, process
cleanup, and a small JSON summary with key evidence lines.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Iterable


DEFAULT_IDF_EXPORT_PS1 = r"D:\esp-idf\v5.5.3\esp-idf\export.ps1"
DEFAULT_BAUD = 115200
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
OBSERVE_START_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("boot_rom_seen", re.compile(r"ESP-ROM:esp32s3")),
    ("app_main_called", re.compile(r"main_task: Calling app_main\(\)")),
)

EVIDENCE_PATTERNS: dict[str, str] = {
    "boot_rom_seen": r"ESP-ROM:esp32s3",
    "app_main_called": r"main_task: Calling app_main\(\)",
    "startup_done": r"boot_stage: startup_sequence_done",
    "ui_first_frame": r"boot_stage: ui_first_frame_ready",
    "rtc_snapshot": r"rtc_time_snapshot:",
    "rtc_bootstrap_ok": r"system_time: rtc bootstrap ok",
    "sntp_started": r"system_time: SNTP started",
    "sntp_sync_ok": r"system_time: sntp sync ok source=SNTP",
    "rtc_writeback": r"rtc_writeback=1",
    "network_service_ready": r"network state: WIFI_READY -> SERVICE_READY",
    "power_snapshot": r"Board power boot snapshot:",
    "rtc_timer_evidence": r"rtc_timer_stopped_after_evidence",
    "light_sleep_skipped": r"light_sleep_test_skipped:",
}

PRESET_PATTERNS: dict[str, dict[str, str]] = {
    "standby": {
        "standby_budget": r"power_budget_change: state=STANDBY",
        "standby_brightness_zero": r"apply brightness state=standby.*target=0%",
        "standby_wifi_ps": r"Wi-Fi power save enabled by power budget",
        "standby_network_paused": r"network state: SERVICE_READY -> WIFI_READY",
    },
    "system-time": {
        "system_time_boot": r"system_time_boot:",
        "rtc_bootstrap_done": r"rtc bootstrap done|rtc bootstrap ok",
        "sntp_sync_ok": r"sntp sync ok source=SNTP",
        "rtc_writeback": r"rtc_writeback=1",
    },
    "runtime-gate": {
        "runtime_gate_test": r"runtime_gate_test",
        "foreground_runtime": r"foreground_runtime|foreground_audio_active",
        "background_https": r"background_https",
        "memory_watch": r"memory_watch",
    },
    "wifi": {
        "wifi_connected": r"wifi:connected with",
        "wifi_disconnect_reason": r"reason=[0-9]+",
        "wifi_service_ready": r"network state: WIFI_READY -> SERVICE_READY",
        "wifi_power_save": r"Wi-Fi power save (enabled|disabled) by power budget",
    },
}

HARD_FATAL_PATTERNS: dict[str, str] = {
    "guru_meditation": r"Guru Meditation",
    "panic": r"\bpanic\b|PANIC",
    "abort": r"\babort\(\)|abort was called",
    "watchdog": r"watchdog|Task watchdog|WDT",
    "flash_checksum_mismatch": r"checksum mismatch",
}

DIAGNOSTIC_PATTERNS: dict[str, str] = {
    "no_mem": r"ESP_ERR_NO_MEM|NO_MEM",
    "stack_overflow": r"stack overflow",
    "wifi_disconnect_reason": r"reason=[0-9]+",
}


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def now_stamp() -> str:
    return dt.datetime.now().strftime("%Y-%m-%d-%H-%M-%S")


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def safe_console_write(text: str) -> None:
    """Write best-effort console output without breaking UTF-8 log capture."""
    encoding = sys.stdout.encoding or "utf-8"
    safe_text = text.encode(encoding, errors="replace").decode(encoding, errors="replace")
    sys.stdout.write(safe_text)
    sys.stdout.flush()


def build_idf_command(args: argparse.Namespace, repo_root: Path) -> list[str]:
    action = {
        "monitor": "monitor",
        "app-flash-monitor": "app-flash monitor",
    }[args.action]
    monitor_options = " --no-reset" if args.no_reset else ""

    powershell = (
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        "$OutputEncoding=[System.Text.Encoding]::UTF8; "
        f"& '{args.idf_export_ps1}'; "
        f"Set-Location '{repo_root}'; "
        f"$env:ESP_IDF_MONITOR_TEST='1'; "
        f"idf.py -p {args.port} -b {args.baud} {action}{monitor_options}"
    )
    return [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        powershell,
    ]


def reader_thread(stream, output: queue.Queue[str]) -> None:
    try:
        for line in iter(stream.readline, ""):
            output.put(line)
    finally:
        output.put("")


def kill_process_tree(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(
            ["taskkill.exe", "/T", "/F", "/PID", str(process.pid)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    else:
        process.kill()


def observe_start_trigger(line: str) -> str | None:
    for name, pattern in OBSERVE_START_PATTERNS:
        if pattern.search(line):
            return name
    return None


def run_capture(command: list[str], log_path: Path, args: argparse.Namespace) -> dict[str, object]:
    started_at = dt.datetime.now(dt.timezone.utc)
    observe_started = args.action != "app-flash-monitor"
    observe_trigger = "command_start" if observe_started else None
    observe_started_at = started_at if observe_started else None
    deadline = time.monotonic() + (
        args.duration_seconds if observe_started else args.flash_timeout_seconds
    )
    timed_out = False
    timed_out_phase = None
    lines: list[str] = []

    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )
    assert process.stdout is not None

    output: queue.Queue[str] = queue.Queue()
    thread = threading.Thread(target=reader_thread, args=(process.stdout, output), daemon=True)
    thread.start()

    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", newline="\n") as log_file:
        while True:
            if process.poll() is not None and output.empty():
                break
            if time.monotonic() >= deadline:
                timed_out = True
                timed_out_phase = "observe" if observe_started else "pre_observe"
                kill_process_tree(process)
            try:
                line = output.get(timeout=0.2)
            except queue.Empty:
                continue
            if line == "":
                continue
            clean_line = strip_ansi(line)
            log_file.write(clean_line)
            log_file.flush()
            clean_text = clean_line.rstrip("\r\n")
            lines.append(clean_text)
            if not observe_started:
                trigger = observe_start_trigger(clean_text)
                if trigger is not None:
                    observe_started = True
                    observe_trigger = trigger
                    observe_started_at = dt.datetime.now(dt.timezone.utc)
                    deadline = time.monotonic() + args.duration_seconds
            if not timed_out and not args.quiet_console:
                safe_console_write(clean_line)

    ended_at = dt.datetime.now(dt.timezone.utc)
    return {
        "started_at": started_at.isoformat(),
        "ended_at": ended_at.isoformat(),
        "timed_out": timed_out,
        "timed_out_phase": timed_out_phase,
        "exit_code": process.poll(),
        "observe_started": observe_started,
        "observe_trigger": observe_trigger,
        "observe_started_at": observe_started_at.isoformat() if observe_started_at else None,
        "lines": lines,
    }


def collect_matches(lines: Iterable[str], patterns: dict[str, str]) -> dict[str, dict[str, object]]:
    results: dict[str, dict[str, object]] = {}
    compiled = {name: re.compile(pattern) for name, pattern in patterns.items()}
    for name in compiled:
        results[name] = {"count": 0, "first_line": None, "last_line": None}

    for index, line in enumerate(lines, start=1):
        for name, pattern in compiled.items():
            if pattern.search(line):
                item = results[name]
                item["count"] = int(item["count"]) + 1
                if item["first_line"] is None:
                    item["first_line"] = {"line": index, "text": line}
                item["last_line"] = {"line": index, "text": line}
    return results


def important_events(lines: list[str], limit: int) -> list[dict[str, object]]:
    keywords = (
        "boot_stage:",
        "system_time",
        "rtc_time_snapshot",
        "Board power boot snapshot",
        "NETWORK_SERVICE",
        "wifi_ctrl:",
        "Guru Meditation",
        "panic",
        "ESP_ERR",
        "rtc_timer",
        "light_sleep",
    )
    events: list[dict[str, object]] = []
    for index, line in enumerate(lines, start=1):
        if any(keyword in line for keyword in keywords):
            events.append({"line": index, "text": line})
    return events[-limit:]


def tail_lines(lines: list[str], limit: int) -> list[dict[str, object]]:
    if limit <= 0:
        return []
    first_line = max(1, len(lines) - limit + 1)
    return [
        {"line": first_line + offset, "text": line}
        for offset, line in enumerate(lines[-limit:])
    ]


def match_count(matches: dict[str, dict[str, object]], name: str) -> int:
    return int(matches.get(name, {}).get("count", 0))


def total_match_count(matches: dict[str, dict[str, object]]) -> int:
    return sum(int(item["count"]) for item in matches.values())


def flash_completed(lines: list[str]) -> bool | None:
    if not any("Executing action: app-flash" in line for line in lines):
        return None
    return any("Hash of data verified" in line or re.search(r"Wrote \d+ bytes", line) for line in lines)


def observation_complete(capture: dict[str, object]) -> bool:
    if not bool(capture["observe_started"]):
        return False
    if not bool(capture["timed_out"]):
        return True
    return capture["timed_out_phase"] == "observe"


def decide_status(
    args: argparse.Namespace,
    evidence: dict[str, dict[str, object]],
    hard_fatal: dict[str, dict[str, object]],
) -> str:
    if total_match_count(hard_fatal) > 0:
        return "fail"
    if match_count(evidence, "boot_rom_seen") == 0:
        if args.action == "app-flash-monitor":
            return "boot_missing_after_flash"
        return "observed_no_boot"
    if match_count(evidence, "startup_done") == 0:
        return "partial"
    return "ok"


def write_summary(
    args: argparse.Namespace,
    command: list[str],
    log_path: Path,
    capture: dict[str, object],
    summary_path: Path,
) -> dict[str, object]:
    lines = list(capture["lines"])
    evidence = collect_matches(lines, EVIDENCE_PATTERNS)
    hard_fatal = collect_matches(lines, HARD_FATAL_PATTERNS)
    diagnostic_events = collect_matches(lines, DIAGNOSTIC_PATTERNS)
    custom_evidence = collect_matches(lines, args.custom_patterns)
    boot_seen = match_count(evidence, "boot_rom_seen") > 0
    app_started = match_count(evidence, "app_main_called") > 0
    startup_done = match_count(evidence, "startup_done") > 0
    summary = {
        "tool": "agent_serial_monitor",
        "status": decide_status(args, evidence, hard_fatal),
        "action": args.action,
        "port": args.port,
        "baud": args.baud,
        "duration_seconds": args.duration_seconds,
        "flash_timeout_seconds": args.flash_timeout_seconds,
        "quiet_console": args.quiet_console,
        "reset_on_start": not args.no_reset,
        "timed_out": capture["timed_out"],
        "timed_out_phase": capture["timed_out_phase"],
        "exit_code": capture["exit_code"],
        "observe_started": capture["observe_started"],
        "observe_trigger": capture["observe_trigger"],
        "observe_started_at": capture["observe_started_at"],
        "flash_completed": flash_completed(lines),
        "boot_seen": boot_seen,
        "app_started": app_started,
        "startup_done": startup_done,
        "observation_complete": observation_complete(capture),
        "hard_fatal_count": total_match_count(hard_fatal),
        "diagnostic_event_count": total_match_count(diagnostic_events),
        "started_at": capture["started_at"],
        "ended_at": capture["ended_at"],
        "command": command,
        "log_path": str(log_path),
        "summary_path": str(summary_path),
        "evidence": evidence,
        "custom_evidence": custom_evidence,
        "fatal": hard_fatal,
        "hard_fatal": hard_fatal,
        "diagnostic_events": diagnostic_events,
        "important_events": important_events(lines, args.max_events),
        "tail": tail_lines(lines, args.tail_lines),
    }
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return summary


def safe_pattern_name(value: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip()).strip("_")
    return safe[:80] or "pattern"


def add_pattern(patterns: dict[str, str], name: str, pattern: str) -> None:
    base_name = safe_pattern_name(name)
    candidate = base_name
    suffix = 2
    while candidate in patterns:
        candidate = f"{base_name}_{suffix}"
        suffix += 1
    patterns[candidate] = pattern


def parse_custom_patterns(raw_patterns: list[str], raw_literal_patterns: list[str]) -> dict[str, str]:
    patterns: dict[str, str] = {}
    for raw in raw_patterns:
        value = raw.strip()
        if not value:
            raise ValueError("--pattern cannot be empty")

        if "=" in value:
            name, pattern = value.split("=", 1)
            name = name.strip()
            pattern = pattern.strip()
            if not name or not pattern:
                name = value
                pattern = re.escape(value)
        else:
            name = safe_pattern_name(value)
            pattern = re.escape(value)

        try:
            re.compile(pattern)
        except re.error as exc:
            raise ValueError(f"--pattern {raw!r} is not a valid regex: {exc}") from exc

        add_pattern(patterns, name, pattern)

    for raw in raw_literal_patterns:
        value = raw.strip()
        if not value:
            raise ValueError("--literal-pattern cannot be empty")
        add_pattern(patterns, value, re.escape(value))
    return patterns


def preset_patterns(presets: list[str]) -> dict[str, str]:
    patterns: dict[str, str] = {}
    for preset in presets:
        for name, pattern in PRESET_PATTERNS[preset].items():
            add_pattern(patterns, name, pattern)
    return patterns


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Bounded ESP-IDF serial monitor capture for agents.",
    )
    parser.add_argument("--port", required=True, help="Serial port, for example COM3.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument(
        "--action",
        choices=("monitor", "app-flash-monitor"),
        default="monitor",
        help="Use monitor only, or app-flash then monitor.",
    )
    parser.add_argument(
        "--duration-seconds",
        type=int,
        default=60,
        help=(
            "Observation window in seconds. For app-flash-monitor this starts "
            "after boot evidence is observed, so flashing time is not counted."
        ),
    )
    parser.add_argument(
        "--flash-timeout-seconds",
        type=int,
        default=180,
        help=(
            "Maximum time to allow app-flash-monitor to finish flashing and reach "
            "boot evidence before starting the observation window."
        ),
    )
    parser.add_argument("--tag", default="serial", help="File-name tag for logs.")
    parser.add_argument("--log-dir", default="board_logs")
    parser.add_argument("--idf-export-ps1", default=DEFAULT_IDF_EXPORT_PS1)
    parser.add_argument("--max-events", type=int, default=80)
    parser.add_argument(
        "--preset",
        action="append",
        choices=tuple(PRESET_PATTERNS.keys()),
        default=[],
        help="Add a built-in evidence preset. Can be passed multiple times.",
    )
    parser.add_argument(
        "--pattern",
        action="append",
        default=[],
        help=(
            "Custom evidence pattern. Use 'name=regex' for regex matching, "
            "or a plain keyword for literal matching. If either side of '=' is empty, "
            "the whole value is treated as literal. Can be passed multiple times."
        ),
    )
    parser.add_argument(
        "--literal-pattern",
        action="append",
        default=[],
        help=(
            "Custom literal evidence pattern. Use this when the keyword contains '=' "
            "or when regex parsing is not wanted. Can be passed multiple times."
        ),
    )
    parser.add_argument(
        "--tail-lines",
        type=int,
        default=120,
        help="Number of recent log lines to include in summary JSON.",
    )
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="Pass --no-reset to idf.py monitor when observing an already-running board.",
    )
    parser.add_argument(
        "--quiet-console",
        action="store_true",
        default=None,
        help="Do not stream captured serial output to stdout; still writes full log and summary.",
    )
    parser.add_argument(
        "--stream-console",
        action="store_false",
        dest="quiet_console",
        help="Stream captured serial output to stdout even for app-flash-monitor.",
    )
    parser.add_argument(
        "--allow-no-boot",
        action="store_true",
        help="Return success even when no boot evidence is observed.",
    )
    args = parser.parse_args()
    if args.duration_seconds <= 0:
        parser.error("--duration-seconds must be > 0")
    if args.flash_timeout_seconds <= 0:
        parser.error("--flash-timeout-seconds must be > 0")
    if args.tail_lines < 0:
        parser.error("--tail-lines must be >= 0")
    try:
        args.custom_patterns = preset_patterns(args.preset)
        for name, pattern in parse_custom_patterns(args.pattern, args.literal_pattern).items():
            add_pattern(args.custom_patterns, name, pattern)
    except ValueError as exc:
        parser.error(str(exc))
    if args.quiet_console is None:
        args.quiet_console = args.action == "app-flash-monitor"
    return args


def main() -> int:
    args = parse_args()
    repo_root = repo_root_from_script()
    log_dir = Path(args.log_dir)
    if not log_dir.is_absolute():
        log_dir = repo_root / log_dir
    stamp = now_stamp()
    safe_tag = re.sub(r"[^A-Za-z0-9_.-]+", "-", args.tag).strip("-") or "serial"
    log_path = log_dir / f"{stamp}-{safe_tag}.log"
    summary_path = log_dir / f"{stamp}-{safe_tag}.summary.json"

    command = build_idf_command(args, repo_root)
    capture = run_capture(command, log_path, args)
    summary = write_summary(args, command, log_path, capture, summary_path)

    print()
    print(f"AGENT_SERIAL_MONITOR_STATUS={summary['status']}")
    print(f"AGENT_SERIAL_MONITOR_LOG={log_path}")
    print(f"AGENT_SERIAL_MONITOR_SUMMARY={summary_path}")
    if summary["status"] in ("ok", "partial"):
        return 0
    if args.allow_no_boot and summary["status"] in ("observed_no_boot", "boot_missing_after_flash"):
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
