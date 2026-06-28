"""watch_session SQLite repository — V2.3 server session 层。

职责边界：
- 维护每个 device 发起的 Hermes 任务生命周期（session/task state）。
- 不替换 watch_conversation；conversation 继续只保存 user/assistant messages。
- V2.3 让 session_id 与 request_id 1:1 映射，但数据库从一开始保留独立 session_id，
  避免 V2.4 多请求聚合时重写 schema。
- 终态（done/error/timeout/canceled）不可回退。
- 默认保留最近 24 小时或最近 100 条 session，以先到者淘汰。
- server 重启后 running/asr_ready/accepted → timeout；其他状态不变。
- 不保存 token、音频内容、ASR/Hermes 原始响应体或完整 reply 正文。
"""
from __future__ import annotations

import sqlite3
import threading
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Optional

# ── 契约常量 ──────────────────────────────────────────────────────────────────
MAX_SESSIONS = 100
MAX_SESSION_AGE_HOURS = 24

# 允许的状态及转移规则
# accepted -> asr_ready | canceled
# asr_ready -> running | error | timeout | canceled
# running -> done | error | timeout | canceled
# done/error/timeout/canceled -> 终态，不可回退
ALL_STATES = {"accepted", "asr_ready", "running", "done", "error", "timeout", "canceled"}
TERMINAL_STATES = {"done", "error", "timeout", "canceled"}

# 每个当前状态允许转移到的下一状态
_ALLOWED_TRANSITIONS: dict[str, set[str]] = {
    "accepted": {"asr_ready", "error", "timeout", "canceled"},
    "asr_ready": {"running", "error", "timeout", "canceled"},
    "running": {"done", "error", "timeout", "canceled"},
    "done": set(),
    "error": set(),
    "timeout": set(),
    "canceled": set(),
}


# ── 异常 ──────────────────────────────────────────────────────────────────────
class SessionValidationError(ValueError):
    """session 字段校验或非法状态转移。"""


# ── 数据类 ────────────────────────────────────────────────────────────────────
@dataclass
class WatchSession:
    session_id: str
    device_id: str
    request_id: str
    state: str
    user_text: str          # ASR 结果，空字符串表示尚未完成 ASR
    reply_text: str         # Hermes 回复，空字符串表示尚未完成
    created_at: str         # ISO 8601 UTC
    updated_at: str         # ISO 8601 UTC
    last_delivered_message_id: str  # 最近一次推送给手表的 conversation message_id


# ── Repository ────────────────────────────────────────────────────────────────
class SessionRepo:
    """线程安全 SQLite session repository。"""

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
        self._recover_on_startup()

    # ── schema ────────────────────────────────────────────────────────────────

    def _init_schema(self) -> None:
        with self._lock:
            self._conn.execute("PRAGMA journal_mode=WAL;")
            self._conn.execute(
                """
                CREATE TABLE IF NOT EXISTS watch_session (
                    row_id                     INTEGER PRIMARY KEY AUTOINCREMENT,
                    session_id                 TEXT NOT NULL,
                    device_id                  TEXT NOT NULL,
                    request_id                 TEXT NOT NULL,
                    state                      TEXT NOT NULL,
                    user_text                  TEXT NOT NULL DEFAULT '',
                    reply_text                 TEXT NOT NULL DEFAULT '',
                    created_at                 TEXT NOT NULL,
                    updated_at                 TEXT NOT NULL,
                    last_delivered_message_id  TEXT NOT NULL DEFAULT '',
                    UNIQUE(device_id, session_id)
                )
                """
            )
            self._conn.execute(
                "CREATE INDEX IF NOT EXISTS idx_session_device_updated "
                "ON watch_session (device_id, updated_at DESC)"
            )
            self._conn.execute(
                "CREATE INDEX IF NOT EXISTS idx_session_state "
                "ON watch_session (device_id, state)"
            )

    def _recover_on_startup(self) -> None:
        """将非终态 session 恢复为 timeout，终态保持原状。"""
        now_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        with self._lock:
            self._conn.execute("BEGIN IMMEDIATE")
            try:
                self._conn.execute(
                    """
                    UPDATE watch_session
                    SET state='timeout', updated_at=?
                    WHERE state IN ('accepted', 'asr_ready', 'running')
                    """,
                    (now_utc,),
                )
                self._conn.execute("COMMIT")
            except Exception:
                self._conn.execute("ROLLBACK")
                raise

    # ── 创建 ──────────────────────────────────────────────────────────────────

    def create(
        self,
        device_id: str,
        session_id: str,
        request_id: str,
    ) -> WatchSession:
        """创建新 session，初始状态 accepted。

        V2.3 调用约定：session_id 必须等于 request_id（1:1 映射）。
        """
        if not device_id or not device_id.strip():
            raise SessionValidationError("device_id must not be blank")
        if not session_id or not session_id.strip():
            raise SessionValidationError("session_id must not be blank")
        if not request_id or not request_id.strip():
            raise SessionValidationError("request_id must not be blank")

        now_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        state = "accepted"
        with self._lock:
            self._conn.execute("BEGIN IMMEDIATE")
            try:
                self._conn.execute(
                    """
                    INSERT OR IGNORE INTO watch_session
                        (session_id, device_id, request_id, state,
                         user_text, reply_text, created_at, updated_at,
                         last_delivered_message_id)
                    VALUES (?, ?, ?, ?, '', '', ?, ?, '')
                    """,
                    (session_id, device_id, request_id, state, now_utc, now_utc),
                )
                # 如果已存在（INSERT OR IGNORE 未插入），读取已有记录
                row = self._conn.execute(
                    "SELECT * FROM watch_session WHERE device_id=? AND session_id=?",
                    (device_id, session_id),
                ).fetchone()
                if row is None:
                    raise SessionValidationError("failed to create session")
                self._conn.execute("COMMIT")
            except Exception:
                self._conn.execute("ROLLBACK")
                raise
        return _row_to_session(row)

    # ── 状态转移 ──────────────────────────────────────────────────────────────

    def transition(
        self,
        device_id: str,
        session_id: str,
        new_state: str,
        user_text: str | None = None,
        reply_text: str | None = None,
        last_delivered_message_id: str | None = None,
    ) -> WatchSession:
        """转移 session 到 new_state。

        终态不可回退；非法转移抛 SessionValidationError。
        """
        if new_state not in ALL_STATES:
            raise SessionValidationError(f"unknown state '{new_state}'")

        now_utc = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        with self._lock:
            self._conn.execute("BEGIN IMMEDIATE")
            try:
                row = self._conn.execute(
                    "SELECT * FROM watch_session WHERE device_id=? AND session_id=?",
                    (device_id, session_id),
                ).fetchone()
                if row is None:
                    raise SessionValidationError(
                        f"session not found: device={device_id} session={session_id}"
                    )

                current = row["state"]
                if current not in _ALLOWED_TRANSITIONS:
                    raise SessionValidationError(
                        f"unknown current state '{current}' for session={session_id}"
                    )
                allowed = _ALLOWED_TRANSITIONS[current]
                if new_state not in allowed:
                    raise SessionValidationError(
                        f"illegal transition: {current} -> {new_state} "
                        f"(allowed: {sorted(allowed)}) for session={session_id}"
                    )

                # 构建 SET 子句
                set_parts = ["state=?", "updated_at=?"]
                params: list[str] = [new_state, now_utc]
                if user_text is not None:
                    set_parts.append("user_text=?")
                    params.append(user_text)
                if reply_text is not None:
                    set_parts.append("reply_text=?")
                    params.append(reply_text)
                if last_delivered_message_id is not None:
                    set_parts.append("last_delivered_message_id=?")
                    params.append(last_delivered_message_id)
                params.extend([device_id, session_id])

                set_clause = ", ".join(set_parts)
                self._conn.execute(
                    f"UPDATE watch_session SET {set_clause} "
                    "WHERE device_id=? AND session_id=?",
                    params,
                )
                self._conn.execute("COMMIT")
            except SessionValidationError:
                self._conn.execute("ROLLBACK")
                raise
            except Exception:
                self._conn.execute("ROLLBACK")
                raise

        # 重新读取返回最新状态
        return self.get(device_id, session_id)

    # ── 读取 ──────────────────────────────────────────────────────────────────

    def get(self, device_id: str, session_id: str) -> WatchSession:
        """读取单个 session；不存在时抛 SessionValidationError。"""
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM watch_session WHERE device_id=? AND session_id=?",
                (device_id, session_id),
            ).fetchone()
        if row is None:
            raise SessionValidationError(
                f"session not found: device={device_id} session={session_id}"
            )
        return _row_to_session(row)

    def get_by_request_id(self, device_id: str, request_id: str) -> WatchSession:
        """通过 request_id 查找 session（V2.3 session_id == request_id）。"""
        # V2.3: session_id 和 request_id 1:1，直接用 request_id 查 session_id
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM watch_session WHERE device_id=? AND request_id=? "
                "ORDER BY row_id DESC LIMIT 1",
                (device_id, request_id),
            ).fetchone()
        if row is None:
            raise SessionValidationError(
                f"session not found by request_id: device={device_id} request_id={request_id}"
            )
        return _row_to_session(row)

    def list_active(self, device_id: str) -> list[WatchSession]:
        """列出该 device 所有非终态 session（accepted/asr_ready/running）。"""
        with self._lock:
            rows = self._conn.execute(
                """
                SELECT * FROM watch_session
                WHERE device_id=? AND state NOT IN ('done','error','timeout','canceled')
                ORDER BY row_id ASC
                """,
                (device_id,),
            ).fetchall()
        return [_row_to_session(r) for r in rows]

    def list_recent(self, device_id: str) -> list[WatchSession]:
        """列出该 device 最近 session，按 updated_at 倒序。"""
        with self._lock:
            rows = self._conn.execute(
                """
                SELECT * FROM watch_session
                WHERE device_id=?
                ORDER BY updated_at DESC
                LIMIT ?
                """,
                (device_id, MAX_SESSIONS),
            ).fetchall()
        return [_row_to_session(r) for r in rows]

    # ── 淘汰 ──────────────────────────────────────────────────────────────────

    def expire_old(self) -> int:
        """淘汰超过 24 小时的终态 session 和超出 100 条上限的最旧记录。

        Returns:
            删除的行数。
        """
        cutoff = (datetime.now(timezone.utc) - timedelta(hours=MAX_SESSION_AGE_HOURS)).strftime(
            "%Y-%m-%dT%H:%M:%SZ"
        )
        deleted = 0
        with self._lock:
            self._conn.execute("BEGIN IMMEDIATE")
            try:
                # 1) 删除过期终态 session
                cur = self._conn.execute(
                    """
                    DELETE FROM watch_session
                    WHERE state IN ('done','error','timeout','canceled')
                      AND updated_at < ?
                    """,
                    (cutoff,),
                )
                deleted += cur.rowcount

                # 2) 按 device 保留最近 MAX_SESSIONS 条
                self._conn.execute(
                    """
                    DELETE FROM watch_session
                    WHERE row_id NOT IN (
                        SELECT row_id FROM watch_session w2
                        WHERE w2.device_id = watch_session.device_id
                        ORDER BY w2.row_id DESC
                        LIMIT ?
                    )
                    """,
                    (MAX_SESSIONS,),
                )
                # rowcount 不准确（子查询），不累计
                self._conn.execute("COMMIT")
            except Exception:
                self._conn.execute("ROLLBACK")
                raise
        return deleted


# ── helpers ───────────────────────────────────────────────────────────────────

def _row_to_session(row: sqlite3.Row) -> WatchSession:
    return WatchSession(
        session_id=row["session_id"],
        device_id=row["device_id"],
        request_id=row["request_id"],
        state=row["state"],
        user_text=row["user_text"] or "",
        reply_text=row["reply_text"] or "",
        created_at=row["created_at"],
        updated_at=row["updated_at"],
        last_delivered_message_id=row["last_delivered_message_id"] or "",
    )
