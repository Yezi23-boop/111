"""SQLite-backed Relay turn and delivery spool.

The spool is the Connector's transport record, not the watch endpoint's task
truth.  A turn is created from an authenticated endpoint request before an
inbound Relay frame is sent.  That durable link is the only fallback used when
Hermes omits ``reply_to`` on a final ``send`` action.
"""

from __future__ import annotations

import sqlite3
import threading
import time
from dataclasses import dataclass
from pathlib import Path


# retryable 表示取消或投递状态尚未确认，仍必须阻塞同一 chat 的新任务。
ACTIVE_TURN_STATES = ("queued", "sent", "awaiting_reply", "retryable")


class RelaySpoolError(RuntimeError):
    """Raised when a Relay turn cannot be safely claimed or associated."""


@dataclass(frozen=True)
class RelayTurn:
    device_id: str
    request_id: str
    message_id: str
    chat_id: str
    text: str
    state: str
    session_key: str
    delivery_id: str


@dataclass(frozen=True)
class RelayDelivery:
    delivery_id: str
    request_id: str
    gateway_request_id: str
    chat_id: str
    content: str
    reply_to: str
    state: str


class RelaySpool:
    """Small thread-safe SQLite spool used by one Connector process."""

    def __init__(self, db_path: str | Path) -> None:
        self._db_path = str(db_path)
        self._lock = threading.RLock()
        Path(self._db_path).parent.mkdir(parents=True, exist_ok=True)
        self._conn = sqlite3.connect(
            self._db_path,
            check_same_thread=False,
            isolation_level=None,
        )
        self._conn.row_factory = sqlite3.Row
        self._init_schema()

    def _init_schema(self) -> None:
        with self._lock:
            self._conn.execute("PRAGMA journal_mode=WAL")
            self._conn.execute(
                """
                CREATE TABLE IF NOT EXISTS relay_turn (
                    request_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    message_id TEXT NOT NULL UNIQUE,
                    chat_id TEXT NOT NULL,
                    text TEXT NOT NULL,
                    state TEXT NOT NULL,
                    session_key TEXT NOT NULL DEFAULT '',
                    delivery_id TEXT NOT NULL DEFAULT '',
                    created_at REAL NOT NULL,
                    updated_at REAL NOT NULL
                )
                """
            )
            self._conn.execute(
                "CREATE INDEX IF NOT EXISTS idx_relay_turn_chat_state "
                "ON relay_turn(chat_id, state, created_at)"
            )
            # SQLite 不会用新定义替换已存在的 partial index；显式重建，
            # 才能让升级前已存在的数据库也把 retryable 纳入串行门禁。
            self._conn.execute("DROP INDEX IF EXISTS uq_relay_active_chat")
            self._conn.execute(
                """
                CREATE UNIQUE INDEX uq_relay_active_chat
                ON relay_turn(chat_id)
                WHERE state IN ('queued', 'sent', 'awaiting_reply', 'retryable')
                """
            )
            self._conn.execute(
                """
                CREATE TABLE IF NOT EXISTS relay_inbound (
                    message_id TEXT PRIMARY KEY,
                    request_id TEXT NOT NULL UNIQUE,
                    frame_json TEXT NOT NULL,
                    state TEXT NOT NULL,
                    created_at REAL NOT NULL,
                    updated_at REAL NOT NULL
                )
                """
            )
            self._conn.execute(
                """
                CREATE TABLE IF NOT EXISTS relay_delivery (
                    delivery_id TEXT PRIMARY KEY,
                    request_id TEXT NOT NULL,
                    gateway_request_id TEXT NOT NULL UNIQUE,
                    chat_id TEXT NOT NULL,
                    content TEXT NOT NULL,
                    reply_to TEXT NOT NULL,
                    state TEXT NOT NULL,
                    created_at REAL NOT NULL,
                    updated_at REAL NOT NULL
                )
                """
            )

    def claim_turn(
        self,
        device_id: str,
        request_id: str,
        text: str,
        *,
        chat_id: str,
        session_key: str = "",
    ) -> RelayTurn:
        """Atomically create one serial turn or return its exact duplicate."""

        if not device_id or not request_id or not text or not chat_id:
            raise RelaySpoolError("turn fields must not be blank")
        message_id = f"watch:{device_id}:{request_id}"
        now = time.time()
        with self._lock:
            self._conn.execute("BEGIN IMMEDIATE")
            try:
                row = self._conn.execute(
                    "SELECT * FROM relay_turn WHERE request_id=?", (request_id,)
                ).fetchone()
                if row is None:
                    try:
                        self._conn.execute(
                            """
                            INSERT INTO relay_turn
                                (request_id, device_id, message_id, chat_id, text,
                                 state, session_key, created_at, updated_at)
                            VALUES (?, ?, ?, ?, ?, 'queued', ?, ?, ?)
                            """,
                            (
                                request_id,
                                device_id,
                                message_id,
                                chat_id,
                                text,
                                session_key,
                                now,
                                now,
                            ),
                        )
                    except sqlite3.IntegrityError as exc:
                        raise RelaySpoolError("active chat already has a turn") from exc
                    row = self._conn.execute(
                        "SELECT * FROM relay_turn WHERE request_id=?", (request_id,)
                    ).fetchone()
                elif row["device_id"] != device_id or row["text"] != text:
                    raise RelaySpoolError("request_id was reused with different content")
                self._conn.execute("COMMIT")
            except Exception:
                self._conn.execute("ROLLBACK")
                raise
        return _turn_from_row(row)

    def put_inbound_frame(self, request_id: str, frame_json: str, message_id: str) -> None:
        now = time.time()
        with self._lock:
            self._conn.execute(
                """
                INSERT INTO relay_inbound(message_id, request_id, frame_json, state,
                                           created_at, updated_at)
                VALUES (?, ?, ?, 'queued', ?, ?)
                ON CONFLICT(request_id) DO UPDATE SET frame_json=excluded.frame_json,
                    updated_at=excluded.updated_at
                """,
                (message_id, request_id, frame_json, now, now),
            )

    def mark_inbound_sent(self, request_id: str) -> None:
        now = time.time()
        with self._lock:
            self._conn.execute(
                "UPDATE relay_inbound SET state='sent', updated_at=? WHERE request_id=?",
                (now, request_id),
            )
            self._conn.execute(
                "UPDATE relay_turn SET state='awaiting_reply', updated_at=? "
                "WHERE request_id=? AND state='queued'",
                (now, request_id),
            )

    def pending_inbounds(self) -> list[tuple[str, str]]:
        with self._lock:
            rows = self._conn.execute(
                "SELECT request_id, frame_json FROM relay_inbound "
                "WHERE state IN ('queued', 'sent') ORDER BY created_at"
            ).fetchall()
        return [(str(row["request_id"]), str(row["frame_json"])) for row in rows]

    def active_turn(self, chat_id: str) -> RelayTurn | None:
        with self._lock:
            rows = self._conn.execute(
                "SELECT * FROM relay_turn WHERE chat_id=? AND state IN "
                "('queued', 'sent', 'awaiting_reply', 'retryable') ORDER BY created_at",
                (chat_id,),
            ).fetchall()
        if len(rows) != 1:
            return None
        return _turn_from_row(rows[0])

    def get_turn(self, request_id: str) -> RelayTurn | None:
        with self._lock:
            row = self._conn.execute(
                "SELECT * FROM relay_turn WHERE request_id=?", (request_id,)
            ).fetchone()
        return _turn_from_row(row) if row is not None else None

    def resolve_delivery(
        self,
        *,
        chat_id: str,
        gateway_request_id: str,
        content: str,
        explicit_reply_to: str | None,
    ) -> RelayDelivery | None:
        """Associate one send with an active persisted turn, never by memory order."""

        with self._lock:
            row = None
            if explicit_reply_to:
                row = self._conn.execute(
                    "SELECT * FROM relay_turn WHERE message_id=? AND chat_id=? "
                    "AND state IN ('sent', 'awaiting_reply')",
                    (explicit_reply_to, chat_id),
                ).fetchone()
                # Hermes 明确给出的 link 不匹配时拒绝归属；只有字段缺失
                # 才允许使用唯一 active turn 的持久 envelope 兜底。
                if row is None:
                    return None
            else:
                row = self._conn.execute(
                    "SELECT * FROM relay_turn WHERE chat_id=? "
                    "AND state='awaiting_reply' ORDER BY created_at",
                    (chat_id,),
                ).fetchone()
                extra = self._conn.execute(
                    "SELECT COUNT(*) FROM relay_turn WHERE chat_id=? "
                    "AND state='awaiting_reply'",
                    (chat_id,),
                ).fetchone()
                if row is None or int(extra[0]) != 1:
                    return None
            existing = self._conn.execute(
                "SELECT * FROM relay_delivery WHERE gateway_request_id=?",
                (gateway_request_id,),
            ).fetchone()
            if existing is not None:
                return _delivery_from_row(existing)
            delivery_id = f"relay-delivery-{gateway_request_id}"
            now = time.time()
            self._conn.execute(
                """
                INSERT INTO relay_delivery
                    (delivery_id, request_id, gateway_request_id, chat_id, content,
                     reply_to, state, created_at, updated_at)
                VALUES (?, ?, ?, ?, ?, ?, 'forwarding', ?, ?)
                """,
                (
                    delivery_id,
                    row["request_id"],
                    gateway_request_id,
                    chat_id,
                    content,
                    explicit_reply_to or row["message_id"],
                    now,
                    now,
                ),
            )
            self._conn.execute(
                "UPDATE relay_turn SET delivery_id=?, updated_at=? WHERE request_id=?",
                (delivery_id, now, row["request_id"]),
            )
            created = self._conn.execute(
                "SELECT * FROM relay_delivery WHERE delivery_id=?", (delivery_id,)
            ).fetchone()
        return _delivery_from_row(created)

    def mark_delivery_result(self, delivery_id: str, success: bool) -> None:
        now = time.time()
        state = "delivered" if success else "retryable"
        with self._lock:
            row = self._conn.execute(
                "SELECT request_id FROM relay_delivery WHERE delivery_id=?",
                (delivery_id,),
            ).fetchone()
            if row is None:
                return
            self._conn.execute(
                "UPDATE relay_delivery SET state=?, updated_at=? WHERE delivery_id=?",
                (state, now, delivery_id),
            )
            if success:
                self._conn.execute(
                    "UPDATE relay_turn SET state='completed', updated_at=? WHERE request_id=?",
                    (now, row["request_id"]),
                )
                self._conn.execute(
                    "UPDATE relay_inbound SET state='completed', updated_at=? "
                    "WHERE request_id=?",
                    (now, row["request_id"]),
                )

    def set_turn_state(self, request_id: str, state: str) -> None:
        if state not in {"queued", "sent", "awaiting_reply", "completed", "canceled", "retryable"}:
            raise RelaySpoolError(f"unknown relay state: {state}")
        with self._lock:
            self._conn.execute(
                "UPDATE relay_turn SET state=?, updated_at=? WHERE request_id=?",
                (state, time.time(), request_id),
            )
            if state in {"completed", "canceled"}:
                self._conn.execute(
                    "UPDATE relay_inbound SET state=?, updated_at=? WHERE request_id=?",
                    (state, time.time(), request_id),
                )

    def close(self) -> None:
        with self._lock:
            self._conn.close()


def _turn_from_row(row: sqlite3.Row) -> RelayTurn:
    return RelayTurn(
        device_id=str(row["device_id"]),
        request_id=str(row["request_id"]),
        message_id=str(row["message_id"]),
        chat_id=str(row["chat_id"]),
        text=str(row["text"]),
        state=str(row["state"]),
        session_key=str(row["session_key"]),
        delivery_id=str(row["delivery_id"]),
    )


def _delivery_from_row(row: sqlite3.Row) -> RelayDelivery:
    return RelayDelivery(
        delivery_id=str(row["delivery_id"]),
        request_id=str(row["request_id"]),
        gateway_request_id=str(row["gateway_request_id"]),
        chat_id=str(row["chat_id"]),
        content=str(row["content"]),
        reply_to=str(row["reply_to"]),
        state=str(row["state"]),
    )
