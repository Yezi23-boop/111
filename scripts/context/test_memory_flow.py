#!/usr/bin/env python3
"""Validate the repo's Agent Context Memory write/retrieve/garden loop."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from _stdio import configure_utf8_stdio

configure_utf8_stdio()


def resolve_project_root(project_root_arg: str | None) -> Path:
    script_path = Path(__file__).resolve()
    if project_root_arg:
        return Path(project_root_arg).resolve()
    return script_path.parents[2]


def run_command(
    project_root: Path,
    command: list[str],
    *,
    expect_success: bool = True,
) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command,
        cwd=project_root,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if expect_success and completed.returncode != 0:
        raise AssertionError(
            f"命令失败({completed.returncode}): {' '.join(command)}\n{completed.stdout}"
        )
    if not expect_success and completed.returncode == 0:
        raise AssertionError(
            f"命令应失败但成功: {' '.join(command)}\n{completed.stdout}"
        )
    return completed


def parse_frontmatter(path: Path) -> dict[str, str]:
    raw = path.read_text(encoding="utf-8")
    if not raw.startswith("---\n"):
        raise AssertionError(f"{path} 缺少 frontmatter")
    end = raw.find("\n---\n", 4)
    if end == -1:
        raise AssertionError(f"{path} frontmatter 未闭合")
    meta: dict[str, str] = {}
    for line in raw[4:end].splitlines():
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        meta[key.strip()] = value.strip().strip("'\"")
    return meta


def assert_contains(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"{label} 未包含 `{needle}`\n{text[:2000]}")


def check_entry_triggers(project_root: Path) -> None:
    required = {
        "AGENTS.md": "validate_context.py --level light",
        "docs/context/INDEX.agent.md": "validate_context.py --level light",
        "docs/context/knowledge/project/project-profile.md": "validate_context.py --level light",
    }
    for rel_path, expected in required.items():
        path = project_root / rel_path
        if not path.exists():
            raise AssertionError(f"缺少首读/触发入口: {rel_path}")
        assert_contains(path.read_text(encoding="utf-8"), expected, rel_path)


def check_attempt_write_gate(project_root: Path) -> None:
    python = sys.executable
    with tempfile.TemporaryDirectory(prefix="context-memory-flow-") as tmp_text:
        tmp_root = Path(tmp_text)
        denied = run_command(
            project_root,
            [
                python,
                "scripts/context/log_attempt.py",
                "--project-root",
                str(tmp_root),
                "--runs-root",
                "runs",
                "--title",
                "write gate denied",
                "--status",
                "success",
                "--changed",
                "docs/context/runs",
            ],
            expect_success=False,
        )
        assert_contains(denied.stdout, "--record-because", "write gate")

        accepted = run_command(
            project_root,
            [
                python,
                "scripts/context/log_attempt.py",
                "--project-root",
                str(tmp_root),
                "--runs-root",
                "runs",
                "--title",
                "schema success",
                "--status",
                "success",
                "--record-because",
                "evidence",
                "--changed",
                "docs/context/runs",
                "--tried",
                "temp schema generation",
                "--evidence",
                "frontmatter parsed",
            ],
        )
        assert_contains(accepted.stdout, "已记录 attempt", "attempt write")
        generated = next((tmp_root / "runs").glob("*attempt-schema-success*.md"))
        meta = parse_frontmatter(generated)
        expected_meta = {
            "status": "active",
            "result": "success",
            "memory_type": "episodic",
            "evidence_level": "observed",
            "record_reasons": "evidence",
        }
        for key, value in expected_meta.items():
            if meta.get(key) != value:
                raise AssertionError(f"{generated} 的 {key}={meta.get(key)!r}，期望 {value!r}")

        forced = run_command(
            project_root,
            [
                python,
                "scripts/context/log_attempt.py",
                "--project-root",
                str(tmp_root),
                "--runs-root",
                "runs",
                "--title",
                "force schema",
                "--status",
                "partial",
                "--force",
                "--changed",
                "docs/context/runs",
            ],
        )
        assert_contains(forced.stdout, "已记录 attempt", "force write")
        forced_file = next((tmp_root / "runs").glob("*attempt-force-schema*.md"))
        forced_meta = parse_frontmatter(forced_file)
        if forced_meta.get("record_reasons") != "force":
            raise AssertionError(f"{forced_file} 的 record_reasons 不可审计")
        if not forced_meta.get("force_reason"):
            raise AssertionError(f"{forced_file} 缺少 force_reason")


def check_runs_retrieval(project_root: Path) -> None:
    completed = run_command(
        project_root,
        [
            sys.executable,
            "scripts/context/query.py",
            "--scope",
            "runs",
            "--q",
            "agent 做过什么 修改 尝试 避免重复",
            "--top",
            "8",
        ],
    )
    assert_contains(completed.stdout, "docs/context/runs/README.md", "runs retrieval")
    assert_contains(completed.stdout, "docs/context/runs/attempt-template.md", "runs retrieval")


def check_light_trigger_flow(project_root: Path) -> None:
    completed = run_command(
        project_root,
        [
            sys.executable,
            "scripts/context/validate_context.py",
            "--level",
            "light",
            "--q",
            "agent 做过什么 修改 尝试 避免重复",
            "--brief",
        ],
    )
    assert_contains(completed.stdout, "query.py", "light flow")
    assert_contains(completed.stdout, "pack_context.py", "light flow")
    assert_contains(completed.stdout, "--no-write", "light flow no-write")
    assert_contains(completed.stdout, "输出文件: 未写入 (--no-write)", "light flow no-write")


def check_garden_and_eval(project_root: Path) -> None:
    garden = run_command(
        project_root,
        [sys.executable, "scripts/context/garden.py", "--summary-json"],
    )
    payload = json.loads(garden.stdout)
    warning_count = payload.get("warning_count")
    if not isinstance(warning_count, int) or warning_count < 0:
        raise AssertionError(f"garden warning_count 非法:\n{garden.stdout}")
    counts = payload.get("candidate_counts", {})
    for key in (
        "stale_candidates",
        "promotion_candidates",
        "archive_candidates",
        "broken_owner_refs",
        "low_value_run_candidates",
    ):
        if key not in counts:
            raise AssertionError(f"garden candidate_counts 缺少 {key}")

    eval_result = run_command(project_root, [sys.executable, "scripts/context/eval_query.py"])
    assert_contains(eval_result.stdout, "失败: 0", "eval_query")


def check_index_freshness(project_root: Path) -> None:
    index_path = project_root / "docs/context/index/context-index.json"
    if not index_path.exists():
        raise AssertionError("缺少 docs/context/index/context-index.json")

    payload = json.loads(index_path.read_text(encoding="utf-8"))
    indexed_paths = {
        str(item.get("path", "")).replace("\\", "/")
        for item in payload.get("documents", [])
        if str(item.get("path", "")).strip()
    }
    actual_paths = {
        path.relative_to(project_root).as_posix()
        for path in (project_root / "docs/context").rglob("*.md")
        if not any(part.startswith(".") for part in path.parts)
    }
    missing = sorted(actual_paths - indexed_paths)
    extra = sorted(indexed_paths - actual_paths)
    if missing or extra:
        details = [
            "context index 与当前 Markdown 文件集合不同步。",
            f"missing_in_index={missing[:10]}",
            f"extra_in_index={extra[:10]}",
            "请先运行 `uv run python scripts/context/build_index.py`。",
        ]
        raise AssertionError("\n".join(details))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="自检 Agent Context Memory 的触发、写入、检索、园艺和回归闭环。"
    )
    parser.add_argument(
        "--project-root",
        default=None,
        help="项目根目录。默认根据脚本路径自动识别。",
    )
    parser.add_argument(
        "--skip-light-flow",
        action="store_true",
        help="跳过 validate_context light --brief 流程测试。",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    project_root = resolve_project_root(args.project_root)

    checks = [
        ("entry-triggers", lambda: check_entry_triggers(project_root)),
        ("attempt-write-gate", lambda: check_attempt_write_gate(project_root)),
        ("runs-retrieval", lambda: check_runs_retrieval(project_root)),
        ("index-freshness", lambda: check_index_freshness(project_root)),
        ("garden-and-eval", lambda: check_garden_and_eval(project_root)),
    ]
    if not args.skip_light_flow:
        checks.insert(3, ("light-trigger-flow", lambda: check_light_trigger_flow(project_root)))

    for name, check in checks:
        print(f"[RUN] {name}", flush=True)
        check()
        print(f"[PASS] {name}", flush=True)

    print("Agent Context Memory flow: PASS", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
