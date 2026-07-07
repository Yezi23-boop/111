#!/usr/bin/env python3
"""Run the right amount of context validation for the current change."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from _stdio import configure_utf8_stdio

configure_utf8_stdio()


LEVEL_HELP = {
    "light": "普通任务低 token 检索：query runs + query plans + query mixed，可选 brief pack。",
    "standard": "只改 context 文档：build_index + check。",
    "routing": "改入口或检索基准：build_index + check + eval_query。",
    "full": "改 scripts/context 或记忆机制：build_index + check + garden + eval_query。",
}


def resolve_project_root(project_root_arg: str | None) -> Path:
    script_path = Path(__file__).resolve()
    if project_root_arg:
        return Path(project_root_arg).resolve()
    return script_path.parents[2]


def script_path(project_root: Path, name: str) -> Path:
    return project_root / "scripts" / "context" / name


def run_command(
    project_root: Path,
    command: list[str],
    *,
    dry_run: bool,
    continue_on_error: bool,
) -> int:
    printable = " ".join(command)
    print(f"> {printable}", flush=True)
    if dry_run:
        return 0

    completed = subprocess.run(command, cwd=project_root)
    if completed.returncode != 0 and not continue_on_error:
        raise SystemExit(completed.returncode)
    return completed.returncode


def build_commands(args: argparse.Namespace, project_root: Path) -> list[list[str]]:
    python = sys.executable
    if args.level == "light":
        if not args.q:
            raise SystemExit("level=light 需要传入 --q，用任务关键词检索 runs、plans 和 mixed 上下文。")

        commands = [
            [
                python,
                str(script_path(project_root, "query.py")),
                "--scope",
                "runs",
                "--q",
                args.q,
                "--top",
                str(args.top_runs),
            ],
            [
                python,
                str(script_path(project_root, "query.py")),
                "--scope",
                "plans",
                "--q",
                args.q,
                "--top",
                str(args.top),
            ],
            [
                python,
                str(script_path(project_root, "query.py")),
                "--q",
                args.q,
                "--top",
                str(args.top),
            ],
        ]
        if args.brief:
            commands.append(
                [
                    python,
                    str(script_path(project_root, "pack_context.py")),
                    "--q",
                    args.q,
                    "--top",
                    str(args.top),
                    "--mode",
                    "brief",
                    "--max-chars",
                    str(args.max_chars),
                    "--no-write",
                    "--print",
                ]
            )
        return commands

    commands = [
        [python, str(script_path(project_root, "build_index.py"))],
        [python, str(script_path(project_root, "check.py"))],
    ]

    if args.level == "routing":
        commands.append([python, str(script_path(project_root, "eval_query.py"))])
    elif args.level == "full":
        commands.extend(
            [
                [python, str(script_path(project_root, "garden.py")), "--verbose"],
                [python, str(script_path(project_root, "eval_query.py"))],
            ]
        )

    return commands


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="按影响范围运行分级 context 检索/验证，避免普通任务默认跑完整四件套。"
    )
    parser.add_argument(
        "--level",
        choices=sorted(LEVEL_HELP),
        required=True,
        help="验证级别：light/standard/routing/full。",
    )
    parser.add_argument("--q", default="", help="level=light 时使用的任务关键词。")
    parser.add_argument("--top-runs", type=int, default=8, help="light 模式 runs 返回数量。")
    parser.add_argument("--top", type=int, default=5, help="light 模式稳定知识返回数量。")
    parser.add_argument("--brief", action="store_true", help="light 模式额外输出 brief pack。")
    parser.add_argument("--max-chars", type=int, default=1800, help="brief pack 最大字符数。")
    parser.add_argument("--dry-run", action="store_true", help="只打印将执行的命令。")
    parser.add_argument(
        "--continue-on-error",
        action="store_true",
        help="某一步失败后继续执行后续命令，最终仍返回失败。",
    )
    parser.add_argument(
        "--project-root",
        default=None,
        help="项目根目录。默认根据脚本路径自动识别。",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    project_root = resolve_project_root(args.project_root)

    print(f"level={args.level}: {LEVEL_HELP[args.level]}", flush=True)
    commands = build_commands(args, project_root)

    exit_code = 0
    for command in commands:
        result = run_command(
            project_root,
            command,
            dry_run=args.dry_run,
            continue_on_error=args.continue_on_error,
        )
        if result != 0:
            exit_code = result

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
