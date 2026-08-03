#!/usr/bin/env python3
"""Agent-friendly ESP32 serial capture tool (pySerial direct-read).

Owns the serial port directly via pySerial instead of spawning
`idf.py monitor` / PowerShell / export.ps1.  This removes the three failure
modes of the previous design:

1. Long captures no longer depend on the caller's Bash window: agent starts
   a capture with `--action start`, gets a PID back, then polls the log file.
   The capture process self-terminates at `--duration-seconds`.
2. A stale monitor can no longer steal the port: the capture process opens the
   COM port exclusively (`exclusive=True`), and a per-port lock file detects a
   still-running capture before trying to open it.  Two capture instances on
   the same port fail fast instead of fighting over the handle.
3. `export.ps1`/MSYSTEM issues are gone: no ESP-IDF environment is needed to
   read the console output, only pySerial (ships with the IDF venv).
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError as exc:  # pragma: no cover - environment check
    print(
        "ERROR: pySerial is required. Run with the ESP-IDF venv python, e.g.:\n"
        "  D:\\esp-idf\\5.3\\tools.espressif\\python_env\\idf5.5_py3.11_env\\Scripts\\python.exe "
        "scripts/board/agent_serial_monitor.py ...",
        file=sys.stderr,
    )
    raise SystemExit(2) from exc


DEFAULT_BAUD = 115200
DEFAULT_FLASH_BAUD = 921600
DEFAULT_IDF_EXPORT_PS1 = r"D:\esp-idf\v5.5.3\esp-idf\export.ps1"
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

# USB-Serial/JTAG keeps the COM handle busy briefly after a process exit;
# keep retrying for a while before giving up on the exclusive open.
PORT_OPEN_ATTEMPTS = 30
PORT_OPEN_RETRY_SECONDS = 2.0
STATE_POLL_SECONDS = 0.2
SERIAL_READ_TIMEOUT_SECONDS = 0.2


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


def build_flash_command(args: argparse.Namespace, repo_root: Path) -> list[str]:
    """Build the `idf.py app-flash` invocation, flashed at the fast baud."""
    monitor_options = " --no-reset" if args.no_reset else ""
    powershell = (
        "[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; "
        "$OutputEncoding=[System.Text.Encoding]::UTF8; "
        # Git Bash injects MSYSTEM into child processes; idf_tools.py rejects
        # MSYS/Mingw environments, so drop it before sourcing export.ps1.
        "Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue; "
        f"& '{args.idf_export_ps1}'; "
        f"Set-Location '{repo_root}'; "
        f"idf.py -p {args.port} -b {args.flash_baud} app-flash{monitor_options}"
    )
    return [
        "powershell",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        powershell,
    ]


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


def acquire_port_lock(port: str, log_dir: Path) -> Path:
    """Create a per-port lock file so two captures never open the same COM port.

    The lock records the PID of the capture that owns it; a lock whose owner is
    no longer alive is stale and is reclaimed.  A live lock means a capture is
    already running on this port and we fail fast.
    """
    log_dir.mkdir(parents=True, exist_ok=True)
    lock_path = log_dir / f"{port}.lock"
    for _ in range(PORT_OPEN_ATTEMPTS):
        if lock_path.exists():
            try:
                owner = int(lock_path.read_text(encoding="utf-8").strip() or "0")
            except (OSError, ValueError):
                owner = 0
            if owner > 0 and process_alive(owner):
                raise RuntimeError(
                    f"port {port} is already captured by pid {owner}; "
                    f"lock file: {lock_path}"
                )
            # Stale lock (owner gone) or unreadable content: remove and retry.
            try:
                lock_path.unlink()
            except OSError:
                pass
        try:
            lock_path.write_text(str(os.getpid()), encoding="utf-8")
            return lock_path
        except OSError:
            time.sleep(PORT_OPEN_RETRY_SECONDS)
    raise RuntimeError(f"could not acquire port lock: {lock_path}")


def process_alive(pid: int) -> bool:
    if os.name == "nt":
        try:
            result = subprocess.run(
                ["tasklist.exe", "/FI", f"PID eq {pid}", "/NH"],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                timeout=3,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            return True  # 无法验证时保守视为存活。
        # 匹配行形如 "explorer.exe  8128 Console ..."；无匹配时 tasklist 输出
        # "没有运行的任务..." 之类提示且不含该 PID（中英文系统均如此），且
        # 返回码恒为 0，不能用 returncode 区分。用非数字边界避免 9999999
        # 误匹配 99999999。
        output = result.stdout.decode(errors="replace")
        return re.search(rf"(?<!\d){pid}(?!\d)", output) is not None
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def read_serial_lines(ser: serial.Serial) -> list[bytes]:
    """Drain whatever is buffered on the serial port right now."""
    lines: list[bytes] = []
    try:
        while True:
            line = ser.readline()
            if not line:
                break
            lines.append(line)
    except serial.SerialException:
        pass
    return lines


def serial_open_retry(args: argparse.Namespace, opened_at: float) -> serial.Serial:
    """Open the COM port exclusively, retrying until the deadline.

    Returns the open handle; raises if the port stays unavailable until
    `opened_at + duration` (flash runs first when action is app-flash-monitor,
    so the port is expected to be busy for the flashing phase).
    """
    last_error: Exception | None = None
    while True:
        if time.monotonic() >= opened_at + args.duration_seconds:
            break
        try:
            return serial.Serial(
                args.port,
                baudrate=args.baud,
                timeout=SERIAL_READ_TIMEOUT_SECONDS,
                exclusive=True,
            )
        except serial.SerialException as exc:
            last_error = exc
        time.sleep(PORT_OPEN_RETRY_SECONDS)
    raise RuntimeError(
        f"could not open {args.port} exclusively after "
        f"{args.duration_seconds}s: {last_error}"
    )


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


def find_pending_capture(log_dir: Path) -> tuple[Path, dict[str, object]] | None:
    """Return (state file, state) of a started-but-still-running capture."""
    for state_path in sorted(log_dir.glob("*-capture.state.json")):
        try:
            state = json.loads(state_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        pid = int(state.get("pid") or 0)
        if pid > 0 and process_alive(pid):
            return state_path, state
    return None


def print_capture_status(state_path: Path, state: dict[str, object]) -> int:
    log_path = state.get("log_path")
    summary_path = state.get("summary_path")
    print(f"AGENT_SERIAL_MONITOR_STATUS=running")
    print(f"AGENT_SERIAL_MONITOR_PID={state.get('pid')}")
    print(f"AGENT_SERIAL_MONITOR_STATE={state_path}")
    print(f"AGENT_SERIAL_MONITOR_LOG={log_path}")
    print(f"AGENT_SERIAL_MONITOR_SUMMARY={summary_path}")
    return 0


def run_capture(
    ser: serial.Serial,
    log_path: Path,
    args: argparse.Namespace,
    started_at: dt.datetime,
    lock_path: Path,
) -> dict[str, object]:
    """Capture serial output until deadline/panic, returning capture facts."""
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
    lines: list[str] = []

    log_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with log_path.open("w", encoding="utf-8", newline="\n") as log_file:
            while True:
                now = time.monotonic()
                if panic_stop_deadline is not None and now >= panic_stop_deadline:
                    capture_stop_reason = "panic_log_seen"
                    break
                if now >= deadline:
                    timed_out = True
                    timed_out_phase = "observe" if observe_started else "pre_observe"
                    capture_stop_reason = (
                        "duration_elapsed" if observe_started else "pre_observe_timeout"
                    )
                    break
                for raw_line in read_serial_lines(ser):
                    clean_line = strip_ansi(
                        raw_line.decode("utf-8", errors="replace")
                    )
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
                            panic_stop_deadline = (
                                time.monotonic() + PANIC_CAPTURE_GRACE_SECONDS
                            )
                    if not timed_out and not args.quiet_console:
                        safe_console_write(clean_line)
    finally:
        lock_path.unlink(missing_ok=True)

    ended_at = dt.datetime.now(dt.timezone.utc)
    if capture_stop_reason is None:
        capture_stop_reason = "port_closed"
    return {
        "started_at": started_at.isoformat(),
        "ended_at": ended_at.isoformat(),
        "timed_out": timed_out,
        "timed_out_phase": timed_out_phase,
        "capture_stop_reason": capture_stop_reason,
        "panic_log_seen": panic_log_seen,
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
    flash_command: list[str] | None,
    log_path: Path,
    capture: dict[str, object],
    summary_path: Path,
) -> dict[str, object]:
    lines = list(capture["lines"])
    summary = {
        "tool": "agent_serial_monitor",
        "status": "captured",
        "action": args.action,
        "port": args.port,
        "baud": args.baud,
        "flash_baud": args.flash_baud,
        "duration_seconds": args.duration_seconds,
        "flash_timeout_seconds": args.flash_timeout_seconds,
        "quiet_console": args.quiet_console,
        "reset_on_start": not args.no_reset,
        "timed_out": capture["timed_out"],
        "timed_out_phase": capture["timed_out_phase"],
        "capture_stop_reason": capture["capture_stop_reason"],
        "panic_log_seen": capture["panic_log_seen"],
        "observe_started": capture["observe_started"],
        "observe_trigger": capture["observe_trigger"],
        "observe_started_at": capture["observe_started_at"],
        "flash_completed": flash_completed(lines),
        "observation_complete": observation_complete(capture),
        "line_count": len(lines),
        "started_at": capture["started_at"],
        "ended_at": capture["ended_at"],
        "flash_command": flash_command,
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
        description=(
            "Bounded ESP32 serial capture for agents (pySerial direct-read). "
            "Runs `idf.py app-flash` first when action is app-flash-monitor, "
            "then opens the port exclusively and captures until the duration "
            "elapses or a panic is detected."
        ),
    )
    parser.add_argument("--port", required=True, help="Serial port, for example COM7.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument(
        "--flash-baud",
        type=int,
        default=DEFAULT_FLASH_BAUD,
        help=(
            "Baud rate for the app-flash stage. ESP32-S3 USB-Serial/JTAG "
            "reliably supports 921600, which keeps a 10+ MB app image well "
            "inside the flash timeout."
        ),
    )
    parser.add_argument(
        "--action",
        choices=("monitor", "app-flash-monitor", "start", "capture"),
        default="monitor",
        help=(
            "Capture only, flash the app first and then capture, or query the "
            "running capture (start/capture report state without opening the port)."
        ),
    )
    parser.add_argument(
        "--duration-seconds",
        type=int,
        default=60,
        help=(
            "Maximum capture time. For app-flash-monitor this is also the "
            "total flash+wait budget, so keep it comfortably above the flash "
            "time (a 10+ MB image takes ~80 s at 921600 baud)."
        ),
    )
    parser.add_argument(
        "--flash-timeout-seconds",
        type=int,
        default=300,
        help=(
            "Maximum time allowed for the app-flash stage to finish before "
            "capture starts. The observe window itself is capped by "
            "--duration-seconds."
        ),
    )
    parser.add_argument("--tag", default="serial", help="File-name tag for logs.")
    parser.add_argument("--log-dir", default="board_logs")
    parser.add_argument("--idf-export-ps1", default=DEFAULT_IDF_EXPORT_PS1)
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="Do not touch DTR/RTS on the port (idf.py app-flash still resets "
        "the board at the end of flashing).",
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
    state_path = log_dir / f"{stamp}-{safe_tag}.capture.state.json"

    if args.action == "start":
        pending = find_pending_capture(log_dir)
        if pending is None:
            print("AGENT_SERIAL_MONITOR_STATUS=not_running")
            print("AGENT_SERIAL_MONITOR_NOTE=no capture is running; use --action capture to start one")
            return 0
        state_path, state = pending
        return print_capture_status(state_path, state)

    if args.action == "capture":
        pending = find_pending_capture(log_dir)
        if pending is not None:
            existing, state = pending
            print(f"AGENT_SERIAL_MONITOR_STATUS=already_running")
            print(f"AGENT_SERIAL_MONITOR_PID={state.get('pid')}")
            print(f"AGENT_SERIAL_MONITOR_LOG={state.get('log_path')}")
            return 0
        print("AGENT_SERIAL_MONITOR_STATUS=not_running")
        print("AGENT_SERIAL_MONITOR_NOTE=no capture is running; use --action start to start one")
        return 0

    # Build the port lock first so a second capture on the same port fails
    # before the flash (which would otherwise already be taking place).
    try:
        lock_path = acquire_port_lock(args.port, log_dir)
    except RuntimeError as exc:
        print("AGENT_SERIAL_MONITOR_STATUS=busy")
        print(f"AGENT_SERIAL_MONITOR_NOTE={exc}")
        return 1

    started_at = dt.datetime.now(dt.timezone.utc)

    flash_process: subprocess.Popen[str] | None = None
    flash_command: list[str] | None = None
    if args.action == "app-flash-monitor":
        flash_command = build_flash_command(args, repo_root)
        flash_process = subprocess.Popen(
            flash_command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        flash_deadline = time.monotonic() + args.flash_timeout_seconds
        while flash_process.poll() is None and time.monotonic() < flash_deadline:
            time.sleep(STATE_POLL_SECONDS)
        if flash_process.poll() is None:
            kill_process_tree(flash_process)
            lock_path.unlink(missing_ok=True)
            print("AGENT_SERIAL_MONITOR_STATUS=flash_timeout")
            print(
                f"AGENT_SERIAL_MONITOR_NOTE=app-flash did not finish within "
                f"{args.flash_timeout_seconds}s; killed"
            )
            return 1

    try:
        ser = serial_open_retry(args, time.monotonic())
    except RuntimeError as exc:
        if flash_process is not None and flash_process.poll() is not None:
            exit_code = flash_process.returncode
            print("AGENT_SERIAL_MONITOR_STATUS=flash_failed")
            print(f"AGENT_SERIAL_MONITOR_FLASH_EXIT_CODE={exit_code}")
            print(f"AGENT_SERIAL_MONITOR_NOTE={exc}")
        else:
            print("AGENT_SERIAL_MONITOR_STATUS=port_open_failed")
            print(f"AGENT_SERIAL_MONITOR_NOTE={exc}")
        lock_path.unlink(missing_ok=True)
        return 1

    capture = run_capture(ser, log_path, args, started_at, lock_path)
    ser.close()

    summary = write_summary(args, flash_command, log_path, capture, summary_path)

    print()
    print(f"AGENT_SERIAL_MONITOR_STATUS={summary['status']}")
    print(f"AGENT_SERIAL_MONITOR_LOG={log_path}")
    print(f"AGENT_SERIAL_MONITOR_SUMMARY={summary_path}")
    print(f"AGENT_SERIAL_MONITOR_CAPTURE_STOP_REASON={summary['capture_stop_reason']}")
    print(f"AGENT_SERIAL_MONITOR_PANIC_LOG_SEEN={int(bool(summary['panic_log_seen']))}")
    if summary["flash_completed"] is not None:
        print(f"AGENT_SERIAL_MONITOR_FLASH_COMPLETED={int(bool(summary['flash_completed']))}")
    print(f"AGENT_SERIAL_MONITOR_OBSERVATION_COMPLETE={int(bool(summary['observation_complete']))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
