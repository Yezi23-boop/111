#!/usr/bin/env python3
"""快速记录 agent 已尝试的修改、验证和失败路径。"""

from __future__ import annotations

import argparse
import re
from datetime import datetime
from pathlib import Path

from _stdio import configure_utf8_stdio

configure_utf8_stdio()


SLUG_RE = re.compile(r"[^a-z0-9]+")

RECORD_REASONS = (
    "repeat-risk",
    "high-cost",
    "error-signature",
    "route-choice",
    "owner-architecture",
    "evidence",
    "handoff",
    "plan-decision",
    "project-knowledge",
    "framework-constraint",
)


def resolve_project_root(project_root_arg: str | None) -> Path:
    script_path = Path(__file__).resolve()
    if project_root_arg:
        return Path(project_root_arg).resolve()
    return script_path.parents[2]


def make_slug(title: str) -> str:
    lowered = title.strip().lower()
    slug = SLUG_RE.sub("-", lowered).strip("-")
    return slug[:64].strip("-") or "attempt"


def csv(items: list[str]) -> str:
    return ", ".join(item.strip() for item in items if item.strip())


def bullet_lines(items: list[str], empty_hint: str) -> list[str]:
    clean_items = [item.strip() for item in items if item.strip()]
    if not clean_items:
        return [f"- {empty_hint}"]
    return [f"- {item}" for item in clean_items]


def unique_output_path(runs_root: Path, date_text: str, slug: str) -> Path:
    candidate = runs_root / f"{date_text}-attempt-{slug}.md"
    if not candidate.exists():
        return candidate

    for index in range(2, 100):
        candidate = runs_root / f"{date_text}-attempt-{slug}-{index}.md"
        if not candidate.exists():
            return candidate

    raise SystemExit("无法生成唯一 attempt 文件名，请缩短或更换标题。")


def build_document(args: argparse.Namespace, date_text: str, doc_id_slug: str) -> str:
    result = args.status
    owners = csv(args.changed) or "docs/context/runs"
    triggers = csv(args.trigger) or args.title
    tags = csv(args.tag) or "context, run, attempt-log"
    evidence_level = "observed" if result in {"success", "partial", "failed"} else "inferred"
    record_reasons = csv(args.record_because) or ("force" if args.force else "")

    lines: list[str] = [
        "---",
        f"id: attempt-{date_text}-{doc_id_slug}",
        f"tags: {tags}",
        f"summary: {args.title}；结果：{result}。",
        f"last_reviewed: {date_text}",
        "memory_type: episodic",
        "scope: task",
        "status: active",
        f"result: {result}",
        f"owners: {owners}",
        f"triggers: {triggers}",
        f"evidence_level: {evidence_level}",
        f"record_reasons: {record_reasons}",
        f"force_reason: {'manual override without record-because' if args.force and not args.record_because else ''}",
        "---",
        "",
        f"# Attempt Log: {args.title}",
        "",
        "## 背景",
        "",
        f"- 本次要验证什么：{args.goal or args.title}",
        f"- 对应任务或计划：{args.task or '未绑定计划'}",
        f"- 结果状态：{result}",
        f"- 长期记录理由：{record_reasons or '未记录'}",
        "",
        "## 环境",
        "",
        f"- 分支/工作区状态：{args.worktree or '未记录'}",
        f"- 设备/串口/板型：{args.device or '未涉及或未记录'}",
        f"- 关键前置条件：{args.precondition or '未记录'}",
        "",
        "## 操作",
        "",
        "- 修改过的文件或 owner：",
        *bullet_lines(args.changed, "未记录"),
        "- 执行的命令或动作：",
        *bullet_lines(args.tried, "未记录"),
        "- 已尝试但不应直接重复的路径：",
        *bullet_lines(args.avoid, "未记录"),
        "",
        "## 观测",
        "",
        "- 关键日志/证据：",
        *bullet_lines(args.evidence, "未记录"),
        "- 与预期不一致的点：",
        *bullet_lines(args.mismatch, "未记录"),
        "",
        "## 结论",
        "",
        f"- 本次可以确认的事实：{args.conclusion or '未形成稳定结论'}",
        "- 仍然不能确认的事实：",
        *bullet_lines(args.unknown, "未记录"),
        "",
        "## 未验证风险",
        "",
        "- 下一轮仍需补证据的边界：",
        *bullet_lines(args.next, "未记录"),
    ]
    return "\n".join(lines) + "\n"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="记录一次 agent 修改/尝试，避免后续重复动作。")
    parser.add_argument("--title", required=True, help="短标题，用于文件名和摘要。")
    parser.add_argument(
        "--status",
        choices=["success", "partial", "failed", "abandoned", "superseded"],
        default="partial",
        help="本次尝试结果状态。",
    )
    parser.add_argument("--goal", default="", help="本次要验证的目标。")
    parser.add_argument("--task", default="", help="对应计划、issue 或任务名。")
    parser.add_argument("--worktree", default="", help="分支或工作区状态。")
    parser.add_argument("--device", default="", help="设备、串口或板型。")
    parser.add_argument("--precondition", default="", help="关键前置条件。")
    parser.add_argument("--conclusion", default="", help="本次确认的结论。")
    parser.add_argument("--changed", action="append", default=[], help="修改过的文件或 owner，可重复。")
    parser.add_argument("--tried", action="append", default=[], help="执行过的命令或动作，可重复。")
    parser.add_argument("--avoid", action="append", default=[], help="不要直接重复的路径，可重复。")
    parser.add_argument("--evidence", action="append", default=[], help="关键日志、测试或证据，可重复。")
    parser.add_argument("--mismatch", action="append", default=[], help="与预期不一致的点，可重复。")
    parser.add_argument("--unknown", action="append", default=[], help="仍不能确认的事实，可重复。")
    parser.add_argument("--next", action="append", default=[], help="下一步需要补证据的边界，可重复。")
    parser.add_argument("--tag", action="append", default=[], help="额外标签，可重复。")
    parser.add_argument("--trigger", action="append", default=[], help="检索触发词，可重复。")
    parser.add_argument(
        "--record-because",
        action="append",
        choices=RECORD_REASONS,
        default=[],
        help=(
            "长期记录理由，可重复。只有满足重复风险、失败代价、owner/架构影响、"
            "大问题错误签名、路线选择、关键证据、交接、规划/决策、项目知识或框架约束时才应写入 runs。"
        ),
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="绕过 record-because 门槛。仅用于人工确认确实需要记录的特殊情况。",
    )
    parser.add_argument(
        "--project-root",
        default=None,
        help="项目根目录。默认根据脚本路径自动识别。",
    )
    parser.add_argument(
        "--runs-root",
        default="docs/context/runs",
        help="runs 目录（相对项目根目录）。",
    )
    parser.add_argument("--print", action="store_true", help="额外打印生成内容。")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if not args.record_because and not args.force:
        allowed = ", ".join(RECORD_REASONS)
        raise SystemExit(
            "未记录 attempt：缺少 --record-because。"
            "上下文只保存有长期复用价值的规划、决策、试错、项目知识和框架。"
            f"可选值：{allowed}；如人工确认仍需记录，可加 --force。"
        )

    project_root = resolve_project_root(args.project_root)
    runs_root = (project_root / args.runs_root).resolve()
    runs_root.mkdir(parents=True, exist_ok=True)

    date_text = datetime.now().strftime("%Y-%m-%d")
    slug = make_slug(args.title)
    output_path = unique_output_path(runs_root, date_text, slug)
    doc_id_slug = output_path.stem.removeprefix(f"{date_text}-attempt-")
    document = build_document(args, date_text, doc_id_slug)
    output_path.write_text(document, encoding="utf-8")

    print(f"已记录 attempt: {output_path}")
    if args.print:
        print("")
        print(document)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
