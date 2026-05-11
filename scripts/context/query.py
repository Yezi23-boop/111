#!/usr/bin/env python3
"""检索本地上下文库并返回高相关文档。"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

from _stdio import configure_utf8_stdio

configure_utf8_stdio()

TOKEN_RE = re.compile(r"[A-Za-z0-9_\-\u4e00-\u9fff]+")

CJK_RE = re.compile(r"[\u4e00-\u9fff]")

SCOPE_MIXED = "mixed"
SCOPE_KNOWLEDGE = "knowledge"
SCOPE_DECISIONS = "decisions"
SCOPE_PROCEDURES = "procedures"
SCOPE_RUNS = "runs"
SCOPE_PLANS = "plans"
SCOPE_HANDOFFS = "handoffs"
SCOPE_ARCHIVE = "archive"
SCOPE_ALL = "all"

META_PATHS = {
    "docs/context/INDEX.agent.md",
    "docs/context/README.md",
    "docs/context/knowledge-map.md",
    "docs/context/CHANGELOG.md",
    "docs/context/decisions/README.md",
}

RETIRED_HINTS = (
    "历史可行性卡",
    "历史知识卡",
    "历史评估卡",
    "历史实验卡",
    "历史记录",
    "不再代表当前",
    "旧组件已退场",
)

HISTORY_INTENT_TERMS = (
    "history",
    "historical",
    "archive",
    "archived",
    "retired",
    "deprecated",
    "superseded",
    "legacy",
    "历史",
    "归档",
    "退场",
    "废弃",
    "被替代",
    "旧方案",
    "旧链路",
    "考古",
    "曾经",
    "历史迁移",
    "迁移历史",
    "迁移清单",
    "旧方案迁移",
    "migration checklist",
)

EPISODIC_INTENT_TERMS = (
    "attempt",
    "run",
    "tried",
    "failed",
    "failure",
    "crash",
    "panic",
    "error",
    "log",
    "monitor",
    "验证",
    "尝试",
    "失败",
    "崩溃",
    "错误",
    "错误码",
    "日志",
    "做过",
    "重复",
    "证据",
)


def tokenize(text: str) -> list[str]:
    """分词：CJK 连续序列只保留 bigram（丢弃单字噪声），其余按原逻辑。

    示例:
      "修改模型 esp-dl" -> ['修改','模型','esp-dl']
      "模型"           -> ['模型']
      "危险detection"  -> ['危险','detection']

    不引入 jieba 等外部依赖，bigram 在 title/summary/triggers 中已有足够召回。
    """
    tokens: list[str] = []
    for token in TOKEN_RE.findall(text):
        lowered = token.lower()
        if len(lowered) == 1 and lowered.isdigit():
            continue
        # 纯 CJK 序列 >=2 字符：只保留 bigram，丢弃单字
        if len(lowered) >= 2 and all(CJK_RE.match(ch) for ch in lowered):
            for i in range(len(lowered) - 1):
                tokens.append(lowered[i : i + 2])
        else:
            tokens.append(lowered)
    return tokens


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
    if scope == SCOPE_PROCEDURES:
        return normalized.startswith("docs/context/procedures/")
    if scope == SCOPE_RUNS:
        return normalized.startswith("docs/context/runs/")
    if scope == SCOPE_PLANS:
        return normalized.startswith("docs/context/plans/")
    if scope == SCOPE_HANDOFFS:
        return normalized.startswith("docs/context/handoffs/")
    if scope == SCOPE_ARCHIVE:
        return normalized.startswith("docs/context/archive/")

    return (
        normalized.startswith("docs/context/knowledge/")
        or normalized.startswith("docs/context/decisions/")
        or normalized.startswith("docs/context/procedures/")
        or normalized.startswith("docs/context/runs/")
    )


def path_bonus(rel_path: str) -> int:
    normalized = rel_path.replace("\\", "/")
    if normalized.startswith("docs/context/knowledge/"):
        return 8
    if normalized.startswith("docs/context/decisions/") and not normalized.endswith("/README.md"):
        return 6
    if normalized.startswith("docs/context/procedures/"):
        return 7
    if normalized.startswith("docs/context/runs/"):
        return 4
    if normalized.startswith("docs/context/plans/"):
        return 3
    if normalized.startswith("docs/context/handoffs/"):
        return 5
    if normalized.startswith("docs/context/archive/"):
        return -20
    if is_meta_path(normalized):
        return -8
    return 0


def has_history_intent(query: str, terms: list[str]) -> bool:
    query_lower = query.lower()
    return any(term in query_lower for term in HISTORY_INTENT_TERMS) or any(
        term in HISTORY_INTENT_TERMS for term in terms
    )


def lifecycle_penalty(
    rel_path: str,
    title: str,
    summary: str,
    status: str,
    superseded_by: str,
    query: str,
    terms: list[str],
) -> int | None:
    normalized = rel_path.replace("\\", "/")
    status_lower = status.strip().lower()
    superseded_by = superseded_by.strip()
    history_intent = has_history_intent(query, terms)

    if normalized.startswith("docs/context/archive/") and not history_intent:
        return None

    if status_lower == "archived" and not history_intent:
        return None

    text = f"{title}\n{summary}".lower()
    looks_retired = bool(superseded_by) or any(hint.lower() in text for hint in RETIRED_HINTS)

    if history_intent:
        if status_lower in {"archived", "superseded", "stale", "retired"} or looks_retired:
            return 8
        return 0

    if status_lower == "superseded":
        return -42
    if status_lower in {"retired", "deprecated"}:
        return -38
    if status_lower == "stale":
        return -24
    if looks_retired:
        return -28

    return 0


def episodic_mixed_penalty(rel_path: str, scope: str, query: str, terms: list[str]) -> int:
    normalized = rel_path.replace("\\", "/")
    if scope != SCOPE_MIXED or not normalized.startswith("docs/context/runs/"):
        return 0

    query_lower = query.lower()
    has_ephemeral_intent = any(term in query_lower for term in EPISODIC_INTENT_TERMS)
    has_ephemeral_intent = has_ephemeral_intent or any(
        term in EPISODIC_INTENT_TERMS for term in terms
    )
    if has_ephemeral_intent:
        return 0

    return -32


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
    meta, body = parse_frontmatter(raw)

    title = str(entry.get("title", ""))
    summary = str(entry.get("summary", ""))
    doc_id = str(entry.get("id", "") or meta.get("id", ""))
    triggers = str(entry.get("triggers", "") or meta.get("triggers", ""))
    owners = str(entry.get("owners", "") or meta.get("owners", ""))
    memory_type = str(entry.get("memory_type", "") or meta.get("memory_type", ""))
    scope_value = str(entry.get("scope", "") or meta.get("scope", ""))
    evidence_level = str(entry.get("evidence_level", "") or meta.get("evidence_level", ""))
    status = str(entry.get("status", "") or meta.get("status", ""))
    superseded_by = str(entry.get("superseded_by", "") or meta.get("superseded_by", ""))

    title_lower = title.lower()
    summary_lower = summary.lower()
    doc_id_lower = doc_id.lower()
    rel_path_lower = rel_path.lower()
    triggers_lower = triggers.lower()
    owners_lower = owners.lower()
    memory_type_lower = memory_type.lower()
    scope_lower = scope_value.lower()
    evidence_level_lower = evidence_level.lower()
    body_lower = body.lower()
    query_lower = query.lower()

    lexical_score = 0
    if query_lower in title_lower:
        lexical_score += 24
    if query_lower in summary_lower:
        lexical_score += 10
    if query_lower in doc_id_lower:
        lexical_score += 16
    if query_lower in triggers_lower:
        lexical_score += 10
    if query_lower in owners_lower:
        lexical_score += 8

    for term in terms:
        if term in title_lower:
            lexical_score += 7
        if term in summary_lower:
            lexical_score += 4
        if term in doc_id_lower:
            lexical_score += 8
        if term in rel_path_lower:
            lexical_score += 5
        if term in triggers_lower:
            lexical_score += 5
        if term in owners_lower:
            lexical_score += 4
        if term in memory_type_lower:
            lexical_score += 2
        if term in scope_lower:
            lexical_score += 2
        if term in evidence_level_lower:
            lexical_score += 1
        if any(term in tag for tag in tags):
            lexical_score += 6
        lexical_score += min(body_lower.count(term), 30)

    if terms and all(term in body_lower for term in terms):
        lexical_score += 8

    if lexical_score <= 0:
        return None

    lifecycle_score = lifecycle_penalty(
        rel_path=rel_path,
        title=title,
        summary=summary,
        status=status,
        superseded_by=superseded_by,
        query=query,
        terms=terms,
    )
    if lifecycle_score is None:
        return None

    score = (
        lexical_score
        + path_bonus(rel_path)
        + lifecycle_score
        + episodic_mixed_penalty(rel_path, scope, query, terms)
    )
    if score <= 0:
        return None

    line_no, snippet = best_snippet(body, terms)
    return {
        "score": score,
        "path": rel_path.replace("\\", "/"),
        "title": title,
        "summary": summary,
        "tags": entry.get("tags", []),
        "owners": owners,
        "triggers": triggers,
        "memory_type": memory_type,
        "scope": scope_value,
        "evidence_level": evidence_level,
        "status": status,
        "superseded_by": superseded_by,
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
        default="docs/context/index/context-index.json",
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
        choices=[
            SCOPE_MIXED,
            SCOPE_KNOWLEDGE,
            SCOPE_DECISIONS,
            SCOPE_PROCEDURES,
            SCOPE_RUNS,
            SCOPE_PLANS,
            SCOPE_HANDOFFS,
            SCOPE_ARCHIVE,
            SCOPE_ALL,
        ],
        default=SCOPE_MIXED,
        help="检索范围。默认 mixed（knowledge + decisions + procedures + runs）。",
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
        status = str(item.get("status", "")).strip() or "active"
        superseded_by = str(item.get("superseded_by", "")).strip()
        if status != "active" or superseded_by:
            print(f"    生命周期: {status} -> {superseded_by or '-'}")
        print(f"    片段(L{item['line']}): {item['snippet']}")
        print("")

    if not results:
        print("未检索到结果。可尝试更换关键词或先补充上下文文档。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
