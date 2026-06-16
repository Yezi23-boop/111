#!/usr/bin/env python3
"""对 docs/context 内容型文档做轻量园艺检查。"""

from __future__ import annotations

import argparse
import json
from datetime import datetime
from pathlib import Path
from typing import Any

from _stdio import configure_utf8_stdio

configure_utf8_stdio()

CONTENT_ROOTS = (
    "knowledge",
    "procedures",
    "runs",
    "plans",
    "handoffs",
)

ROOT_CONTENT_FILES = (
    "INDEX.agent.md",
)

RECOMMENDED_FIELDS = (
    "memory_type",
    "scope",
    "owners",
    "triggers",
    "evidence_level",
)

LIFECYCLE_STATUSES = (
    "active",
    "stale",
    "superseded",
    "retired",
    "deprecated",
    "archived",
)

GARDEN_REVIEW_STATUSES = (
    "archived",
    "covered",
    "keep-evidence",
    "keep-history",
    "no-action",
)

RETIRED_HINTS = (
    "历史可行性卡",
    "历史知识卡",
    "历史评估卡",
    "历史实验卡",
    "历史记录",
    "不再代表当前",
    "旧组件已退场",
)

SUCCESS_HINTS = (
    "结果：success",
    "结果状态：success",
    "success",
    "已验证",
)

PROMOTION_RECORD_REASONS = {
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
}

PROMOTION_MEMORY_TYPES = {
    "project_plan",
    "trial_error",
    "project_knowledge",
    "framework",
    "constraints",
    "stable_preferences",
}

PROMOTION_HINTS = (
    "用户规划",
    "项目规划",
    "长期规划",
    "重要决策",
    "决策",
    "取舍",
    "试错",
    "失败路径",
    "稳定事实",
    "项目知识",
    "真实 owner",
    "owner 分工",
    "owner 边界",
    "模块边界",
    "架构边界",
    "项目框架",
    "长期约束",
    "稳定偏好",
    "真机",
    "monitor",
    "panic",
    "crash",
    "错误码",
    "验证闭环",
)

REQUIRED_SECTIONS_BY_MATCH = (
    (
        "plans/active/",
        (
            "## Purpose / Big Picture",
            "## Scope / Non-Goals",
            "## Progress",
            "## Decision Log",
            "## Validation and Acceptance",
            "## Idempotence and Recovery",
            "## Next Step",
        ),
    ),
    (
        "runs/",
        (
            "## 背景",
            "## 环境",
            "## 操作",
            "## 观测",
            "## 结论",
            "## 未验证风险",
        ),
    ),
)

REQUIRED_SECTIONS_BY_EXACT = {
    "handoffs/current-task.md": (
        "## 目标",
        "## 当前状态",
        "## Progress",
        "## Decision Log",
        "## 已验证",
        "## 当前风险",
        "## 下一步",
        "## 证据入口",
    ),
    "handoffs/handoff-template.md": (
        "## 目标",
        "## 当前状态",
        "## Progress",
        "## Decision Log",
        "## 已验证",
        "## 当前风险",
        "## 下一步",
        "## 证据入口",
    ),
}

LIKELY_PATH_PREFIXES = (
    "docs/",
    "main/",
    "components/",
    "scripts/",
    "context/",
    "tests/",
    ".codex/",
)


def parse_frontmatter(raw: str) -> tuple[dict[str, Any], str]:
    if not raw.startswith("---\n"):
        return {}, raw

    end = raw.find("\n---\n", 4)
    if end == -1:
        return {}, raw

    block = raw[4:end]
    body = raw[end + 5 :]
    meta: dict[str, Any] = {}

    for line in block.splitlines():
        s = line.strip()
        if not s or s.startswith("#") or ":" not in s:
            continue
        key, value = s.split(":", 1)
        key = key.strip()
        value = value.strip().strip("'\"")
        if key == "tags":
            if value.startswith("[") and value.endswith("]"):
                value = value[1:-1]
            meta[key] = [item.strip() for item in value.split(",") if item.strip()]
        else:
            meta[key] = value
    return meta, body


def resolve_project_root(project_root_arg: str | None) -> Path:
    script_path = Path(__file__).resolve()
    if project_root_arg:
        return Path(project_root_arg).resolve()
    return script_path.parents[2]


def collect_files(docs_root: Path) -> list[Path]:
    files: list[Path] = []
    for file_name in ROOT_CONTENT_FILES:
        file_path = docs_root / file_name
        if file_path.exists():
            files.append(file_path)
    for root_name in CONTENT_ROOTS:
        root_path = docs_root / root_name
        if not root_path.exists():
            continue
        for file_path in sorted(root_path.rglob("*.md")):
            files.append(file_path)
    return files


def looks_like_repo_path(token: str) -> bool:
    token = token.strip()
    if not token:
        return False
    if token in {"AGENTS.md", "CLAUDE.md", "README.md"}:
        return True
    if any(token.startswith(prefix) for prefix in LIKELY_PATH_PREFIXES):
        return True
    return "/" in token or "\\" in token or "." in Path(token).name


def split_csv_like(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def garden_review_is_current(
    meta: dict[str, Any],
    rel_path: str,
    max_age_days: int,
    warnings: list[str],
) -> bool:
    garden_status = str(meta.get("garden_status", "")).strip().lower()
    if not garden_status:
        return False

    if garden_status not in GARDEN_REVIEW_STATUSES:
        warnings.append(
            f"{rel_path}: `garden_status` 建议使用 {', '.join(GARDEN_REVIEW_STATUSES)}"
        )
        return False

    garden_reviewed = str(meta.get("garden_reviewed", "")).strip()
    if not garden_reviewed:
        warnings.append(f"{rel_path}: 设置了 `garden_status` 但缺少 `garden_reviewed`")
        return False

    try:
        reviewed_at = datetime.strptime(garden_reviewed, "%Y-%m-%d")
    except ValueError:
        warnings.append(f"{rel_path}: `garden_reviewed` 日期格式应为 YYYY-MM-DD")
        return False

    return (datetime.utcnow() - reviewed_at).days <= max_age_days


def check_required_sections(rel_path: str, body: str) -> list[str]:
    warnings: list[str] = []
    required: tuple[str, ...] = ()

    if rel_path in REQUIRED_SECTIONS_BY_EXACT:
        required = REQUIRED_SECTIONS_BY_EXACT[rel_path]
    else:
        for match_text, match_required in REQUIRED_SECTIONS_BY_MATCH:
            if match_text in rel_path:
                if rel_path.endswith("README.md"):
                    continue
                required = match_required
                break

    for heading in required:
        if heading not in body:
            warnings.append(f"{rel_path}: 缺少推荐章节 `{heading}`")

    return warnings


def check_file(
    path: Path,
    docs_root: Path,
    project_root: Path,
    max_age_days: int,
) -> tuple[list[str], list[str], dict[str, list[dict[str, str]]]]:
    warnings: list[str] = []
    notes: list[str] = []
    candidates: dict[str, list[dict[str, str]]] = {
        "stale_candidates": [],
        "promotion_candidates": [],
        "archive_candidates": [],
        "broken_owner_refs": [],
        "low_value_run_candidates": [],
    }

    rel_path = path.relative_to(docs_root).as_posix()
    raw = path.read_text(encoding="utf-8")
    meta, body = parse_frontmatter(raw)

    if not meta:
        warnings.append(f"{rel_path}: 缺少 frontmatter，无法做园艺检查")
        return warnings, notes, candidates

    for key in RECOMMENDED_FIELDS:
        value = str(meta.get(key, "")).strip()
        if not value:
            warnings.append(f"{rel_path}: 缺少推荐字段 `{key}`")

    owners_value = str(meta.get("owners", "")).strip()
    for owner in split_csv_like(owners_value):
        if not looks_like_repo_path(owner):
            continue
        owner_path = (project_root / owner).resolve()
        if not owner_path.exists():
            message = f"{rel_path}: `owners` 引用不存在路径 `{owner}`"
            warnings.append(message)
            candidates["broken_owner_refs"].append(
                {
                    "path": rel_path,
                    "owner": owner,
                    "reason": "owners path does not exist",
                }
            )

    age_days = -1
    last_reviewed = str(meta.get("last_reviewed", "")).strip()
    if last_reviewed:
        try:
            reviewed_at = datetime.strptime(last_reviewed, "%Y-%m-%d")
            age_days = (datetime.utcnow() - reviewed_at).days
            if age_days > max_age_days:
                warnings.append(
                    f"{rel_path}: `last_reviewed` 距今 {age_days} 天，超过阈值 {max_age_days} 天"
                )
            else:
                notes.append(f"{rel_path}: freshness={age_days}d")
        except ValueError:
            warnings.append(f"{rel_path}: `last_reviewed` 日期格式非法")

    warnings.extend(check_required_sections(rel_path, body))

    status = str(meta.get("status", "active")).strip().lower() or "active"
    superseded_by = str(meta.get("superseded_by", "")).strip()
    summary_text = str(meta.get("summary", "")).lower()
    looks_retired = any(hint.lower() in summary_text for hint in RETIRED_HINTS)
    garden_review_current = garden_review_is_current(meta, rel_path, max_age_days, warnings)

    if status not in LIFECYCLE_STATUSES:
        warnings.append(
            f"{rel_path}: `status` 建议使用 {', '.join(LIFECYCLE_STATUSES)}"
        )

    if not garden_review_current and (
        age_days > max_age_days
        or status in {"stale", "superseded", "retired", "deprecated"}
        or looks_retired
    ):
        reason_parts: list[str] = []
        if age_days > max_age_days:
            reason_parts.append(f"last_reviewed={age_days}d")
        if status != "active":
            reason_parts.append(f"status={status}")
        if superseded_by:
            reason_parts.append(f"superseded_by={superseded_by}")
        if looks_retired:
            reason_parts.append("retired hint")
        candidates["stale_candidates"].append(
            {
                "path": rel_path,
                "reason": ", ".join(reason_parts) or "needs review",
            }
        )

    if not garden_review_current and status in {"stale", "superseded", "retired", "deprecated"} and superseded_by:
        candidates["archive_candidates"].append(
            {
                "path": rel_path,
                "reason": f"status={status}, superseded_by={superseded_by}",
            }
        )

    is_run_record = rel_path.startswith("runs/") and not rel_path.endswith("README.md")
    is_template = "template" in Path(rel_path).stem
    memory_type = str(meta.get("memory_type", "")).strip().lower()
    evidence_level = str(meta.get("evidence_level", "")).strip().lower()
    if is_run_record and not is_template and not garden_review_current:
        run_text = f"{meta.get('summary', '')}\n{body}".lower()
        looks_successful = any(hint.lower() in run_text for hint in SUCCESS_HINTS)
        record_reasons = {
            item.strip()
            for item in split_csv_like(str(meta.get("record_reasons", "")))
            if item.strip()
        }
        has_promotion_reason = bool(record_reasons & PROMOTION_RECORD_REASONS)
        has_promotion_type = memory_type in PROMOTION_MEMORY_TYPES
        has_promotion_hint = any(hint.lower() in run_text for hint in PROMOTION_HINTS)
        if looks_successful and evidence_level == "observed" and (
            has_promotion_reason or has_promotion_type or has_promotion_hint
        ):
            reason_parts: list[str] = []
            if has_promotion_reason:
                reason_parts.append(f"record_reasons={','.join(sorted(record_reasons))}")
            if has_promotion_type:
                reason_parts.append(f"memory_type={memory_type}")
            if has_promotion_hint:
                reason_parts.append("promotion hint")
            candidates["promotion_candidates"].append(
                {
                    "path": rel_path,
                    "reason": "; ".join(reason_parts),
                }
            )
        elif looks_successful and evidence_level == "observed":
            candidates["low_value_run_candidates"].append(
                {
                    "path": rel_path,
                    "reason": "successful run lacks long-term promotion reason",
                }
            )

    return warnings, notes, candidates


def main() -> int:
    parser = argparse.ArgumentParser(description="上下文文档园艺检查。")
    parser.add_argument(
        "--project-root",
        default=None,
        help="项目根目录。默认根据脚本路径自动识别。",
    )
    parser.add_argument(
        "--docs-root",
        default="docs/context",
        help="上下文文档目录（相对项目根目录）。",
    )
    parser.add_argument(
        "--max-age-days",
        type=int,
        default=90,
        help="超过该天数未复查的文档会给出告警。",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="打印完整警告列表。",
    )
    parser.add_argument("--json", action="store_true", help="以 JSON 输出。")
    parser.add_argument(
        "--summary-json",
        action="store_true",
        help="只输出候选桶和计数，不输出 freshness notes，适合低 token 自动化周检。",
    )
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root)
    docs_root = (project_root / args.docs_root).resolve()
    if not docs_root.exists():
        raise SystemExit(f"未找到上下文目录: {docs_root}")

    files = collect_files(docs_root)
    warnings: list[str] = []
    notes: list[str] = []
    candidate_buckets: dict[str, list[dict[str, str]]] = {
        "stale_candidates": [],
        "promotion_candidates": [],
        "archive_candidates": [],
        "broken_owner_refs": [],
        "low_value_run_candidates": [],
    }

    for file_path in files:
        file_warnings, file_notes, file_candidates = check_file(
            path=file_path,
            docs_root=docs_root,
            project_root=project_root,
            max_age_days=max(1, args.max_age_days),
        )
        warnings.extend(file_warnings)
        notes.extend(file_notes)
        for key, value in file_candidates.items():
            candidate_buckets[key].extend(value)

    payload = {
        "checked_files": len(files),
        "warnings": warnings,
        "notes": notes,
        **candidate_buckets,
    }
    summary_payload = {
        "checked_files": len(files),
        "warning_count": len(warnings),
        "candidate_counts": {
            key: len(value) for key, value in candidate_buckets.items()
        },
        **candidate_buckets,
    }

    if args.summary_json:
        print(json.dumps(summary_payload, ensure_ascii=False, indent=2))
    elif args.json:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        print(f"已园艺检查文件: {len(files)}")
        print(f"警告: {len(warnings)}")
        if warnings and args.verbose:
            print("")
            print("警告列表：")
            for item in warnings:
                print(f"- {item}")
        elif warnings:
            print("提示: 传入 --verbose 查看完整警告列表。")
        if args.verbose:
            print("")
            print("Curator 候选：")
            for key in (
                "stale_candidates",
                "promotion_candidates",
                "archive_candidates",
                "broken_owner_refs",
                "low_value_run_candidates",
            ):
                items = candidate_buckets[key]
                print(f"- {key}: {len(items)}")
                for item in items:
                    reason = item.get("reason", "")
                    suffix = f" ({reason})" if reason else ""
                    print(f"  - {item.get('path', '-')}{suffix}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
