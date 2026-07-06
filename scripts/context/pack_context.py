#!/usr/bin/env python3
"""将检索结果打包为可直接粘贴给 Codex 的上下文片段。"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from _stdio import configure_utf8_stdio
from query import (
    SCOPE_ALL,
    SCOPE_ARCHIVE,
    SCOPE_KNOWLEDGE,
    SCOPE_MIXED,
    SCOPE_PLANS,
    SCOPE_PROCEDURES,
    SCOPE_RUNS,
    load_index,
    resolve_project_root,
    search_documents,
)

configure_utf8_stdio()

MODE_BRIEF = "brief"
MODE_STANDARD = "standard"


def build_context_pack(
    query_text: str,
    results: list[dict[str, Any]],
    max_chars: int,
    scope: str,
    include_meta: bool,
    tags: list[str],
    mode: str,
) -> str:
    lines: list[str] = []
    lines.append("# 上下文包")
    lines.append("")
    lines.append(f"- 生成时间(UTC): {datetime.now(timezone.utc).isoformat()}")
    lines.append(f"- 查询: {query_text}")
    lines.append(f"- 范围: {scope}")
    lines.append(f"- 模式: {mode}")
    lines.append(f"- 包含导航文档: {include_meta}")
    if tags:
        lines.append(f"- 标签过滤: {', '.join(tags)}")
    if mode == MODE_BRIEF:
        lines.append("- 低 token 规则: 先读本包；只有命中项确实需要证据时再打开原文。")
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
        lines.append(f"   - 摘要: {item['summary']}")
        if mode == MODE_STANDARD:
            lines.append(f"   - 标签: {tag_text}")
    lines.append("")
    if mode == MODE_BRIEF:
        lines.append("## Brief Context")
    else:
        lines.append("## 可直接粘贴给 Codex 的上下文")
    lines.append("")

    used_chars = 0
    included = 0

    for item in results:
        if mode == MODE_BRIEF:
            owners = str(item.get("owners", "")).strip() or "-"
            memory_type = str(item.get("memory_type", "")).strip() or "-"
            evidence_level = str(item.get("evidence_level", "")).strip() or "-"
            status = str(item.get("status", "")).strip() or "active"
            block = [
                f"- `{item['path']}` score={item['score']} type={memory_type} evidence={evidence_level} status={status}",
                f"  title: {item['title']}",
                f"  summary: {item['summary']}",
                f"  owners: {owners}",
                f"  snippet L{item['line']}: {item['snippet']}",
                "",
            ]
        else:
            block = [
                f"### 来源: {item['path']}",
                f"- 相关分数: {item['score']}",
                f"- 关键片段(L{item['line']}): {item['snippet']}",
                f"- 摘要: {item['summary']}",
                f"- Owners: {str(item.get('owners', '')).strip() or '-'}",
                f"- Triggers: {str(item.get('triggers', '')).strip() or '-'}",
                f"- Status: {str(item.get('status', '')).strip() or 'active'}",
                f"- Superseded By: {str(item.get('superseded_by', '')).strip() or '-'}",
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
        default="docs/context/index/context-index.json",
        help="索引文件路径（相对项目根目录）。",
    )
    parser.add_argument(
        "--project-root",
        default=None,
        help="项目根目录。默认根据脚本路径自动识别。",
    )
    parser.add_argument(
        "--scope",
        choices=[
            SCOPE_MIXED,
            SCOPE_KNOWLEDGE,
            SCOPE_PROCEDURES,
            SCOPE_RUNS,
            SCOPE_PLANS,
            SCOPE_ARCHIVE,
            SCOPE_ALL,
        ],
        default=SCOPE_MIXED,
        help="检索范围。默认 mixed（knowledge + procedures + runs）。",
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
        default=1800,
        help="打包片段总字符预算。",
    )
    parser.add_argument(
        "--mode",
        choices=[MODE_BRIEF, MODE_STANDARD],
        default=MODE_BRIEF,
        help="打包模式。brief 默认低 token；standard 保留更多字段。",
    )
    parser.add_argument(
        "--output",
        default="context/pack/context-pack.md",
        help="输出文件路径（相对项目根目录）。",
    )
    parser.add_argument(
        "--no-write",
        action="store_true",
        help="只打印/返回上下文包，不写入输出文件；用于普通 light 检索避免污染工作区。",
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
        mode=args.mode,
    )

    output_path = (project_root / args.output).resolve()
    if not args.no_write:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(packed, encoding="utf-8")

    print(f"查询: {args.q}")
    print(
        f"候选文档: {stats['candidates']}，命中: {stats['matched']}，打包: {len(results)}"
    )
    print(f"模式: {args.mode}，字符预算: {max(500, args.max_chars)}")
    if args.no_write:
        print("输出文件: 未写入 (--no-write)")
    else:
        print(f"输出文件: {output_path}")

    if args.print:
        print("")
        print(packed)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
