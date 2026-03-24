#!/usr/bin/env python3
"""将检索结果打包为可直接粘贴给 Codex 的上下文片段。"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from query import (
    SCOPE_ALL,
    SCOPE_DECISIONS,
    SCOPE_KNOWLEDGE,
    SCOPE_MIXED,
    load_index,
    resolve_project_root,
    search_documents,
)


def build_context_pack(
    query_text: str,
    results: list[dict[str, Any]],
    max_chars: int,
    scope: str,
    include_meta: bool,
    tags: list[str],
) -> str:
    lines: list[str] = []
    lines.append("# 上下文包")
    lines.append("")
    lines.append(f"- 生成时间(UTC): {datetime.now(timezone.utc).isoformat()}")
    lines.append(f"- 查询: {query_text}")
    lines.append(f"- 范围: {scope}")
    lines.append(f"- 包含导航文档: {include_meta}")
    if tags:
        lines.append(f"- 标签过滤: {', '.join(tags)}")
    lines.append("")
    lines.append("## 命中文档")
    lines.append("")

    if not results:
        lines.append("未命中文档。")
        return "\n".join(lines)

    for idx, item in enumerate(results, start=1):
        tag_text = ", ".join(item["tags"]) if item["tags"] else "-"
        lines.append(f"{idx}. `{item['path']}` (score={item['score']})")
        lines.append(f"   - 标题: {item['title']}")
        lines.append(f"   - 标签: {tag_text}")
        lines.append(f"   - 摘要: {item['summary']}")
    lines.append("")
    lines.append("## 可直接粘贴给 Codex 的上下文")
    lines.append("")

    used_chars = 0
    included = 0

    for item in results:
        block = [
            f"### 来源: {item['path']}",
            f"- 相关分数: {item['score']}",
            f"- 关键片段(L{item['line']}): {item['snippet']}",
            f"- 摘要: {item['summary']}",
            "",
        ]
        text = "\n".join(block)
        if used_chars + len(text) > max_chars:
            if included == 0:
                remain = max(80, max_chars - used_chars)
                text = text[:remain] + "..."
                lines.append(text)
                included = 1
            break
        lines.append(text)
        used_chars += len(text)
        included += 1

    lines.append("")
    lines.append(f"> 已打包片段数: {included}/{len(results)}，片段字符预算: {max_chars}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="打包上下文检索结果。")
    parser.add_argument("--q", required=True, help="查询关键词。")
    parser.add_argument("--top", type=int, default=5, help="最多打包前 N 条命中。")
    parser.add_argument(
        "--index",
        default="context/index/context-index.json",
        help="索引文件路径（相对项目根目录）。",
    )
    parser.add_argument(
        "--project-root",
        default=None,
        help="项目根目录。默认根据脚本路径自动识别。",
    )
    parser.add_argument(
        "--scope",
        choices=[SCOPE_MIXED, SCOPE_KNOWLEDGE, SCOPE_DECISIONS, SCOPE_ALL],
        default=SCOPE_MIXED,
        help="检索范围。默认 mixed（knowledge + decisions）。",
    )
    parser.add_argument(
        "--include-meta",
        action="store_true",
        help="包含 README、knowledge-map、CHANGELOG 等导航文档。",
    )
    parser.add_argument(
        "--tag",
        action="append",
        default=[],
        help="按标签过滤，可重复传入。",
    )
    parser.add_argument(
        "--max-chars",
        type=int,
        default=4000,
        help="打包片段总字符预算。",
    )
    parser.add_argument(
        "--output",
        default="context/pack/context-pack.md",
        help="输出文件路径（相对项目根目录）。",
    )
    parser.add_argument(
        "--print",
        action="store_true",
        help="在终端额外打印打包内容。",
    )
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root)
    index_path = (project_root / args.index).resolve()
    if not index_path.exists():
        raise SystemExit(
            f"未找到索引文件: {index_path}\n请先运行: python scripts/context/build_index.py"
        )

    data = load_index(index_path)
    docs = data.get("documents", [])
    results, stats = search_documents(
        docs=docs,
        project_root=project_root,
        query=args.q,
        top_n=args.top,
        tags=args.tag,
        scope=args.scope,
        include_meta=args.include_meta,
    )

    packed = build_context_pack(
        query_text=args.q,
        results=results,
        max_chars=max(500, args.max_chars),
        scope=args.scope,
        include_meta=args.include_meta,
        tags=args.tag,
    )

    output_path = (project_root / args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(packed, encoding="utf-8")

    print(f"查询: {args.q}")
    print(
        f"候选文档: {stats['candidates']}，命中: {stats['matched']}，打包: {len(results)}"
    )
    print(f"输出文件: {output_path}")

    if args.print:
        print("")
        print(packed)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
