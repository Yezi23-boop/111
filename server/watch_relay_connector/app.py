"""Watch Relay Connector for Hermes Gateway Relay.

The endpoint remains the task truth source. This service owns only the private
Relay transport spool and a persisted one-device turn envelope used to safely
associate Hermes' final ``send`` when the native optional ``reply_to`` field is
absent.
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import os
import time
from collections import deque
from dataclasses import dataclass
from typing import Any

import httpx
from fastapi import FastAPI, Header, HTTPException, WebSocket, WebSocketDisconnect
from pydantic import BaseModel, Field

from relay_spool import RelaySpool, RelaySpoolError, RelayTurn


RELAY_CONTRACT_VERSION = 1
WATCH_CHAT_ID = "watch-001"
WATCH_USER_ID = "watch-001-owner"
WATCH_PROFILE = "main"
MAX_TRACE_EVENTS = 128


@dataclass(frozen=True)
class RelaySettings:
    """Runtime settings. Secrets remain environment-only and are never logged."""

    gateway_id: str
    gateway_secret: str
    internal_token: str
    endpoint_url: str
    endpoint_token: str
    db_path: str
    session_key: str
    test_mode: bool
    test_token: str

    @classmethod
    def from_env(cls) -> "RelaySettings":
        return cls(
            gateway_id=os.getenv("GATEWAY_RELAY_ID", "").strip(),
            gateway_secret=os.getenv("GATEWAY_RELAY_SECRET", "").strip(),
            internal_token=os.getenv("WATCH_RELAY_INTERNAL_TOKEN", "").strip(),
            endpoint_url=os.getenv("WATCH_RELAY_ENDPOINT_URL", "").strip().rstrip("/"),
            endpoint_token=os.getenv("WATCH_RELAY_ENDPOINT_TOKEN", "").strip(),
            db_path=os.getenv("WATCH_RELAY_DB_PATH", "/data/relay.db").strip(),
            session_key=os.getenv(
                "WATCH_RELAY_SESSION_KEY", "agent:main:relay:dm:watch-001"
            ).strip(),
            test_mode=os.getenv("WATCH_RELAY_TEST_MODE", "false").strip().lower() == "true",
            test_token=os.getenv("WATCH_RELAY_TEST_TOKEN", "").strip(),
        )


class SyntheticInbound(BaseModel):
    """Test-only normalized input. ``text`` is never copied into the trace."""

    request_id: str = Field(min_length=1, max_length=96)
    text: str = Field(min_length=1, max_length=512)


class InternalTurn(BaseModel):
    device_id: str = Field(min_length=1, max_length=64)
    request_id: str = Field(min_length=1, max_length=96)
    text: str = Field(min_length=1, max_length=512)


class InternalCancel(BaseModel):
    device_id: str = Field(min_length=1, max_length=64)


class RelayOutbound(BaseModel):
    request_id: str = Field(min_length=1, max_length=96)
    delivery_id: str = Field(min_length=1, max_length=160)
    content: str = Field(min_length=1, max_length=4096)
    reply_to: str = Field(default="", max_length=160)


class RelayService:
    """Own Relay wire state while endpoint owns task/conversation truth."""

    def __init__(self, settings: RelaySettings) -> None:
        self.settings = settings
        self.spool = RelaySpool(settings.db_path)
        self._websocket: WebSocket | None = None
        self._trace: deque[dict[str, str]] = deque(maxlen=MAX_TRACE_EVENTS)
        self._connected_at: float | None = None
        self._last_frame_at: float | None = None
        self._handshake_count = 0
        self._disconnect_count = 0
        self._replay_count = 0

    @property
    def connected(self) -> bool:
        return self._websocket is not None

    def attach(self, websocket: WebSocket) -> None:
        self._websocket = websocket
        self._connected_at = time.time()
        self._last_frame_at = self._connected_at
        self._record("connector", "connected")

    def detach(self, websocket: WebSocket) -> None:
        if self._websocket is websocket:
            self._websocket = None
            self._disconnect_count += 1
            self._record("connector", "disconnected")

    def mark_frame(self) -> None:
        """更新最近一次 Relay 帧时间，不保留帧正文。"""

        self._last_frame_at = time.time()

    def mark_handshake(self) -> None:
        """记录一次通过认证的 Hermes Relay handshake。"""

        self._handshake_count += 1
        self.mark_frame()

    def status_snapshot(self) -> dict[str, Any]:
        """返回仅供内网诊断的连接与 spool 摘要。"""

        active_turn = self.spool.active_turn(WATCH_CHAT_ID)
        return {
            "connected": self.connected,
            "connected_at": self._connected_at,
            "last_frame_at": self._last_frame_at,
            "handshake_count": self._handshake_count,
            "disconnect_count": self._disconnect_count,
            "replay_count": self._replay_count,
            "pending_inbounds": len(self.spool.pending_inbounds()),
            "active_turn_request_id": active_turn.request_id if active_turn else None,
            "active_turn_state": active_turn.state if active_turn else None,
        }

    def _record(self, direction: str, frame_type: str, **identifiers: Any) -> None:
        """Keep protocol evidence without retaining conversation text or secrets."""

        event = {"direction": direction, "type": frame_type}
        for key, value in identifiers.items():
            if value is not None and str(value):
                event[key] = str(value)[:128]
        self._trace.append(event)

    def trace(self) -> list[dict[str, str]]:
        return list(self._trace)

    def _frame_for_turn(self, turn: RelayTurn) -> dict[str, Any]:
        event = {
            "text": turn.text,
            "message_type": "text",
            "message_id": turn.message_id,
            "source": {
                "platform": "relay",
                "chat_id": turn.chat_id,
                "chat_type": "dm",
                "user_id": WATCH_USER_ID,
                "profile": WATCH_PROFILE,
                "message_id": turn.message_id,
            },
        }
        return {"type": "inbound", "event": event}

    async def submit_turn(self, turn: RelayTurn) -> None:
        """Persist an inbound frame and send it when the gateway is connected."""

        frame = self._frame_for_turn(turn)
        self.spool.put_inbound_frame(turn.request_id, json.dumps(frame, ensure_ascii=False), turn.message_id)
        if self._websocket is None:
            return
        if turn.state in (
            "sent",
            "awaiting_reply",
            "retryable",
            "completed",
            "canceled",
        ):
            return
        await _send_frame(self._websocket, frame)
        self.mark_frame()
        self.spool.mark_inbound_sent(turn.request_id)
        self._record(
            "connector_to_gateway",
            "inbound",
            request_id=turn.request_id,
            message_id=turn.message_id,
        )

    async def send_synthetic_inbound(self, request_id: str, text: str) -> str:
        """Test-only turn that uses the same durable path as endpoint ingress."""

        turn = self.spool.claim_turn(
            WATCH_CHAT_ID,
            request_id,
            text,
            chat_id=WATCH_CHAT_ID,
            session_key=self.settings.session_key,
        )
        await self.submit_turn(turn)
        return turn.message_id

    async def replay_pending(self) -> None:
        """Replay persisted inbound frames after a successful handshake."""

        if self._websocket is None:
            return
        for request_id, frame_json in self.spool.pending_inbounds():
            frame = json.loads(frame_json)
            await _send_frame(self._websocket, frame)
            self.mark_frame()
            self._replay_count += 1
            self.spool.mark_inbound_sent(request_id)
            self._record("connector_to_gateway", "inbound_replay", request_id=request_id)

    async def handle_outbound(self, frame: dict[str, Any]) -> dict[str, Any]:
        self.mark_frame()
        request_id = str(frame.get("requestId", ""))
        action = frame.get("action") if isinstance(frame.get("action"), dict) else {}
        op = str(action.get("op", ""))
        chat_id = str(action.get("chat_id", ""))
        self._record(
            "gateway_to_connector",
            "outbound",
            request_id=request_id,
            op=op,
            chat_id=chat_id,
            reply_to_present=str("reply_to" in action).lower(),
            reply_to=action.get("reply_to"),
        )
        if not request_id or chat_id != WATCH_CHAT_ID:
            return {"success": False, "error": "unsupported relay chat"}
        if op == "typing":
            return {"success": True}
        if op != "send":
            return {"success": False, "error": "unsupported POC outbound action"}
        content = str(action.get("content") or "").strip()
        if not content:
            return {"success": False, "error": "empty outbound content"}
        delivery = self.spool.resolve_delivery(
            chat_id=chat_id,
            gateway_request_id=request_id,
            content=content,
            explicit_reply_to=str(action.get("reply_to") or ""),
        )
        if delivery is None:
            return {"success": False, "error": "unbound_outbound"}
        if not self.settings.endpoint_url or not self.settings.endpoint_token:
            if self.settings.test_mode:
                self.spool.mark_delivery_result(delivery.delivery_id, True)
                return {"success": True, "message_id": delivery.delivery_id}
            self.spool.mark_delivery_result(delivery.delivery_id, False)
            return {"success": False, "error": "endpoint_egress_not_configured"}
        turn = self.spool.get_turn(delivery.request_id)
        if turn is None:
            self.spool.mark_delivery_result(delivery.delivery_id, False)
            return {"success": False, "error": "relay_turn_missing"}
        try:
            async with httpx.AsyncClient(timeout=10.0, trust_env=False) as client:
                response = await client.post(
                    f"{self.settings.endpoint_url}/internal/relay/outbound",
                    headers={"Authorization": f"Bearer {self.settings.endpoint_token}"},
                    json={
                        "device_id": turn.device_id,
                        "request_id": delivery.request_id,
                        "delivery_id": delivery.delivery_id,
                        "content": delivery.content,
                        "reply_to": delivery.reply_to,
                    },
                )
            if response.status_code not in (200, 202):
                raise RuntimeError(f"endpoint_status_{response.status_code}")
        except (httpx.RequestError, RuntimeError):
            self.spool.mark_delivery_result(delivery.delivery_id, False)
            return {"success": False, "error": "endpoint_egress_failed"}
        self.spool.mark_delivery_result(delivery.delivery_id, True)
        return {"success": True, "message_id": delivery.delivery_id}

    async def cancel_turn(self, request_id: str, device_id: str) -> dict[str, Any]:
        turn = self.spool.get_turn(request_id)
        if turn is None or turn.device_id != device_id:
            return {"accepted": False, "error": "relay_turn_not_found"}
        if turn.state in ("completed", "canceled"):
            return {"accepted": turn.state == "canceled", "state": turn.state}
        if self._websocket is None:
            # 断线时 Connector 没有得到 Hermes 的中断确认，不能伪造 canceled；
            # 保留 retryable 让重连后的运维或上层策略继续处理。
            self.spool.set_turn_state(request_id, "retryable")
            return {"accepted": False, "error": "relay_gateway_unavailable"}
        session_key = turn.session_key or self.settings.session_key
        await _send_frame(
            self._websocket,
            {"type": "interrupt_inbound", "session_key": session_key, "chat_id": turn.chat_id},
        )
        self.mark_frame()
        self.spool.set_turn_state(request_id, "canceled")
        self._record("connector_to_gateway", "interrupt_inbound", request_id=request_id)
        return {"accepted": True, "state": "canceled"}


def _hmac_hex(payload: str, secret: str) -> str:
    return hmac.new(secret.encode("utf-8"), payload.encode("utf-8"), hashlib.sha256).hexdigest()


def _verify_upgrade_token(token: str, settings: RelaySettings) -> bool:
    """Verify Hermes' documented base64url ``payload:exp:sig`` bearer token."""

    if not settings.gateway_id or not settings.gateway_secret or not token:
        return False
    try:
        padded = token + "=" * (-len(token) % 4)
        decoded = base64.urlsafe_b64decode(padded.encode("ascii")).decode("utf-8")
        payload, expires_at, signature = decoded.rsplit(":", 2)
        expiry = int(expires_at)
    except (UnicodeDecodeError, ValueError):
        return False
    if payload != settings.gateway_id or (expiry and int(time.time()) > expiry):
        return False
    expected = _hmac_hex(f"{payload}:{expiry}", settings.gateway_secret)
    return hmac.compare_digest(signature, expected)


def _authorized_test_request(settings: RelaySettings, token: str | None) -> None:
    if not settings.test_mode:
        raise HTTPException(status_code=404, detail="not found")
    if not settings.test_token or not token or not hmac.compare_digest(token, settings.test_token):
        raise HTTPException(status_code=403, detail="forbidden")


def _descriptor() -> dict[str, Any]:
    """The narrow capability set required for a final-text watch POC."""

    return {
        "contract_version": RELAY_CONTRACT_VERSION,
        "platform": "relay",
        "label": "AI Memory Watch",
        "max_message_length": 512,
        "supports_draft_streaming": False,
        "supports_edit": False,
        "supports_threads": False,
        "markdown_dialect": "plain",
        "len_unit": "chars",
        "supported_ops": ["send", "typing"],
    }


async def _send_frame(websocket: WebSocket, frame: dict[str, Any]) -> None:
    """Send one Relay frame using Hermes' newline-delimited JSON framing."""

    payload = json.dumps(frame, ensure_ascii=False, separators=(",", ":"))
    await websocket.send_text(f"{payload}\n")


settings = RelaySettings.from_env()
harness = RelayService(settings)
app = FastAPI(title="AI Memory Watch Relay Connector", docs_url=None, redoc_url=None)


@app.get("/health")
async def health() -> dict[str, Any]:
    return {
        "status": "ok",
        "relay_contract_version": RELAY_CONTRACT_VERSION,
        "gateway_connected": harness.connected,
        "test_mode": settings.test_mode,
        "spool_configured": bool(settings.db_path),
    }


@app.post("/test/inbound")
async def test_inbound(
    payload: SyntheticInbound,
    x_relay_test_token: str | None = Header(default=None),
) -> dict[str, str]:
    _authorized_test_request(settings, x_relay_test_token)
    try:
        message_id = await harness.send_synthetic_inbound(payload.request_id, payload.text)
    except RuntimeError as exc:
        raise HTTPException(status_code=409, detail=str(exc)) from exc
    return {"request_id": payload.request_id, "message_id": message_id}


@app.get("/test/trace")
async def test_trace(x_relay_test_token: str | None = Header(default=None)) -> dict[str, Any]:
    _authorized_test_request(settings, x_relay_test_token)
    return {"events": harness.trace()}


def _require_internal(settings: RelaySettings, authorization: str | None) -> None:
    if not settings.internal_token:
        raise HTTPException(status_code=503, detail="relay_internal_token_not_configured")
    prefix = "Bearer "
    if not authorization or not authorization.startswith(prefix):
        raise HTTPException(status_code=401, detail="missing_internal_token")
    if not hmac.compare_digest(authorization[len(prefix):], settings.internal_token):
        raise HTTPException(status_code=403, detail="invalid_internal_token")


@app.post("/internal/relay/turn")
async def internal_turn(
    payload: InternalTurn,
    authorization: str | None = Header(default=None),
) -> dict[str, Any]:
    _require_internal(settings, authorization)
    if payload.device_id != WATCH_CHAT_ID:
        raise HTTPException(status_code=403, detail="device_not_allowed")
    try:
        turn = harness.spool.claim_turn(
            payload.device_id,
            payload.request_id,
            payload.text,
            chat_id=WATCH_CHAT_ID,
            session_key=settings.session_key,
        )
        await harness.submit_turn(turn)
    except RelaySpoolError as exc:
        raise HTTPException(status_code=409, detail="relay_turn_conflict") from exc
    latest = harness.spool.get_turn(turn.request_id)
    return {
        "accepted": True,
        "request_id": turn.request_id,
        "message_id": turn.message_id,
        "state": latest.state if latest is not None else turn.state,
    }


@app.post("/internal/relay/turn/{request_id}/cancel")
async def internal_cancel(
    request_id: str,
    payload: InternalCancel,
    authorization: str | None = Header(default=None),
) -> dict[str, Any]:
    _require_internal(settings, authorization)
    if payload.device_id != WATCH_CHAT_ID:
        raise HTTPException(status_code=403, detail="device_not_allowed")
    return await harness.cancel_turn(request_id, payload.device_id)


@app.get("/internal/relay/status")
async def internal_status(authorization: str | None = Header(default=None)) -> dict[str, Any]:
    _require_internal(settings, authorization)
    return harness.status_snapshot()


@app.websocket("/relay")
async def relay(websocket: WebSocket) -> None:
    authorization = websocket.headers.get("authorization", "")
    scheme, _, token = authorization.partition(" ")
    if scheme.lower() != "bearer" or not _verify_upgrade_token(token, settings):
        await websocket.close(code=4401)
        return

    await websocket.accept()
    harness.attach(websocket)
    hello_received = False
    try:
        while True:
            wire = await websocket.receive_text()
            for line in wire.splitlines():
                if not line.strip():
                    continue
                try:
                    frame = json.loads(line)
                except json.JSONDecodeError:
                    harness._record("gateway_to_connector", "malformed_frame")
                    continue
                harness.mark_frame()
                frame_type = str(frame.get("type", ""))
                if not hello_received:
                    if frame_type != "hello" or frame.get("platform") != "relay":
                        harness._record("gateway_to_connector", "unexpected_pre_hello")
                        await websocket.close(code=4403)
                        return
                    hello_received = True
                    harness.mark_handshake()
                    harness._record("gateway_to_connector", "hello")
                    await _send_frame(websocket, {"type": "descriptor", "descriptor": _descriptor()})
                    harness._record("connector_to_gateway", "descriptor")
                    await harness.replay_pending()
                    continue

                if frame_type == "inbound_ack":
                    harness._record("gateway_to_connector", "inbound_ack", buffer_id=frame.get("bufferId"))
                    continue
                if frame_type == "interrupt":
                    harness._record("gateway_to_connector", "interrupt", session_key=frame.get("session_key"))
                    continue
                if frame_type == "outbound":
                    request_id = str(frame.get("requestId", ""))
                    result = await harness.handle_outbound(frame)
                    await _send_frame(websocket, {"type": "outbound_result", "requestId": request_id, "result": result})
                    harness._record(
                        "connector_to_gateway",
                        "outbound_result",
                        request_id=request_id,
                        success=result.get("success"),
                    )
                    continue

                harness._record("gateway_to_connector", "unsupported_frame", frame_name=frame_type)
    except WebSocketDisconnect:
        pass
    finally:
        harness.detach(websocket)
