"""
inbox_repo.py — Hermes 收件箱 SQLite 持久化层。

职责边界：
- 校验字段长度、kind 枚举和必填约束；超限抛 InboxValidationError。
- 幂等写入：(device_id, notification_id) 已存在时不覆盖，返回 created=False。
- 事务内删除该 device 超出最近 20 条的最旧记录（按 row_id ASC 删旧留新）。
- created_at 由本层在首次写入事务中生成，调用方不提供。
- 日志只记录 device_id/notification_id/kind/created/error，不记录 title/preview/body。
"""
from __future__ import annotations

import sqlite3
import threading
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

# ── 契约常量（与 watch_contract.v1.json inbox 节对齐）────────────────────────
_MAX_ITEMS = 20
_KIND_ENUM = {"reminder", "info", "warning"}
# 字段 UTF-8 字节上限
_BYTE_LIMITS: dict[str, int] = {
    "notification_id": 63,
    "kind": 23,
    "title": 63,
    "preview": 127,
    "body": 383,
}
# notification_id 禁止包含控制字符（CR/LF/其他控制码）
_CTRL_RE = __import__("re").compile(r"[\x00-\x1f\x7f]")


# ── 异常 ─────────────────────────────────────────────────────────────────────
class InboxValidationError(ValueError):
    """字段校验失败；调用方应映射为 HTTP 422。"""


# ── 数据类 ────────────────────────────────────────────────────────────────────
@dataclass
class InboxItem:
    notification_id: str
    source: str
    kind: str
    created_at: str
    title: str
    preview: str
    body: str
    read: bool


@dataclass
class CreateResult:
    created: bool
    item: InboxItem


# ── Repository ────────────────────────────────────────────────────────────────
class InboxRepo:
    """
    线程安全的 SQLite inbox repository。

    每个进程共用一个实例；SQLite 以 check_same_thread=False + threading.Lock 保护写路径。
    读路径（list/exists）也走同一 lock，保证快照一致。
    """

    def __init__(self, db_path: str | Path) -> None:
        self._db_path = str(db_path)
        self._lock = threading.Lock()
        self._conn = sqlite3.connect(
            self._db_path,
            check_same_thread=False,
            isolation_level=None,  # 手动事务
        )
        self._conn.row_factory = sqlite3.Row
        self._init_schema()

    # ── 初始化 ─────────────────────────────────────────────────────────────
    def _init_schema(self) -> None:
        """非破坏性建表；重启/重建容器后数据保留。"""
        with self._lock:
            self._conn.execute("PRAGMA journal_mode=WAL;")
            self._conn.execute(
                """
                CREATE TABLE IF NOT EXISTS watch_inbox (
                    row_id          INTEGER PRIMARY KEY AUTOINCREMENT,
                    device_id       TEXT    NOT NULL,
                    notification_id TEXT    NOT NULL,
                    source          TEXT    NOT NULL DEFAULT 'hermes',
                    kind            TEXT    NOT NULL,
                    created_at      TEXT    NOT NULL,
                    title           TEXT    NOT NULL,
                    preview         TEXT    NOT NULL,
                    body            TEXT    NOT NULL,
                    read            INTEGER NOT NULL DEFAULT 0,
                    UNIQUE(device_id, notification_id)
                )
                """
            )
            self._conn.execute(
                "CREATE INDEX IF NOT EXISTS idx_inbox_device_row "
                "ON watch_inbox (device_id, row_id DESC)"
            )

    # ── 校验 ───────────────────────────────────────────────────────────────
    @staticmethod
    def _validate_create(notification_id: str, kind: str, title: str, preview: str, body: str) -> None:
        """
        校验所有必填字段；任一失败抛 InboxValidationError。
        顺序：非空检查 → kind 枚举 → 字节长度 → notification_id 控制字符。
        """
        fields = {
            "notification_id": notification_id,
            "kind": kind,
            "title": title,
            "preview": preview,
            "body": body,
        }
        for name, value in fields.items():
            if not value or not value.strip():
                raise InboxValidationError(f"field '{name}' is required and must not be blank")

        if kind not in _KIND_ENUM:
            raise InboxValidationError(
                f"kind '{kind}' is not allowed; valid values: {sorted(_KIND_ENUM)}"
            )

        for name, value in fields.items():
            limit = _BYTE_LIMITS[name]
            byte_len = len(value.encode("utf-8"))
            if byte_len > limit:
                raise InboxValidationError(
                    f"field '{name}' exceeds {limit} bytes (got {byte_len})"
                )

        # notification_id 禁止 CR/LF/控制字符
        if _CTRL_RE.search(notification_id):
            raise InboxValidationError("notification_id must not contain control characters")

    # ── 写入 ───────────────────────────────────────────────────────────────
    def create(
        self,
        device_id: str,
        notification_id: str,
        kind: str,
        title: str,
        preview: str,
        body: str,
    ) -> CreateResult:
        """
        幂等写入：
        - 首次 → 插入，返回 created=True，HTTP 层映射 201。
        - 重复 → 不覆盖，返回 created=False 和已有记录，HTTP 层映射 200。
        - 插入成功后在同一事务内删除该 device 超出最近 20 条的最旧记录。
        """
        self._validate_create(notification_id, kind, title, preview, body)

        with self._lock:
            self._conn.execute("BEGIN IMMEDIATE")
            try:
                # 先查是否已有相同 (device_id, notification_id)
                cur = self._conn.execute(
                    "SELECT * FROM watch_inbox WHERE device_id=? AND notification_id=?",
                    (device_id, notification_id),
                )
                existing = cur.fetchone()
                if existing:
                    # 内容不可变：重复请求不覆盖，只返回已有记录
                    self._conn.execute("ROLLBACK")
                    return CreateResult(created=False, item=_row_to_item(existing))

                created_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
                self._conn.execute(
                    """
                    INSERT INTO watch_inbox
                        (device_id, notification_id, source, kind, created_at, title, preview, body, read)
                    VALUES (?, ?, 'hermes', ?, ?, ?, ?, ?, 0)
                    """,
                    (device_id, notification_id, kind, created_at, title, preview, body),
                )

                # 保留最近 _MAX_ITEMS 条，淘汰最旧（row_id 最小）的多余记录
                self._conn.execute(
                    """
                    DELETE FROM watch_inbox
                    WHERE device_id=? AND row_id NOT IN (
                        SELECT row_id FROM watch_inbox
                        WHERE device_id=?
                        ORDER BY row_id DESC
                        LIMIT ?
                    )
                    """,
                    (device_id, device_id, _MAX_ITEMS),
                )

                self._conn.execute("COMMIT")

                cur2 = self._conn.execute(
                    "SELECT * FROM watch_inbox WHERE device_id=? AND notification_id=?",
                    (device_id, notification_id),
                )
                row = cur2.fetchone()
                return CreateResult(created=True, item=_row_to_item(row))
            except Exception:
                self._conn.execute("ROLLBACK")
                raise

    # ── 列表 ───────────────────────────────────────────────────────────────
    def list_items(self, device_id: str) -> tuple[list[InboxItem], int]:
        """
        返回最近 _MAX_ITEMS 条（created_at DESC，同时刻以 row_id DESC 稳定排序）
        和未读数。空列表时返回 ([], 0)，不抛异常。
        """
        with self._lock:
            cur = self._conn.execute(
                """
                SELECT * FROM watch_inbox
                WHERE device_id=?
                ORDER BY row_id DESC
                LIMIT ?
                """,
                (device_id, _MAX_ITEMS),
            )
            rows = cur.fetchall()

        items = [_row_to_item(r) for r in rows]
        unread = sum(1 for i in items if not i.read)
        return items, unread

    # ── 标记已读 ───────────────────────────────────────────────────────────
    def mark_read(self, device_id: str, notification_id: str) -> Optional[InboxItem]:
        """
        幂等标记已读。
        - 目标存在（无论是否已读）→ 标记后返回 item。
        - 目标不存在或已被淘汰 → 返回 None，HTTP 层映射 404。
        """
        with self._lock:
            cur = self._conn.execute(
                "SELECT row_id FROM watch_inbox WHERE device_id=? AND notification_id=?",
                (device_id, notification_id),
            )
            row = cur.fetchone()
            if row is None:
                return None
            self._conn.execute(
                "UPDATE watch_inbox SET read=1 WHERE device_id=? AND notification_id=?",
                (device_id, notification_id),
            )
            cur2 = self._conn.execute(
                "SELECT * FROM watch_inbox WHERE device_id=? AND notification_id=?",
                (device_id, notification_id),
            )
            return _row_to_item(cur2.fetchone())


# ── 内部工具 ──────────────────────────────────────────────────────────────────
def _row_to_item(row: sqlite3.Row) -> InboxItem:
    return InboxItem(
        notification_id=row["notification_id"],
        source=row["source"],
        kind=row["kind"],
        created_at=row["created_at"],
        title=row["title"],
        preview=row["preview"],
        body=row["body"],
        read=bool(row["read"]),
    )
