#!/usr/bin/env python3
"""检索本地上下文库并返回高相关文档。"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

TOKEN_RE = re.compile(r"[A-Za-z0-9_\-\u4e00-\u9fff]+")

SCOPE_MIXED = "mixed"
SCOPE_KNOWLEDGE = "knowledge"
SCOPE_DECISIONS = "decisions"
SCOPE_ALL = "all"

META_PATHS = {
    "docs/context/README.md",
    "docs/context/knowledge-map.md",
    "docs/context/CHANGELOG.md",
    "docs/context/decisions/README.md",
}


def tokenize(text: str) -> list[str]:
    return [token.lower() for token in TOKEN_RE.findall(text)]


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


def is_meta_path(rel_path: str) -> bool:
    normalized = rel_path.replace("\\", "/")
    return normalized in META_PATHS


def in_scope(rel_path: str, scope: str, include_meta: bool) -> bool:
    normalized = rel_path.replace("\\", "/")
    is_meta = is_meta_path(normalized)

    if is_meta and not include_meta:
        return False

    if scope == SCOPE_ALL:
        return True
    if scope == SCOPE_KNOWLEDGE:
        return normalized.startswith("docs/context/knowledge/")
    if scope == SCOPE_DECISIONS:
        return normalized.startswith("docs/context/decisions/")
    return normalized.startswith("docs/context/knowledge/") or normalized.startswith(
        "docs/context/decisions/"
    )


def path_bonus(rel_path: str) -> int:
    normalized = rel_path.replace("\\", "/")
    if normalized.startswith("docs/context/knowledge/"):
        return 8
    if normalized.startswith("docs/context/decisions/") and not normalized.endswith("/README.md"):
        return 6
    if is_meta_path(normalized):
        return -8
    return 0


def best_snippet(body: str, terms: list[str]) -> tuple[int, str]:
    if not body.strip():
        return 0, ""

    lines = body.splitlines()
    best_line_no = 1
    best_line = lines[0]
    best_hits = -1

    for idx, line in enumerate(lines, start=1):
        clean = line.strip()
        lowered = clean.lower()
        if not clean:
            continue
        hits = sum(1 for term in terms if term in lowered)
        if hits > best_hits:
            best_hits = hits
            best_line_no = idx
            best_line = clean
        elif hits == best_hits and len(clean) > len(best_line):
            best_line_no = idx
            best_line = clean

    return best_line_no, best_line[:220]


def score_document(
    entry: dict[str, Any],
    project_root: Path,
    query: str,
    terms: list[str],
    tag_filter: set[str],
    scope: str,
    include_meta: bool,
) -> dict[str, Any] | None:
    rel_path = str(entry.get("path", ""))
    if not rel_path:
        return None

    if not in_scope(rel_path, scope, include_meta):
        return None

    tags = [str(tag).lower() for tag in entry.get("tags", [])]
    if tag_filter and not tag_filter.intersection(tags):
        return None

    file_path = (project_root / rel_path).resolve()
    if not file_path.exists():
        return None

    raw = file_path.read_text(encoding="utf-8")
    _, body = parse_frontmatter(raw)

    title = str(entry.get("title", ""))
    summary = str(entry.get("summary", ""))

    title_lower = title.lower()
    summary_lower = summary.lower()
    body_lower = body.lower()
    query_lower = query.lower()

    lexical_score = 0
    if query_lower in title_lower:
        lexical_score += 24
    if query_lower in summary_lower:
        lexical_score += 10

    for term in terms:
        if term in title_lower:
            lexical_score += 7
        if term in summary_lower:
            lexical_score += 4
        if any(term in tag for tag in tags):
            lexical_score += 6
        lexical_score += min(body_lower.count(term), 30)

    if terms and all(term in body_lower for term in terms):
        lexical_score += 8

    if lexical_score <= 0:
        return None

    score = lexical_score + path_bonus(rel_path)
    if score <= 0:
        return None

    line_no, snippet = best_snippet(body, terms)
    return {
        "score": score,
        "path": rel_path.replace("\\", "/"),
        "title": title,
        "summary": summary,
        "tags": entry.get("tags", []),
        "line": line_no,
        "snippet": snippet,
    }


def load_index(index_path: Path) -> dict[str, Any]:
    return json.loads(index_path.read_text(encoding="utf-8"))


def search_documents(
    docs: list[dict[str, Any]],
    project_root: Path,
    query: str,
    top_n: int = 5,
    tags: list[str] | None = None,
    scope: str = SCOPE_MIXED,
    include_meta: bool = False,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    terms = tokenize(query)
    tag_filter = {item.lower() for item in (tags or [])}

    ranked: list[dict[str, Any]] = []
    for entry in docs:
        scored = score_document(
            entry=entry,
            project_root=project_root,
            query=query,
            terms=terms,
            tag_filter=tag_filter,
            scope=scope,
            include_meta=include_meta,
        )
        if scored is not None:
            ranked.append(scored)

    ranked.sort(key=lambda item: item["score"], reverse=True)
    top_n = max(1, int(top_n))
    results = ranked[:top_n]

    stats = {
        "candidates": len(docs),
        "matched": len(ranked),
        "returned": len(results),
        "scope": scope,
        "include_meta": include_meta,
        "tags": sorted(tag_filter),
    }
    return results, stats


def resolve_project_root(project_root_arg: str | None) -> Path:
    script_path = Path(__file__).resolve()
    if project_root_arg:
        return Path(project_root_arg).resolve()
    return script_path.parents[2]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="检索本地上下文库。")
    parser.add_argument("--q", required=True, help="查询关键词。")
    parser.add_argument("--top", type=int, default=5, help="返回前 N 条结果。")
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
        "--tag",
        action="append",
        default=[],
        help="按标签过滤，可重复传入。",
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
    parser.add_argument("--json", action="store_true", help="以 JSON 格式输出结果。")
    return parser


def main() -> int:
    parser = build_parser()
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

    if args.json:
        print(json.dumps(results, ensure_ascii=False, indent=2))
        return 0

    print(f"查询: {args.q}")
    print(
        f"候选文档: {stats['candidates']}，命中: {stats['matched']}，返回: {stats['returned']}"
    )
    print(f"检索范围: {stats['scope']}，包含导航文档: {stats['include_meta']}")
    if stats["tags"]:
        print(f"标签过滤: {', '.join(stats['tags'])}")
    print("")

    for idx, item in enumerate(results, start=1):
        tags = item["tags"]
        tag_text = ", ".join(tags) if tags else "-"
        print(f"[{idx}] 分数={item['score']} 文件={item['path']}")
        print(f"    标题: {item['title']}")
        print(f"    标签: {tag_text}")
        print(f"    摘要: {item['summary']}")
        print(f"    片段(L{item['line']}): {item['snippet']}")
        print("")

    if not results:
        print("未检索到结果。可尝试更换关键词或先补充上下文文档。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
