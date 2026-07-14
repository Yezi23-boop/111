#!/usr/bin/env python3
"""Lightweight layering boundary checker for the current ESP32-S3 repo."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from _stdio import configure_utf8_stdio

configure_utf8_stdio()


@dataclass(frozen=True)
class BoundaryRule:
    scope: str
    pattern: re.Pattern[str]
    reason: str


@dataclass(frozen=True)
class KnownException:
    path: str
    pattern: re.Pattern[str]
    reason: str


RULES: tuple[BoundaryRule, ...] = (
    BoundaryRule("main/ui/generated", re.compile(r"#include\s*[<\"](?:services|features)/"), "Generated UI must not depend directly on feature/service owners."),
    BoundaryRule("main/ui/generated", re.compile(r"\besp_http_client_\w+|\bhttpd_\w+|\b#include\s*[<\"]esp_http"), "Generated UI must not own HTTP work."),
    BoundaryRule("main/ui/generated", re.compile(r"\besp_wifi_\w+|\bwifi_\w+|\binclude\s*[<\"](?:esp_wifi|wifi_control)"), "Generated UI must not own Wi-Fi work."),
    BoundaryRule("main/ui", re.compile(r"\besp_wifi_\w+|\b#include\s*[<\"]esp_wifi\.h[>\"]"), "UI should go through network_service/network_manager, not esp_wifi directly."),
    BoundaryRule("main/ui", re.compile(r"\bwifi_prov_mgr_\w+|\b#include\s*[<\"]wifi_provisioning/"), "UI should not own provisioning manager calls."),
    BoundaryRule("main/ui", re.compile(r"\bhttpd_\w+|\b#include\s*[<\"]esp_http_server\.h[>\"]"), "UI should not own HTTP server/provisioning portal details."),
    BoundaryRule("main/ui", re.compile(r"\besp_lcd_\w+|\b#include\s*[<\"]esp_lcd_"), "UI should use lvgl_port/panel owner instead of raw LCD SDK calls."),
    BoundaryRule("main/ui", re.compile(r"\bi2s_\w+|\b#include\s*[<\"]driver/i2s"), "UI should not touch raw I2S/audio transport."),
    BoundaryRule("main/ui", re.compile(r"\baxp2101_\w+|\b#include\s*[<\"]axp2101"), "UI should read power snapshots through power_service/board_power."),
    BoundaryRule("main/ui", re.compile(r"\bco5300_panel_\w+|\b#include\s*[<\"]co5300_panel"), "UI should not bypass the display owner."),
    BoundaryRule("main/ui", re.compile(r"\btouch_ft5x06_\w+|\b#include\s*[<\"]touch_ft5x06"), "UI should not bypass the touch owner."),
    BoundaryRule("main/features", re.compile(r"\bwifi_prov_mgr_\w+|\bhttpd_\w+|\b#include\s*[<\"]esp_http_server\.h[>\"]"), "Feature code should route provisioning/network server work through service/manager owners."),
    BoundaryRule("main/features", re.compile(r"\besp_lcd_\w+|\bco5300_panel_\w+|\btouch_ft5x06_\w+"), "Feature code should not depend on raw display/touch drivers."),
    BoundaryRule("main/services", re.compile(r"\besp_lcd_\w+|\bco5300_panel_\w+|\btouch_ft5x06_\w+"), "Service code should not directly own display/touch driver details."),
    BoundaryRule("main/services", re.compile(r"#include\s*[<\"](?:qmi8658c|axp2101|ds2413|co5300_panel|touch_ft5x06|i2c_manager)\.h[>\"]"), "Service code should depend on board/domain adapters instead of raw device drivers."),
)

KNOWN_EXCEPTIONS: tuple[KnownException, ...] = (
    KnownException(
        "main/ui/ui_refresh_policy.c",
        re.compile(r"\bco5300_panel_set_brightness_percent\s*\("),
        "Documented current low-power brightness path; keep visible but do not count as a new warning.",
    ),
    KnownException(
        "main/services/power/wakeup_evidence_service.c",
        re.compile(r"#include\s*[<\"]axp2101\.h[>\"]"),
        "Existing PMIC wake-evidence path; directory reorganization does not change its owner contract.",
    ),
)

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp"}
SKIP_PARTS = {"build", "managed_components"}
I2C_BUS_HANDLE_PATTERN = re.compile(r"\bi2c_manager_get_bus_handle\s*\(")
I2C_BUS_HANDLE_ALLOWLIST = {
    "components/audio_codec/audio_codec.c",
    "components/axp2101/axp2101.c",
    "components/i2c_manager/include/i2c_manager.h",
    "components/i2c_manager/i2c_manager.c",
    "components/pcf85063atl/pcf85063atl.c",
    "components/qmi8658c/qmi8658c.c",
    "components/touch_ft5x06/touch_ft5x06.c",
}


def resolve_project_root(project_root_arg: str | None) -> Path:
    script_path = Path(__file__).resolve()
    if project_root_arg:
        return Path(project_root_arg).resolve()
    return script_path.parents[2]


def should_scan(path: Path, project_root: Path) -> bool:
    if path.suffix.lower() not in SOURCE_SUFFIXES:
        return False
    rel_parts = set(path.relative_to(project_root).parts)
    return not bool(rel_parts & SKIP_PARTS)


def collect_files(project_root: Path) -> list[Path]:
    files: list[Path] = []
    for root_name in ("main/ui", "main/features", "main/services"):
        root = project_root / root_name
        if not root.exists():
            continue
        files.extend(path for path in sorted(root.rglob("*")) if path.is_file() and should_scan(path, project_root))
    return files


def collect_i2c_bus_handle_warnings(project_root: Path) -> list[dict[str, Any]]:
    """限制 raw I2C bus handle 只在已登记的 device adapter 内使用。"""
    warnings: list[dict[str, Any]] = []
    roots = (project_root / "main", project_root / "components")
    for root in roots:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file() or not should_scan(path, project_root):
                continue
            rel_path = path.relative_to(project_root).as_posix()
            for line_no, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(),
                start=1,
            ):
                if not I2C_BUS_HANDLE_PATTERN.search(line):
                    continue
                if rel_path in I2C_BUS_HANDLE_ALLOWLIST:
                    break
                warnings.append(
                    {
                        "path": rel_path,
                        "line": line_no,
                        "scope": "raw-i2c-handle",
                        "reason": "Raw I2C bus handle is restricted to registered device adapters.",
                        "text": line.strip()[:160],
                    }
                )
                break
    return warnings


def collect_memory_watch_ownership_warnings(project_root: Path) -> list[dict[str, Any]]:
    """确保 Memory Watch runtime 文件不会重新散落到 services 根目录。"""
    warnings: list[dict[str, Any]] = []
    services_root = project_root / "main" / "services"
    if not services_root.exists():
        return warnings

    for path in sorted(services_root.rglob("*")):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        if not (path.name.startswith("memory_watch_") or path.name.startswith("watch_endpoint_service")):
            continue
        rel_path = path.relative_to(project_root).as_posix()
        if rel_path.startswith("main/services/memory_watch/"):
            continue
        warnings.append(
            {
                "path": rel_path,
                "line": 1,
                "scope": "memory-watch-owner",
                "reason": "Memory Watch runtime files belong under main/services/memory_watch/.",
                "text": path.name,
            }
        )
    return warnings


def matching_rules(rel_path: str) -> list[BoundaryRule]:
    normalized = rel_path.replace("\\", "/")
    return [rule for rule in RULES if normalized.startswith(rule.scope + "/")]


def matching_exception(rel_path: str, line: str) -> KnownException | None:
    normalized = rel_path.replace("\\", "/")
    for exception in KNOWN_EXCEPTIONS:
        if normalized == exception.path and exception.pattern.search(line):
            return exception
    return None


def scan_file(path: Path, project_root: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    rel_path = path.relative_to(project_root).as_posix()
    rules = matching_rules(rel_path)
    if not rules:
        return [], []

    warnings: list[dict[str, Any]] = []
    exceptions: list[dict[str, Any]] = []
    for line_no, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith("//"):
            continue
        for rule in rules:
            if rule.pattern.search(line):
                exception = matching_exception(rel_path, line)
                if exception:
                    exceptions.append(
                        {
                            "path": rel_path,
                            "line": line_no,
                            "scope": rule.scope,
                            "reason": exception.reason,
                            "text": stripped[:160],
                        }
                    )
                    continue
                warnings.append(
                    {
                        "path": rel_path,
                        "line": line_no,
                        "scope": rule.scope,
                        "reason": rule.reason,
                        "text": stripped[:160],
                    }
                )
    return warnings, exceptions


def run_check(project_root: Path) -> dict[str, Any]:
    files = collect_files(project_root)
    warnings: list[dict[str, Any]] = []
    exceptions: list[dict[str, Any]] = []
    for path in files:
        file_warnings, file_exceptions = scan_file(path, project_root)
        warnings.extend(file_warnings)
        exceptions.extend(file_exceptions)
    warnings.extend(collect_i2c_bus_handle_warnings(project_root))
    warnings.extend(collect_memory_watch_ownership_warnings(project_root))
    return {
        "checked_files": len(files),
        "warnings": warnings,
        "known_exceptions": exceptions,
        "warning_count": len(warnings),
        "known_exception_count": len(exceptions),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Check lightweight layering boundary hints.")
    parser.add_argument("--project-root", default=None, help="Project root. Defaults to repository root.")
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    parser.add_argument("--verbose", action="store_true", help="Print warning details.")
    parser.add_argument("--strict", action="store_true", help="Return non-zero when warnings are found.")
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root)
    result = run_check(project_root)

    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print(
            f"checked_files={result['checked_files']} "
            f"warning_count={result['warning_count']} "
            f"known_exception_count={result['known_exception_count']}"
        )
        if args.verbose and result["warnings"]:
            for item in result["warnings"]:
                print(f"- {item['path']}:{item['line']} [{item['scope']}] {item['reason']}")
                print(f"  {item['text']}")
        if args.verbose and result["known_exceptions"]:
            print("known exceptions:")
            for item in result["known_exceptions"]:
                print(f"- {item['path']}:{item['line']} [{item['scope']}] {item['reason']}")
                print(f"  {item['text']}")

    if args.strict and result["warning_count"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
