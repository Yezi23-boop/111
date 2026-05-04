#!/usr/bin/env python3
"""检查上下文库文档质量。"""

from __future__ import annotations

import argparse
import json
import re
from datetime import datetime
from pathlib import Path
from typing import Any

from _stdio import configure_utf8_stdio

configure_utf8_stdio()

ID_RE = re.compile(r"^[a-z0-9][a-z0-9\-]{1,127}$")

ROOT_CONTENT_FILES = (
    "INDEX.agent.md",
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
            tags = [item.strip() for item in value.split(",") if item.strip()]
            meta[key] = tags
        else:
            meta[key] = value
    return meta, body


def resolve_project_root(project_root_arg: str | None) -> Path:
    script_path = Path(__file__).resolve()
    if project_root_arg:
        return Path(project_root_arg).resolve()
    return script_path.parents[2]


def collect_target_files(docs_root: Path) -> list[Path]:
    files: list[Path] = []

    for file_name in ROOT_CONTENT_FILES:
        file_path = docs_root / file_name
        if file_path.exists():
            files.append(file_path)

    content_roots = (
        "knowledge",
        "decisions",
        "procedures",
        "runs",
        "plans",
        "handoffs",
    )

    for root_name in content_roots:
        root_path = docs_root / root_name
        if not root_path.exists():
            continue

        for file_path in sorted(root_path.rglob("*.md")):
            if root_name == "decisions" and file_path.name.lower() == "readme.md":
                continue
            files.append(file_path)

    return files


def validate_file(path: Path, docs_root: Path, seen_ids: dict[str, str]) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []

    rel_path = path.relative_to(docs_root).as_posix()
    raw = path.read_text(encoding="utf-8")
    meta, body = parse_frontmatter(raw)

    if not meta:
        errors.append(f"{rel_path}: 缺少 frontmatter")
        return errors, warnings

    required = ("id", "tags", "summary", "last_reviewed")
    for key in required:
        if key not in meta or (isinstance(meta[key], str) and not meta[key].strip()):
            errors.append(f"{rel_path}: 缺少必填字段 `{key}`")

    doc_id = str(meta.get("id", "")).strip()
    if doc_id:
        if not ID_RE.match(doc_id):
            errors.append(f"{rel_path}: `id` 格式非法（建议小写字母/数字/连字符）")
        elif doc_id in seen_ids:
            errors.append(f"{rel_path}: `id` 重复，已在 {seen_ids[doc_id]} 使用")
        else:
            seen_ids[doc_id] = rel_path

    tags = meta.get("tags")
    if not isinstance(tags, list) or not tags:
        errors.append(f"{rel_path}: `tags` 必须是非空列表")
    else:
        invalid_tags = [tag for tag in tags if not str(tag).strip()]
        if invalid_tags:
            errors.append(f"{rel_path}: `tags` 包含空值")

    summary = str(meta.get("summary", "")).strip()
    if not summary:
        errors.append(f"{rel_path}: `summary` 不能为空")
    elif len(summary) > 220:
        warnings.append(f"{rel_path}: `summary` 过长（建议 <= 220 字符）")

    last_reviewed = str(meta.get("last_reviewed", "")).strip()
    if last_reviewed:
        try:
            datetime.strptime(last_reviewed, "%Y-%m-%d")
        except ValueError:
            errors.append(f"{rel_path}: `last_reviewed` 日期格式应为 YYYY-MM-DD")

    if "# " not in body:
        warnings.append(f"{rel_path}: 未检测到一级标题（建议添加）")

    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser(description="检查上下文库文档质量。")
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
    parser.add_argument("--json", action="store_true", help="以 JSON 格式输出。")
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root)
    docs_root = (project_root / args.docs_root).resolve()
    if not docs_root.exists():
        raise SystemExit(f"未找到上下文目录: {docs_root}")

    files = collect_target_files(docs_root)
    seen_ids: dict[str, str] = {}
    all_errors: list[str] = []
    all_warnings: list[str] = []

    for file_path in files:
        errors, warnings = validate_file(file_path, docs_root, seen_ids)
        all_errors.extend(errors)
        all_warnings.extend(warnings)

    payload = {
        "checked_files": len(files),
        "errors": all_errors,
        "warnings": all_warnings,
    }

    if args.json:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        print(f"已检查文件: {len(files)}")
        print(f"错误: {len(all_errors)}，警告: {len(all_warnings)}")
        if all_errors:
            print("")
            print("错误列表：")
            for item in all_errors:
                print(f"- {item}")
        if all_warnings:
            print("")
            print("警告列表：")
            for item in all_warnings:
                print(f"- {item}")

    if all_errors:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
