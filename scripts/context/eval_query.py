#!/usr/bin/env python3
"""运行 query-golden.yaml 中的黄金查询，做轻量检索回归。"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from _stdio import configure_utf8_stdio
from query import SCOPE_MIXED, load_index, resolve_project_root, search_documents

configure_utf8_stdio()


def parse_scalar(text: str) -> str:
    value = text.strip()
    if value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    if value.startswith("'") and value.endswith("'"):
        return value[1:-1]
    return value


def parse_golden_yaml(path: Path) -> list[dict[str, Any]]:
    queries: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    current_list_key: str | None = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.rstrip()
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        if stripped == "queries:":
            continue

        if stripped.startswith("- id:"):
            if current is not None:
                queries.append(current)
            current = {"id": parse_scalar(stripped.split(":", 1)[1])}
            current_list_key = None
            continue

        if current is None:
            continue

        if stripped.endswith(":") and not stripped.startswith("- "):
            current_list_key = stripped[:-1]
            current[current_list_key] = []
            continue

        if stripped.startswith("- "):
            if current_list_key is None:
                raise ValueError(f"无法解析列表项: {raw_line}")
            current[current_list_key].append(parse_scalar(stripped[2:]))
            continue

        if ":" in stripped:
            key, value = stripped.split(":", 1)
            current[key.strip()] = parse_scalar(value)
            current_list_key = None
            continue

        raise ValueError(f"无法解析行: {raw_line}")

    if current is not None:
        queries.append(current)

    return queries


def evaluate_queries(
    golden_queries: list[dict[str, Any]],
    docs: list[dict[str, Any]],
    project_root: Path,
    scope: str,
    include_meta: bool,
) -> tuple[list[dict[str, Any]], dict[str, int]]:
    results: list[dict[str, Any]] = []
    passed = 0

    for item in golden_queries:
        query_text = str(item.get("query", "")).strip()
        query_id = str(item.get("id", "")).strip()
        if not query_text or not query_id:
            continue

        expected_top1 = str(item.get("expected_top1", "")).strip()
        expected_top3 = [str(x) for x in item.get("expected_top3", [])]
        expected_top5 = [str(x) for x in item.get("expected_top5", [])]
        forbidden_top3 = [str(x) for x in item.get("forbidden_top3", [])]
        forbidden_top5 = [str(x) for x in item.get("forbidden_top5", [])]
        min_results = int(item.get("min_results", 1))
        item_scope = str(item.get("scope", scope) or scope)
        item_include_meta = include_meta
        if str(item.get("include_meta", "")).strip().lower() in {"1", "true", "yes"}:
            item_include_meta = True
        top_n = 5 if expected_top5 or forbidden_top5 else 3

        matches, _ = search_documents(
            docs=docs,
            project_root=project_root,
            query=query_text,
            top_n=top_n,
            scope=item_scope,
            include_meta=item_include_meta,
        )
        actual_paths = [str(match["path"]) for match in matches]

        wrong_top1 = bool(expected_top1) and (
            not actual_paths or actual_paths[0] != expected_top1
        )
        missing_top3 = [path for path in expected_top3 if path not in actual_paths[:3]]
        missing_top5 = [path for path in expected_top5 if path not in actual_paths[:5]]
        hit_forbidden_top3 = [path for path in forbidden_top3 if path in actual_paths[:3]]
        hit_forbidden_top5 = [path for path in forbidden_top5 if path in actual_paths[:5]]
        too_few_results = len(actual_paths) < max(0, min_results)
        ok = (
            not wrong_top1
            and not missing_top3
            and not missing_top5
            and not hit_forbidden_top3
            and not hit_forbidden_top5
            and not too_few_results
        )
        if ok:
            passed += 1

        results.append(
            {
                "id": query_id,
                "query": query_text,
                "ok": ok,
                "expected_top3": expected_top3,
                "expected_top1": expected_top1,
                "expected_top5": expected_top5,
                "scope": item_scope,
                "include_meta": item_include_meta,
                "actual_paths": actual_paths,
                "wrong_top1": wrong_top1,
                "missing_top3": missing_top3,
                "missing_top5": missing_top5,
                "forbidden_top3": forbidden_top3,
                "forbidden_top5": forbidden_top5,
                "hit_forbidden_top3": hit_forbidden_top3,
                "hit_forbidden_top5": hit_forbidden_top5,
                "min_results": min_results,
                "too_few_results": too_few_results,
            }
        )

    stats = {
        "total": len(results),
        "passed": passed,
        "failed": len(results) - passed,
    }
    return results, stats


def main() -> int:
    parser = argparse.ArgumentParser(description="运行上下文检索黄金查询回归。")
    parser.add_argument(
        "--project-root",
        default=None,
        help="项目根目录。默认根据脚本路径自动识别。",
    )
    parser.add_argument(
        "--index",
        default="context/index/context-index.json",
        help="索引文件路径（相对项目根目录）。",
    )
    parser.add_argument(
        "--golden",
        default="docs/context/evals/query-golden.yaml",
        help="黄金查询 YAML 路径（相对项目根目录）。",
    )
    parser.add_argument(
        "--scope",
        default=SCOPE_MIXED,
        help="检索范围，默认 mixed。",
    )
    parser.add_argument(
        "--include-meta",
        action="store_true",
        help="评测时包含 README、knowledge-map 等导航文档。",
    )
    parser.add_argument("--json", action="store_true", help="以 JSON 输出。")
    args = parser.parse_args()

    project_root = resolve_project_root(args.project_root)
    index_path = (project_root / args.index).resolve()
    golden_path = (project_root / args.golden).resolve()

    if not index_path.exists():
        raise SystemExit(
            f"未找到索引文件: {index_path}\n请先运行: uv run python scripts/context/build_index.py"
        )
    if not golden_path.exists():
        raise SystemExit(f"未找到黄金查询文件: {golden_path}")

    data = load_index(index_path)
    docs = data.get("documents", [])
    golden_queries = parse_golden_yaml(golden_path)

    results, stats = evaluate_queries(
        golden_queries=golden_queries,
        docs=docs,
        project_root=project_root,
        scope=args.scope,
        include_meta=args.include_meta,
    )

    payload = {
        "stats": stats,
        "results": results,
    }

    if args.json:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        print(f"总查询: {stats['total']}，通过: {stats['passed']}，失败: {stats['failed']}")
        print(f"范围: {args.scope}，包含导航文档: {args.include_meta}")
        print("")
        for item in results:
            status = "PASS" if item["ok"] else "FAIL"
            print(f"[{status}] {item['id']} -> {item['query']}")
            if item["scope"] != args.scope or item["include_meta"] != args.include_meta:
                print(f"    范围覆盖: {item['scope']}，包含导航文档: {item['include_meta']}")
            print(f"    实际命中: {', '.join(item['actual_paths']) if item['actual_paths'] else '-'}")
            if item["wrong_top1"]:
                actual_top1 = item["actual_paths"][0] if item["actual_paths"] else "-"
                print(f"    top1 不匹配: 期望 {item['expected_top1']}，实际 {actual_top1}")
            if item["missing_top3"]:
                print(f"    缺失 top3: {', '.join(item['missing_top3'])}")
            if item["missing_top5"]:
                print(f"    缺失 top5: {', '.join(item['missing_top5'])}")
            if item["hit_forbidden_top3"]:
                print(f"    命中禁止 top3: {', '.join(item['hit_forbidden_top3'])}")
            if item["hit_forbidden_top5"]:
                print(f"    命中禁止 top5: {', '.join(item['hit_forbidden_top5'])}")
            if item["too_few_results"]:
                print(f"    返回结果不足: 期望至少 {item['min_results']} 条")
            print("")

    return 0 if stats["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
