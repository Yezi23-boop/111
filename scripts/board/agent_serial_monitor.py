#!/usr/bin/env python3
"""Agent-friendly ESP-IDF serial monitor wrapper.

This tool keeps `idf.py monitor` as the low-level transport, but adds the parts
agents repeatedly need: bounded capture windows, stable log file names, process
cleanup, and a small JSON summary with capture facts.
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


DEFAULT_IDF_EXPORT_PS1 = r"D:\esp-idf\v5.5.3\esp-idf\export.ps1"
DEFAULT_BAUD = 115200
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
OBSERVE_START_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("boot_rom_seen", re.compile(r"ESP-ROM:esp32s3")),
    ("app_main_called", re.compile(r"main_task: Calling app_main\(\)")),
)

PANIC_START_RE = re.compile(
    r"Guru Meditation Error|panic'ed|abort\(\) was called|abort was called",
    re.IGNORECASE,
)
PANIC_CONFIRM_RE = re.compile(
    r"Backtrace:|ELF file SHA256|Rebooting\.\.\.|Core\s+\d+\s+register dump|register dump:",
    re.IGNORECASE,
)
PANIC_CAPTURE_GRACE_SECONDS = 1.0

RESIDUAL_MONITOR_PATTERNS: tuple[re.Pattern[str], ...] = (
    re.compile(r"\bidf_monitor\.py\b", re.IGNORECASE),
    re.compile(r"\bidf\.py\b.*\bmonitor\b", re.IGNORECASE),
    re.compile(r"\bESP_IDF_MONITOR_TEST\b", re.IGNORECASE),
)
MONITOR_PORT_PATTERNS: tuple[re.Pattern[str], ...] = (
    re.compile(r"(?:^|\s)(?:-p|--port)\s+(COM\d+)\b", re.IGNORECASE),
    re.compile(r"\bport\s*=\s*(COM\d+)\b", re.IGNORECASE),
)


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


def compact_command_line(command_line: object, limit: int = 400) -> str:
    text = "" if command_line is None else str(command_line)
    text = " ".join(text.split())
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def process_field(process: dict[str, object], *names: str) -> object:
    for name in names:
        if name in process:
            return process[name]
    return None


def process_id(process: dict[str, object]) -> int | None:
    value = process_field(process, "ProcessId", "pid")
    try:
        return int(value) if value is not None else None
    except (TypeError, ValueError):
        return None


def is_residual_monitor_process(process: dict[str, object], current_pid: int) -> bool:
    pid = process_id(process)
    if pid is None or pid == current_pid:
        return False
    command_line = compact_command_line(process_field(process, "CommandLine", "command_line"), 4000)
    if not command_line:
        return False
    if re.search(r"\bagent_serial_monitor\.py\b", command_line, re.IGNORECASE):
        return False
    return any(pattern.search(command_line) for pattern in RESIDUAL_MONITOR_PATTERNS)


def extract_monitor_port(command_line: object) -> str | None:
    text = compact_command_line(command_line, 4000)
    for pattern in MONITOR_PORT_PATTERNS:
        match = pattern.search(text)
        if match:
            return match.group(1).upper()
    return None


def residual_process_summary(process: dict[str, object]) -> dict[str, object]:
    command_line = process_field(process, "CommandLine", "command_line")
    return {
        "pid": process_id(process),
        "parent_pid": process_field(process, "ParentProcessId", "ppid"),
        "name": process_field(process, "Name", "name"),
        "port": extract_monitor_port(command_line),
        "command_line": compact_command_line(command_line),
    }


def scan_residual_monitor_processes(current_pid: int) -> dict[str, object]:
    """Find monitor-like host processes after capture."""
    if os.name == "nt":
        command = [
            "powershell",
            "-NoProfile",
            "-Command",
            (
                "Get-CimInstance Win32_Process | "
                "Select-Object ProcessId,ParentProcessId,Name,CommandLine | "
                "ConvertTo-Json -Compress"
            ),
        ]
    else:
        command = [
            "ps",
            "-eo",
            "pid=,ppid=,comm=,args=",
        ]

    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=5,
            check=False,
        )
    except Exception as exc:  # noqa: BLE001 - diagnostics must not break capture.
        return {"error": str(exc), "processes": []}

    if result.returncode != 0:
        return {"error": result.stderr.strip() or f"exit_code={result.returncode}", "processes": []}

    try:
        if os.name == "nt":
            raw = result.stdout.strip()
            if not raw:
                process_rows: list[dict[str, object]] = []
            else:
                parsed = json.loads(raw)
                if isinstance(parsed, dict):
                    process_rows = [parsed]
                else:
                    process_rows = [row for row in parsed if isinstance(row, dict)]
        else:
            process_rows = []
            for line in result.stdout.splitlines():
                parts = line.strip().split(maxsplit=3)
                if len(parts) < 4:
                    continue
                pid, ppid, name, command_line = parts
                process_rows.append(
                    {
                        "pid": pid,
                        "ppid": ppid,
                        "name": name,
                        "command_line": command_line,
                    }
                )
    except Exception as exc:  # noqa: BLE001 - diagnostics must not break capture.
        return {"error": str(exc), "processes": []}

    processes = [
        residual_process_summary(row)
        for row in process_rows
        if is_residual_monitor_process(row, current_pid)
    ]
    return {"error": None, "processes": processes}


def terminate_residual_monitor_processes(processes: list[dict[str, object]]) -> list[dict[str, object]]:
    results: list[dict[str, object]] = []
    for process in processes:
        pid = process.get("pid")
        result = {
            "pid": pid,
            "port": process.get("port"),
            "name": process.get("name"),
            "terminated": False,
            "exit_code": None,
            "error": None,
        }
        try:
            pid_text = str(int(pid))
        except (TypeError, ValueError):
            result["error"] = "invalid_pid"
            results.append(result)
            continue

        try:
            if os.name == "nt":
                completed = subprocess.run(
                    ["taskkill.exe", "/T", "/F", "/PID", pid_text],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=5,
                    check=False,
                )
            else:
                completed = subprocess.run(
                    ["kill", "-TERM", pid_text],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=5,
                    check=False,
                )
            result["exit_code"] = completed.returncode
            result["terminated"] = completed.returncode == 0
            if completed.returncode != 0:
                result["error"] = completed.stderr.strip() or completed.stdout.strip()
        except Exception as exc:  # noqa: BLE001 - cleanup diagnostics must not break summary.
            result["error"] = str(exc)
        results.append(result)
    return results


def observe_start_trigger(line: str) -> str | None:
    for name, pattern in OBSERVE_START_PATTERNS:
        if pattern.search(line):
            return name
    return None


def update_panic_detector(line: str, panic_start_seen: bool) -> tuple[bool, bool]:
    """Track ESP panic log structure without deciding firmware health."""
    panic_start_seen = panic_start_seen or bool(PANIC_START_RE.search(line))
    panic_confirmed = panic_start_seen and bool(PANIC_CONFIRM_RE.search(line))
    return panic_start_seen, panic_confirmed


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
    capture_stop_reason = None
    panic_start_seen = False
    panic_log_seen = False
    panic_stop_deadline = None
    stop_kill_sent = False
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
            now = time.monotonic()
            if panic_stop_deadline is not None and now >= panic_stop_deadline:
                capture_stop_reason = "panic_log_seen"
                if not stop_kill_sent:
                    kill_process_tree(process)
                    stop_kill_sent = True
            elif now >= deadline:
                timed_out = True
                timed_out_phase = "observe" if observe_started else "pre_observe"
                capture_stop_reason = (
                    "duration_elapsed" if observe_started else "pre_observe_timeout"
                )
                if not stop_kill_sent:
                    kill_process_tree(process)
                    stop_kill_sent = True
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
            if not panic_log_seen:
                panic_start_seen, panic_confirmed = update_panic_detector(
                    clean_text, panic_start_seen
                )
                if panic_confirmed:
                    panic_log_seen = True
                    capture_stop_reason = "panic_log_seen"
                    panic_stop_deadline = time.monotonic() + PANIC_CAPTURE_GRACE_SECONDS
            if not timed_out and not args.quiet_console:
                safe_console_write(clean_line)

    ended_at = dt.datetime.now(dt.timezone.utc)
    if capture_stop_reason is None:
        capture_stop_reason = "process_exited"
    return {
        "started_at": started_at.isoformat(),
        "ended_at": ended_at.isoformat(),
        "timed_out": timed_out,
        "timed_out_phase": timed_out_phase,
        "capture_stop_reason": capture_stop_reason,
        "panic_log_seen": panic_log_seen,
        "exit_code": process.poll(),
        "observe_started": observe_started,
        "observe_trigger": observe_trigger,
        "observe_started_at": observe_started_at.isoformat() if observe_started_at else None,
        "lines": lines,
    }


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


def write_summary(
    args: argparse.Namespace,
    command: list[str],
    log_path: Path,
    capture: dict[str, object],
    summary_path: Path,
    residual_scan: dict[str, object],
    residual_kill_results: list[dict[str, object]],
    residual_final_scan: dict[str, object],
) -> dict[str, object]:
    lines = list(capture["lines"])
    residual_processes = list(residual_scan.get("processes", []))
    residual_remaining = list(residual_final_scan.get("processes", []))
    residual_killed_count = sum(1 for item in residual_kill_results if item.get("terminated"))
    summary = {
        "tool": "agent_serial_monitor",
        "status": "captured",
        "action": args.action,
        "port": args.port,
        "baud": args.baud,
        "duration_seconds": args.duration_seconds,
        "flash_timeout_seconds": args.flash_timeout_seconds,
        "quiet_console": args.quiet_console,
        "reset_on_start": not args.no_reset,
        "timed_out": capture["timed_out"],
        "timed_out_phase": capture["timed_out_phase"],
        "capture_stop_reason": capture["capture_stop_reason"],
        "panic_log_seen": capture["panic_log_seen"],
        "exit_code": capture["exit_code"],
        "observe_started": capture["observe_started"],
        "observe_trigger": capture["observe_trigger"],
        "observe_started_at": capture["observe_started_at"],
        "flash_completed": flash_completed(lines),
        "observation_complete": observation_complete(capture),
        "line_count": len(lines),
        "residual_monitor_count": len(residual_processes),
        "residual_monitor_processes": residual_processes,
        "residual_monitor_scan_error": residual_scan.get("error"),
        "residual_monitor_kill_results": residual_kill_results,
        "residual_monitor_killed_count": residual_killed_count,
        "residual_monitor_remaining_count": len(residual_remaining),
        "residual_monitor_remaining_processes": residual_remaining,
        "residual_monitor_final_scan_error": residual_final_scan.get("error"),
        "started_at": capture["started_at"],
        "ended_at": capture["ended_at"],
        "command": command,
        "log_path": str(log_path),
        "summary_path": str(summary_path),
    }
    summary_path.write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return summary


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
    args = parser.parse_args()
    if args.duration_seconds <= 0:
        parser.error("--duration-seconds must be > 0")
    if args.flash_timeout_seconds <= 0:
        parser.error("--flash-timeout-seconds must be > 0")
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
    residual_scan = scan_residual_monitor_processes(os.getpid())
    residual_processes = list(residual_scan.get("processes", []))
    residual_kill_results = terminate_residual_monitor_processes(residual_processes)
    if residual_processes:
        time.sleep(0.5)
    residual_final_scan = scan_residual_monitor_processes(os.getpid())
    summary = write_summary(
        args,
        command,
        log_path,
        capture,
        summary_path,
        residual_scan,
        residual_kill_results,
        residual_final_scan,
    )

    print()
    print(f"AGENT_SERIAL_MONITOR_STATUS={summary['status']}")
    print(f"AGENT_SERIAL_MONITOR_LOG={log_path}")
    print(f"AGENT_SERIAL_MONITOR_SUMMARY={summary_path}")
    print(f"AGENT_SERIAL_MONITOR_CAPTURE_STOP_REASON={summary['capture_stop_reason']}")
    print(f"AGENT_SERIAL_MONITOR_PANIC_LOG_SEEN={int(bool(summary['panic_log_seen']))}")
    print(f"AGENT_SERIAL_MONITOR_RESIDUAL_COUNT={summary['residual_monitor_count']}")
    print(f"AGENT_SERIAL_MONITOR_RESIDUAL_KILLED_COUNT={summary['residual_monitor_killed_count']}")
    print(f"AGENT_SERIAL_MONITOR_RESIDUAL_REMAINING_COUNT={summary['residual_monitor_remaining_count']}")
    if summary["residual_monitor_scan_error"]:
        print(f"AGENT_SERIAL_MONITOR_RESIDUAL_SCAN_ERROR={summary['residual_monitor_scan_error']}")
    for process in summary["residual_monitor_processes"]:
        print(
            "AGENT_SERIAL_MONITOR_RESIDUAL="
            f"pid={process['pid']} port={process['port'] or 'unknown'} name={process['name']}"
        )
    for result in summary["residual_monitor_kill_results"]:
        print(
            "AGENT_SERIAL_MONITOR_RESIDUAL_KILL="
            f"pid={result['pid']} port={result['port'] or 'unknown'} "
            f"terminated={int(bool(result['terminated']))}"
        )
    for process in summary["residual_monitor_remaining_processes"]:
        print(
            "AGENT_SERIAL_MONITOR_RESIDUAL_REMAINING="
            f"pid={process['pid']} port={process['port'] or 'unknown'} name={process['name']}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
