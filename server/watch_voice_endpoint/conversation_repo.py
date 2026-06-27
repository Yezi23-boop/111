"""watch_conversation SQLite repository.

职责边界：
- 保存手表发起的 Hermes 对话消息，而不是 proactive inbox。
- 每个 device 只保留最近 20 条 messages，作为 WebSocket 断线补发真相源。
- 不保存 token、Authorization、音频内容、ASR/Hermes 原始响应体。
"""
from __future__ import annotations

import sqlite3
import threading
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


MAX_CONVERSATION_MESSAGES = 20
ROLES = {"user", "assistant"}
STATUSES = {"pending", "done", "error", "timeout", "canceled"}


class ConversationValidationError(ValueError):
    """watch_conversation 字段校验失败。"""


@dataclass
class ConversationMessage:
    message_id: str
    request_id: str
    role: str
    text: str
    created_at: str
    status: str


class ConversationRepo:
    """线程安全 SQLite conversation repository。"""

    def __init__(self, db_path: str | Path) -> None:
        self._db_path = str(db_path)
        self._lock = threading.Lock()
        self._conn = sqlite3.connect(
            self._db_path,
            check_same_thread=False,
            isolation_level=None,
        )
        self._conn.row_factory = sqlite3.Row
        self._init_schema()

    def _init_schema(self) -> None:
        with self._lock:
            self._conn.execute("PRAGMA journal_mode=WAL;")
            self._conn.execute(
                """
                CREATE TABLE IF NOT EXISTS watch_conversation (
                    row_id      INTEGER PRIMARY KEY AUTOINCREMENT,
                    device_id   TEXT NOT NULL,
                    message_id  TEXT NOT NULL,
                    request_id  TEXT NOT NULL,
                    role        TEXT NOT NULL,
                    text        TEXT NOT NULL,
                    created_at  TEXT NOT NULL,
                    status      TEXT NOT NULL,
                    UNIQUE(device_id, message_id)
                )
                """
            )
            self._conn.execute(
                "CREATE INDEX IF NOT EXISTS idx_conversation_device_row "
                "ON watch_conversation (device_id, row_id DESC)"
            )

    def add_message(
        self,
        device_id: str,
        request_id: str,
        role: str,
        text: str,
        status: str = "done",
    ) -> ConversationMessage:
        if role not in ROLES:
            raise ConversationValidationError(f"role '{role}' is not allowed")
        if status not in STATUSES:
            raise ConversationValidationError(f"status '{status}' is not allowed")
        normalized_text = text.strip()
        if not normalized_text:
            raise ConversationValidationError("text must not be blank")

        message_id = f"msg_{uuid.uuid4().hex}"
        created_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        with self._lock:
            self._conn.execute("BEGIN IMMEDIATE")
            try:
                self._conn.execute(
                    """
                    INSERT INTO watch_conversation
                        (device_id, message_id, request_id, role, text, created_at, status)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                    """,
                    (device_id, message_id, request_id, role, normalized_text, created_at, status),
                )
                self._conn.execute(
                    """
                    DELETE FROM watch_conversation
                    WHERE device_id=? AND row_id NOT IN (
                        SELECT row_id FROM watch_conversation
                        WHERE device_id=?
                        ORDER BY row_id DESC
                        LIMIT ?
                    )
                    """,
                    (device_id, device_id, MAX_CONVERSATION_MESSAGES),
                )
                self._conn.execute("COMMIT")
            except Exception:
                self._conn.execute("ROLLBACK")
                raise
        return ConversationMessage(
            message_id=message_id,
            request_id=request_id,
            role=role,
            text=normalized_text,
            created_at=created_at,
            status=status,
        )

    def list_recent(self, device_id: str) -> list[ConversationMessage]:
        """返回最近 20 条，按时间从旧到新排列，便于 UI 追加。"""
        with self._lock:
            rows = self._conn.execute(
                """
                SELECT * FROM (
                    SELECT * FROM watch_conversation
                    WHERE device_id=?
                    ORDER BY row_id DESC
                    LIMIT ?
                )
                ORDER BY row_id ASC
                """,
                (device_id, MAX_CONVERSATION_MESSAGES),
            ).fetchall()
        return [_row_to_message(row) for row in rows]

    def list_after(self, device_id: str, last_seen_message_id: str | None) -> list[ConversationMessage]:
        """按 last_seen_message_id 补发；未知 last_seen 时回退最近 20 条。"""
        if not last_seen_message_id:
            return self.list_recent(device_id)
        with self._lock:
            row = self._conn.execute(
                "SELECT row_id FROM watch_conversation WHERE device_id=? AND message_id=?",
                (device_id, last_seen_message_id),
            ).fetchone()
            if row is None:
                rows = self._conn.execute(
                    """
                    SELECT * FROM (
                        SELECT * FROM watch_conversation
                        WHERE device_id=?
                        ORDER BY row_id DESC
                        LIMIT ?
                    )
                    ORDER BY row_id ASC
                    """,
                    (device_id, MAX_CONVERSATION_MESSAGES),
                ).fetchall()
            else:
                rows = self._conn.execute(
                    """
                    SELECT * FROM watch_conversation
                    WHERE device_id=? AND row_id>?
                    ORDER BY row_id ASC
                    LIMIT ?
                    """,
                    (device_id, row["row_id"], MAX_CONVERSATION_MESSAGES),
                ).fetchall()
        return [_row_to_message(item) for item in rows]


def _row_to_message(row: sqlite3.Row) -> ConversationMessage:
    return ConversationMessage(
        message_id=row["message_id"],
        request_id=row["request_id"],
        role=row["role"],
        text=row["text"],
        created_at=row["created_at"],
        status=row["status"],
    )
