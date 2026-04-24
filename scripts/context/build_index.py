#!/usr/bin/env python3
"""为 docs/context 下的 Markdown 文件构建轻量索引。"""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

TOKEN_RE = re.compile(r"[A-Za-z0-9_\-\u4e00-\u9fff]+")


@dataclass
class IndexedDoc:
    path: str
    title: str
    tags: list[str]
    summary: str
    id: str
    last_reviewed: str
    mtime_utc: str
    token_count: int

    def to_dict(self) -> dict[str, Any]:
        return {
            "path": self.path,
            "title": self.title,
            "tags": self.tags,
            "summary": self.summary,
            "id": self.id,
            "last_reviewed": self.last_reviewed,
            "mtime_utc": self.mtime_utc,
            "token_count": self.token_count,
        }


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


def extract_title(body: str, fallback: str) -> str:
    for line in body.splitlines():
        s = line.strip()
        if s.startswith("# "):
            return s[2:].strip()
    return fallback


def extract_summary(meta: dict[str, Any], body: str) -> str:
    if isinstance(meta.get("summary"), str) and meta["summary"].strip():
        return meta["summary"].strip()
    for line in body.splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        return s[:180]
    return ""


def index_markdown(root: Path, docs_root: Path) -> list[IndexedDoc]:
    docs: list[IndexedDoc] = []
    for md in sorted(docs_root.rglob("*.md")):
        if any(part.startswith(".") for part in md.parts):
            continue
        raw = md.read_text(encoding="utf-8")
        meta, body = parse_frontmatter(raw)
        rel = md.relative_to(root).as_posix()
        fallback_title = md.stem.replace("-", " ")
        title = extract_title(body, fallback_title)
        tags = meta.get("tags", [])
        if not isinstance(tags, list):
            tags = []
        summary = extract_summary(meta, body)
        token_count = len(TOKEN_RE.findall(body))
        mtime_utc = datetime.fromtimestamp(md.stat().st_mtime, tz=timezone.utc).isoformat()

        docs.append(
            IndexedDoc(
                path=rel,
                title=title,
                tags=tags,
                summary=summary,
                id=str(meta.get("id", "")),
                last_reviewed=str(meta.get("last_reviewed", "")),
                mtime_utc=mtime_utc,
                token_count=token_count,
            )
        )
    return docs


def main() -> int:
    parser = argparse.ArgumentParser(description="构建本地上下文索引。")
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
        "--output",
        default="context/index/context-index.json",
        help="索引 JSON 输出路径（相对项目根目录）。",
    )
    args = parser.parse_args()

    script_path = Path(__file__).resolve()
    project_root = Path(args.project_root).resolve() if args.project_root else script_path.parents[2]
    docs_root = (project_root / args.docs_root).resolve()
    output_path = (project_root / args.output).resolve()

    if not docs_root.exists():
        raise SystemExit(f"未找到文档目录: {docs_root}")

    docs = index_markdown(project_root, docs_root)

    payload: dict[str, Any] = {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "project_root": str(project_root),
        "docs_root": str(docs_root.relative_to(project_root).as_posix()),
        "doc_count": len(docs),
        "documents": [doc.to_dict() for doc in docs],
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8", newline="\r\n") as f:
        f.write(json.dumps(payload, ensure_ascii=False, indent=2))
    print(f"已索引 {len(docs)} 个文件 -> {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
