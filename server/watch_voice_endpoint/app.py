from __future__ import annotations

import asyncio
import base64
import json
import os
import re
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Literal

import httpx
from fastapi import FastAPI, File, Form, Header, HTTPException, Query, Request, UploadFile, WebSocket
from pydantic import BaseModel
from starlette.websockets import WebSocketDisconnect

from conversation_repo import ConversationMessage, ConversationRepo, ConversationValidationError
from inbox_repo import InboxRepo, InboxValidationError


HERMES_API_URL = os.getenv("HERMES_API_URL", "http://127.0.0.1:8642").rstrip("/")
HERMES_API_KEY = os.getenv("HERMES_API_KEY", "")
HERMES_MODEL = os.getenv("HERMES_MODEL", "hermes-agent")
HERMES_TIMEOUT_SECONDS = float(os.getenv("HERMES_TIMEOUT_SECONDS", "120"))
WATCH_REQUEST_TIMEOUT_SECONDS = float(os.getenv("WATCH_REQUEST_TIMEOUT_SECONDS", "115"))
WATCH_CONVERSATION_SUFFIX = os.getenv("WATCH_CONVERSATION_SUFFIX", "ai-memory-watch")
WATCH_MOCK_ASR_TEXT = os.getenv("WATCH_MOCK_ASR_TEXT", "记一下明天看电池日志")
REQUEST_CACHE_LIMIT = int(os.getenv("WATCH_REQUEST_CACHE_LIMIT", "256"))
CANCELED_REQUEST_LIMIT = int(os.getenv("WATCH_CANCELED_REQUEST_LIMIT", "1024"))
ASR_PROVIDER = os.getenv("WATCH_ASR_PROVIDER", "mock").strip().lower()
MIMO_ASR_BASE_URL = os.getenv("MIMO_ASR_BASE_URL", os.getenv("XIAOMI_BASE_URL", "")).rstrip("/")
MIMO_ASR_API_KEY = os.getenv("MIMO_ASR_API_KEY", os.getenv("XIAOMI_API_KEY", ""))
MIMO_ASR_MODEL = os.getenv("MIMO_ASR_MODEL", "mimo-v2.5-asr")
MIMO_ASR_LANGUAGE = os.getenv("MIMO_ASR_LANGUAGE", "auto")
MIMO_ASR_TIMEOUT_SECONDS = float(os.getenv("MIMO_ASR_TIMEOUT_SECONDS", "60"))
MIMO_ASR_TRANSCODE_TIMEOUT_SECONDS = float(os.getenv("MIMO_ASR_TRANSCODE_TIMEOUT_SECONDS", "20"))
MAX_AUDIO_BYTES = int(os.getenv("WATCH_MAX_AUDIO_BYTES", str(6 * 1024 * 1024)))
MAX_TEXT_CHARS = int(os.getenv("WATCH_MAX_TEXT_CHARS", "240"))
REPLY_MAX_CHARS = int(os.getenv("WATCH_REPLY_MAX_CHARS", "80"))
WATCH_WS_ENABLED = os.getenv("WATCH_WS_ENABLED", "false").strip().lower() in ("1", "true", "yes", "on")
WATCH_WS_MAX_MESSAGE_BYTES = int(os.getenv("WATCH_WS_MAX_MESSAGE_BYTES", str(6 * 1024 * 1024)))
MIMO_ASR_DIRECT_MIME_TYPES = {"audio/wav", "audio/mp3", "audio/mpeg"}
REQUEST_ID_PATTERN = re.compile(r"^[A-Za-z0-9._:-]{1,96}$")
INVALID_REQUEST_ID = "invalid-request"

WATCH_INSTRUCTIONS = (
    "你是 AI Memory Watch 的 Hermes 大脑。输入来自 ESP32-S3 手表短语音转写或手表文本。"
    "优先判断用户是在记录记忆、创建提醒、提问，还是执行工具动作。"
    "回复必须适合小屏显示，尽量不超过 80 个中文字。"
    "如果需要追问，一次只问一个问题。"
)


class WatchResponse(BaseModel):
    request_id: str
    status: Literal["done", "error", "timeout", "canceled"]
    action: Literal[
        "memory_saved",
        "reminder_created",
        "question_answered",
        "clarification_needed",
        "no_action",
        "error",
    ]
    asr_text: str
    reply_text: str
    clarification_id: str | None = None
    error_code: str | None = None


class HealthResponse(BaseModel):
    status: Literal["ok", "error"]
    hermes_status: Literal["online", "offline"]
    device_id: str


app = FastAPI(title="AI Memory Watch Endpoint", version="0.1.0")
_canceled_requests: set[tuple[str, str]] = set()
_completed_requests: dict[tuple[str, str], WatchResponse] = {}
_inflight_requests: dict[tuple[str, str], asyncio.Task[WatchResponse]] = {}

# SQLite inbox repository；数据库路径可通过环境变量覆盖，便于测试隔离。
_INBOX_DB_PATH = Path(os.getenv("INBOX_DB_PATH", "/data/inbox.db"))
_CONVERSATION_DB_PATH = Path(os.getenv("CONVERSATION_DB_PATH", "/data/conversation.db"))
_inbox_repo: InboxRepo | None = None
_conversation_repo: ConversationRepo | None = None
_ws_background_tasks: set[asyncio.Task[None]] = set()


@dataclass
class WsConnectionState:
    """单条 watch WebSocket 的 best-effort 推送状态。"""

    websocket: WebSocket
    send_lock: asyncio.Lock
    connected: bool = True


def _get_inbox_repo() -> InboxRepo:
    """懒初始化 InboxRepo；测试时可在 app 启动前覆盖 _inbox_repo。"""
    global _inbox_repo
    if _inbox_repo is None:
        _INBOX_DB_PATH.parent.mkdir(parents=True, exist_ok=True)
        _inbox_repo = InboxRepo(_INBOX_DB_PATH)
    return _inbox_repo


def _get_conversation_repo() -> ConversationRepo:
    """懒初始化 watch_conversation store；server 断线补发的真相源。"""
    global _conversation_repo
    if _conversation_repo is None:
        _CONVERSATION_DB_PATH.parent.mkdir(parents=True, exist_ok=True)
        _conversation_repo = ConversationRepo(_CONVERSATION_DB_PATH)
    return _conversation_repo


_request_event_counts = {
    "processed": 0,
    "cache_hits": 0,
    "inflight_waits": 0,
    "canceled_hits": 0,
}
_request_status_counts = {
    "done": 0,
    "error": 0,
    "timeout": 0,
    "canceled": 0,
}
_auth_failure_counts = {
    "missing_bearer_token": 0,
    "invalid_device_token": 0,
    "device_not_allowed": 0,
}
_request_error_counts: dict[str, int] = {}
_last_request_summary: dict[str, str | int | float | None] = {}
_last_auth_failure_summary: dict[str, str | int] = {}
_request_lock = asyncio.Lock()


def _device_tokens() -> dict[str, str]:
    raw = os.getenv("WATCH_DEVICE_TOKENS", "")
    pairs: dict[str, str] = {}
    for item in raw.split(","):
        item = item.strip()
        if not item or "=" not in item:
            continue
        device_id, token = item.split("=", 1)
        if device_id.strip() and token.strip():
            pairs[device_id.strip()] = token.strip()
    return pairs


def _raise_auth_failure(
    endpoint: str,
    device_id: str,
    status_code: int,
    reason: str,
) -> None:
    _auth_failure_counts[reason] = _auth_failure_counts.get(reason, 0) + 1
    _last_auth_failure_summary.clear()
    _last_auth_failure_summary.update(
        {
            "endpoint": endpoint,
            "device_id": device_id,
            "status_code": status_code,
            "reason": reason,
            "occurred_at": int(time.time()),
        }
    )
    raise HTTPException(status_code=status_code, detail=reason)


def _require_device(device_id: str, authorization: str | None, endpoint: str) -> None:
    tokens = _device_tokens()
    expected = tokens.get(device_id)
    if not expected:
        _raise_auth_failure(endpoint, device_id, 403, "device_not_allowed")
    prefix = "Bearer "
    if not authorization or not authorization.startswith(prefix):
        _raise_auth_failure(endpoint, device_id, 401, "missing_bearer_token")
    if authorization[len(prefix) :] != expected:
        _raise_auth_failure(endpoint, device_id, 403, "invalid_device_token")


def _hermes_headers() -> dict[str, str]:
    if not HERMES_API_KEY:
        raise HTTPException(status_code=500, detail="hermes_api_key_missing")
    return {"Authorization": f"Bearer {HERMES_API_KEY}"}


def _conversation_for_device(device_id: str) -> str:
    return f"{device_id}-{WATCH_CONVERSATION_SUFFIX}"


def _extract_output_text(payload: dict) -> str:
    texts: list[str] = []
    for item in payload.get("output", []) or []:
        if item.get("type") != "message":
            continue
        for part in item.get("content", []) or []:
            text = part.get("text")
            if text:
                texts.append(str(text))
    if texts:
        return "\n".join(texts).strip()
    return str(payload.get("output_text") or "").strip()


def _infer_action(asr_text: str, reply_text: str) -> str:
    combined = f"{asr_text}\n{reply_text}"
    if "提醒" in combined:
        return "reminder_created"
    if "？" in asr_text or "?" in asr_text:
        return "question_answered"
    if "需要" in reply_text and ("补充" in reply_text or "确认" in reply_text):
        return "clarification_needed"
    return "memory_saved"


def _canceled_watch_response(request_id: str, asr_text: str = "") -> WatchResponse:
    return WatchResponse(
        request_id=request_id,
        status="canceled",
        action="no_action",
        asr_text=asr_text,
        reply_text="已取消等待",
    )


def _error_watch_response(
    request_id: str,
    reply_text: str = "没有处理成功，请再说一次",
    asr_text: str = "",
) -> WatchResponse:
    return WatchResponse(
        request_id=request_id,
        status="error",
        action="error",
        asr_text=asr_text,
        reply_text=reply_text,
        error_code="asr_or_agent_error",
    )


def _timeout_watch_response(request_id: str, asr_text: str = "") -> WatchResponse:
    return WatchResponse(
        request_id=request_id,
        status="timeout",
        action="error",
        asr_text=asr_text,
        reply_text="Hermes 处理超时",
        error_code="server_timeout",
    )


def _normalize_request_id(request_id: str) -> str | None:
    normalized = request_id.strip()
    if not REQUEST_ID_PATTERN.fullmatch(normalized):
        return None
    return normalized


def _record_request_event(name: str) -> None:
    _request_event_counts[name] = _request_event_counts.get(name, 0) + 1


def _record_request_result(
    device_id: str,
    request_id: str,
    response: WatchResponse,
    started_at: float,
    audio_bytes: int,
) -> WatchResponse:
    duration_ms = int((time.monotonic() - started_at) * 1000)
    _record_request_event("processed")
    _request_status_counts[response.status] = _request_status_counts.get(response.status, 0) + 1
    if response.error_code:
        _request_error_counts[response.error_code] = _request_error_counts.get(response.error_code, 0) + 1
    _last_request_summary.clear()
    _last_request_summary.update(
        {
            "device_id": device_id,
            "request_id": request_id,
            "status": response.status,
            "action": response.action,
            "error_code": response.error_code,
            "duration_ms": duration_ms,
            "audio_bytes": audio_bytes,
            "asr_provider": ASR_PROVIDER,
            "completed_at": int(time.time()),
        }
    )
    return response


def _trim_completed_requests() -> None:
    while len(_completed_requests) > REQUEST_CACHE_LIMIT:
        _completed_requests.pop(next(iter(_completed_requests)))


def _trim_canceled_requests() -> None:
    while len(_canceled_requests) > CANCELED_REQUEST_LIMIT:
        _canceled_requests.pop()


async def _read_audio(audio: UploadFile) -> bytes:
    chunks: list[bytes] = []
    total = 0
    while True:
        chunk = await audio.read(64 * 1024)
        if not chunk:
            break
        total += len(chunk)
        if total > MAX_AUDIO_BYTES:
            raise HTTPException(status_code=413, detail="audio_too_large")
        chunks.append(chunk)
    return b"".join(chunks)


def _audio_data_uri(audio_bytes: bytes, mime_type: str | None) -> str:
    content_type = mime_type or "audio/ogg"
    if content_type == "application/octet-stream":
        content_type = "audio/ogg"
    encoded = base64.b64encode(audio_bytes).decode("ascii")
    return f"data:{content_type};base64,{encoded}"


def _extract_asr_text(payload: dict) -> str:
    choices = payload.get("choices", []) or []
    if not choices:
        return ""
    message = choices[0].get("message", {}) or {}
    return str(message.get("content") or "").strip()


async def _transcode_to_wav(audio_bytes: bytes) -> tuple[bytes, str]:
    process = await asyncio.create_subprocess_exec(
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        "pipe:0",
        "-ac",
        "1",
        "-ar",
        "16000",
        "-f",
        "wav",
        "pipe:1",
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        stdout, stderr = await asyncio.wait_for(
            process.communicate(audio_bytes),
            timeout=MIMO_ASR_TRANSCODE_TIMEOUT_SECONDS,
        )
    except asyncio.TimeoutError as exc:
        process.kill()
        await process.wait()
        raise HTTPException(status_code=500, detail="audio_transcode_timeout") from exc
    if process.returncode != 0 or not stdout:
        raise HTTPException(
            status_code=500,
            detail=f"audio_transcode_failed:{stderr.decode('utf-8', errors='ignore')[:120]}",
        )
    return stdout, "audio/wav"


async def _prepare_mimo_asr_audio(audio_bytes: bytes, mime_type: str | None) -> tuple[bytes, str]:
    content_type = mime_type or "audio/ogg"
    if content_type == "application/octet-stream":
        content_type = "audio/ogg"
    if content_type in MIMO_ASR_DIRECT_MIME_TYPES:
        return audio_bytes, content_type
    return await _transcode_to_wav(audio_bytes)


async def _call_mimo_asr(audio_bytes: bytes, mime_type: str | None) -> str:
    if not MIMO_ASR_BASE_URL or not MIMO_ASR_API_KEY:
        raise HTTPException(status_code=500, detail="mimo_asr_config_missing")
    prepared_audio, prepared_mime_type = await _prepare_mimo_asr_audio(audio_bytes, mime_type)
    body = {
        "model": MIMO_ASR_MODEL,
        "messages": [
            {
                "role": "user",
                "content": [
                    {
                        "type": "input_audio",
                        "input_audio": {
                            "data": _audio_data_uri(prepared_audio, prepared_mime_type),
                        },
                    }
                ],
            }
        ],
        "asr_options": {
            "language": MIMO_ASR_LANGUAGE,
        },
    }
    timeout = httpx.Timeout(MIMO_ASR_TIMEOUT_SECONDS)
    async with httpx.AsyncClient(timeout=timeout, trust_env=False) as client:
        response = await client.post(
            f"{MIMO_ASR_BASE_URL}/chat/completions",
            headers={"Authorization": f"Bearer {MIMO_ASR_API_KEY}"},
            json=body,
        )
        response.raise_for_status()
        return _extract_asr_text(response.json())


async def _transcribe_audio(
    audio_bytes: bytes,
    mime_type: str | None,
    mock_asr_text: str | None,
) -> str:
    if mock_asr_text:
        return mock_asr_text.strip()
    if ASR_PROVIDER in ("", "mock"):
        return WATCH_MOCK_ASR_TEXT.strip()
    if ASR_PROVIDER == "mimo":
        return await _call_mimo_asr(audio_bytes, mime_type)
    raise HTTPException(status_code=500, detail="unsupported_asr_provider")


async def _call_hermes(device_id: str, asr_text: str, clarification_id: str | None) -> str:
    input_text = f"手表用户说：{asr_text}"
    if clarification_id:
        input_text += f"\n这是对追问 {clarification_id} 的补充回答。"
    body = {
        "model": HERMES_MODEL,
        "instructions": WATCH_INSTRUCTIONS,
        "input": input_text,
        "conversation": _conversation_for_device(device_id),
    }
    timeout = httpx.Timeout(HERMES_TIMEOUT_SECONDS)
    # Windows host proxy variables may include IPv6 loopback entries that httpx
    # parses differently; this service talks to an explicitly configured Hermes
    # endpoint, so do not inherit ambient proxy settings.
    async with httpx.AsyncClient(timeout=timeout, trust_env=False) as client:
        response = await client.post(
            f"{HERMES_API_URL}/v1/responses",
            headers=_hermes_headers(),
            json=body,
        )
        response.raise_for_status()
        return _extract_output_text(response.json())


async def _process_voice_command_inner(
    request_id: str,
    device_id: str,
    audio_bytes: bytes,
    audio_content_type: str | None,
    clarification_id: str | None,
    mock_asr_text: str | None,
) -> WatchResponse:
    key = (device_id, request_id)
    if key in _canceled_requests:
        return _canceled_watch_response(request_id)

    try:
        asr_text = await _transcribe_audio(audio_bytes, audio_content_type, mock_asr_text)
    except Exception:
        return WatchResponse(
            request_id=request_id,
            status="error",
            action="error",
            asr_text="",
            reply_text="没有处理成功，请再说一次",
            error_code="asr_or_agent_error",
        )
    if not asr_text:
        return WatchResponse(
            request_id=request_id,
            status="error",
            action="error",
            asr_text="",
            reply_text="没有听清，请再说一次",
            error_code="asr_or_agent_error",
        )

    try:
        reply_text = await _call_hermes(device_id, asr_text, clarification_id)
    except httpx.TimeoutException:
        return _timeout_watch_response(request_id, asr_text)
    except Exception:
        return WatchResponse(
            request_id=request_id,
            status="error",
            action="error",
            asr_text=asr_text,
            reply_text="没有处理成功，请再说一次",
            error_code="asr_or_agent_error",
        )
    if not reply_text:
        return WatchResponse(
            request_id=request_id,
            status="error",
            action="error",
            asr_text=asr_text,
            reply_text="没有处理成功，请再说一次",
            error_code="asr_or_agent_error",
        )

    if key in _canceled_requests:
        return _canceled_watch_response(request_id, asr_text)

    return WatchResponse(
        request_id=request_id,
        status="done",
        action=_infer_action(asr_text, reply_text),
        asr_text=asr_text,
        reply_text=reply_text[:REPLY_MAX_CHARS],
        clarification_id=None,
        error_code=None,
    )


async def _process_text_command_inner(
    request_id: str,
    device_id: str,
    text: str,
    clarification_id: str | None,
) -> WatchResponse:
    key = (device_id, request_id)
    if key in _canceled_requests:
        return _canceled_watch_response(request_id, text)

    try:
        reply_text = await _call_hermes(device_id, text, clarification_id)
    except httpx.TimeoutException:
        return _timeout_watch_response(request_id, text)
    except Exception:
        return WatchResponse(
            request_id=request_id,
            status="error",
            action="error",
            asr_text=text,
            reply_text="没有处理成功，请再发送一次",
            error_code="asr_or_agent_error",
        )
    if not reply_text:
        return WatchResponse(
            request_id=request_id,
            status="error",
            action="error",
            asr_text=text,
            reply_text="没有处理成功，请再发送一次",
            error_code="asr_or_agent_error",
        )

    if key in _canceled_requests:
        return _canceled_watch_response(request_id, text)

    return WatchResponse(
        request_id=request_id,
        status="done",
        action=_infer_action(text, reply_text),
        asr_text=text,
        reply_text=reply_text[:REPLY_MAX_CHARS],
        clarification_id=None,
        error_code=None,
    )


async def _process_voice_command(
    request_id: str,
    device_id: str,
    audio_bytes: bytes,
    audio_content_type: str | None,
    clarification_id: str | None,
    mock_asr_text: str | None,
) -> WatchResponse:
    started_at = time.monotonic()
    try:
        response = await asyncio.wait_for(
            _process_voice_command_inner(
                request_id=request_id,
                device_id=device_id,
                audio_bytes=audio_bytes,
                audio_content_type=audio_content_type,
                clarification_id=clarification_id,
                mock_asr_text=mock_asr_text,
            ),
            timeout=WATCH_REQUEST_TIMEOUT_SECONDS,
        )
    except asyncio.TimeoutError:
        response = _timeout_watch_response(request_id)
    return _record_request_result(device_id, request_id, response, started_at, len(audio_bytes))


async def _process_text_command(
    request_id: str,
    device_id: str,
    text: str,
    clarification_id: str | None,
) -> WatchResponse:
    started_at = time.monotonic()
    try:
        response = await asyncio.wait_for(
            _process_text_command_inner(
                request_id=request_id,
                device_id=device_id,
                text=text,
                clarification_id=clarification_id,
            ),
            timeout=WATCH_REQUEST_TIMEOUT_SECONDS,
        )
    except asyncio.TimeoutError:
        response = _timeout_watch_response(request_id, text)
    return _record_request_result(device_id, request_id, response, started_at, 0)


async def _await_request_task(
    key: tuple[str, str],
    task: asyncio.Task[WatchResponse],
) -> WatchResponse:
    device_id, request_id = key
    try:
        response = await asyncio.shield(task)
    except asyncio.CancelledError:
        response = _canceled_watch_response(request_id)

    async with _request_lock:
        if _inflight_requests.get(key) is task:
            _inflight_requests.pop(key, None)
            if response.status == "canceled":
                _record_request_result(device_id, request_id, response, time.monotonic(), 0)
        if key in _canceled_requests:
            return _canceled_watch_response(request_id, response.asr_text)
        if response.status != "canceled":
            _completed_requests[key] = response
            _trim_completed_requests()
        return response


def _conversation_message_payload(message: ConversationMessage) -> dict[str, object]:
    return {
        "message_id": message.message_id,
        "request_id": message.request_id,
        "role": message.role,
        "text": message.text,
        "created_at": message.created_at,
        "status": message.status,
    }


async def _ws_send_json(websocket: WebSocket, payload: dict[str, object]) -> None:
    # 单个 WebSocket 连接内串行发送，避免 ASR/Hermes 后台任务与主循环同时写 socket。
    await websocket.send_text(json.dumps(payload, ensure_ascii=False, separators=(",", ":")))


async def _ws_send_error(websocket: WebSocket, request_id: str | None, code: str) -> None:
    payload: dict[str, object] = {"type": "error", "error_code": code}
    if request_id:
        payload["request_id"] = request_id
    await _ws_send_json(websocket, payload)


async def _ws_try_send_json(conn: WsConnectionState | None, payload: dict[str, object]) -> None:
    """向仍在线的 WS 连接 best-effort 推送；断线不影响后台任务落库。"""
    if conn is None or not conn.connected:
        return
    async with conn.send_lock:
        if not conn.connected:
            return
        try:
            await _ws_send_json(conn.websocket, payload)
        except Exception:
            conn.connected = False


async def _ws_try_send_error(
    conn: WsConnectionState | None,
    request_id: str | None,
    code: str,
) -> None:
    payload: dict[str, object] = {"type": "error", "error_code": code}
    if request_id:
        payload["request_id"] = request_id
    await _ws_try_send_json(conn, payload)


async def _ws_run_hermes_job(
    conn: WsConnectionState | None,
    device_id: str,
    request_id: str,
    asr_text: str,
) -> None:
    repo = _get_conversation_repo()
    await _ws_try_send_json(conn, {"type": "task_started", "request_id": request_id})
    try:
        reply_text = await _call_hermes(device_id, asr_text, None)
        if not reply_text:
            raise RuntimeError("empty_hermes_reply")
        message = repo.add_message(
            device_id=device_id,
            request_id=request_id,
            role="assistant",
            text=reply_text[:REPLY_MAX_CHARS],
            status="done",
        )
        await _ws_try_send_json(
            conn,
            {"type": "conversation_message", **_conversation_message_payload(message)},
        )
    except Exception:
        try:
            message = repo.add_message(
                device_id=device_id,
                request_id=request_id,
                role="assistant",
                text="没有处理成功，请再说一次",
                status="error",
            )
            await _ws_try_send_json(
                conn,
                {"type": "conversation_message", **_conversation_message_payload(message)},
            )
        except Exception:
            await _ws_try_send_error(conn, request_id, "hermes_error")


async def _ws_finish_audio(
    conn: WsConnectionState | None,
    device_id: str,
    request_id: str,
    audio_bytes: bytes,
    mock_asr_text: str | None,
) -> None:
    if not audio_bytes:
        await _ws_try_send_error(conn, request_id, "empty_audio")
        return
    try:
        asr_text = await _transcribe_audio(audio_bytes, "audio/ogg", mock_asr_text)
        if not asr_text:
            await _ws_try_send_error(conn, request_id, "empty_asr_text")
            return
        message = _get_conversation_repo().add_message(
            device_id=device_id,
            request_id=request_id,
            role="user",
            text=asr_text,
            status="done",
        )
        await _ws_try_send_json(
            conn,
            {
                "type": "asr_result",
                "request_id": request_id,
                "message_id": message.message_id,
                "text": asr_text,
            },
        )
        await _ws_run_hermes_job(conn, device_id, request_id, asr_text)
    except ConversationValidationError:
        await _ws_try_send_error(conn, request_id, "conversation_store_error")
    except Exception:
        await _ws_try_send_error(conn, request_id, "asr_or_agent_error")


def _ws_track_background_task(task: asyncio.Task[None]) -> None:
    _ws_background_tasks.add(task)
    task.add_done_callback(_ws_background_tasks.discard)


@app.websocket("/v1/watch/ws")
async def watch_websocket(websocket: WebSocket) -> None:
    await websocket.accept()
    if not WATCH_WS_ENABLED:
        await _ws_send_json(websocket, {"type": "error", "error_code": "websocket_disabled"})
        await websocket.close(code=1013)
        return

    conn = WsConnectionState(websocket=websocket, send_lock=asyncio.Lock())
    device_id: str | None = None
    current_request_id: str | None = None
    audio_chunks: list[bytes] = []
    audio_total = 0
    mock_asr_text: str | None = None

    try:
        auth = await websocket.receive_json()
        if auth.get("type") != "auth":
            await _ws_try_send_error(conn, None, "auth_required")
            await websocket.close(code=1008)
            return
        device_id = str(auth.get("device_id") or "")
        device_token = str(auth.get("device_token") or "")
        expected = _device_tokens().get(device_id)
        if not expected or device_token != expected:
            await _ws_try_send_error(conn, None, "invalid_device_token")
            await websocket.close(code=1008)
            return
        await _ws_try_send_json(
            conn,
            {"type": "auth_ok", "server_time": int(time.time())},
        )
        last_seen = auth.get("last_seen_conversation_id")
        messages = _get_conversation_repo().list_after(
            device_id,
            str(last_seen) if last_seen else None,
        )
        await _ws_try_send_json(
            conn,
            {
                "type": "conversation_snapshot",
                "messages": [_conversation_message_payload(message) for message in messages],
                "unread_reply_count": sum(1 for message in messages if message.role == "assistant"),
            },
        )

        while True:
            message = await websocket.receive()
            if message.get("type") == "websocket.disconnect":
                break
            if message.get("bytes") is not None:
                if current_request_id is None:
                    await _ws_try_send_error(conn, None, "audio_start_required")
                    continue
                chunk = message["bytes"] or b""
                audio_total += len(chunk)
                if audio_total > min(MAX_AUDIO_BYTES, WATCH_WS_MAX_MESSAGE_BYTES):
                    current_request_id = None
                    audio_chunks.clear()
                    audio_total = 0
                    await _ws_try_send_error(conn, None, "audio_too_large")
                    continue
                audio_chunks.append(chunk)
                continue
            text = message.get("text")
            if not text:
                continue
            try:
                event = json.loads(text)
            except json.JSONDecodeError:
                await _ws_try_send_error(conn, current_request_id, "invalid_json")
                continue
            event_type = event.get("type")
            if event_type == "audio_start":
                request_id = _normalize_request_id(str(event.get("request_id") or ""))
                if request_id is None:
                    await _ws_try_send_error(conn, None, "invalid_request_id")
                    continue
                if event.get("format") not in (None, "ogg_opus"):
                    await _ws_try_send_error(conn, request_id, "unsupported_audio_format")
                    continue
                current_request_id = request_id
                audio_chunks = []
                audio_total = 0
                mock_asr_text = event.get("mock_asr_text")
                if mock_asr_text is not None:
                    mock_asr_text = str(mock_asr_text)
                await _ws_try_send_json(conn, {"type": "audio_started", "request_id": request_id})
                continue
            if event_type == "audio_end":
                request_id = str(event.get("request_id") or "")
                if not current_request_id or request_id != current_request_id:
                    await _ws_try_send_error(conn, request_id or current_request_id, "request_id_mismatch")
                    continue
                finished_request_id = current_request_id
                finished_audio = b"".join(audio_chunks)
                current_request_id = None
                audio_chunks = []
                audio_total = 0
                task = asyncio.create_task(
                    _ws_finish_audio(
                        conn,
                        device_id,
                        finished_request_id,
                        finished_audio,
                        mock_asr_text,
                    )
                )
                _ws_track_background_task(task)
                mock_asr_text = None
                continue
            if event_type == "ack":
                await _ws_try_send_json(
                    conn,
                    {
                        "type": "ack_ok",
                        "scope": event.get("scope") or "conversation",
                        "message_id": event.get("message_id"),
                    },
                )
                continue
            await _ws_try_send_error(conn, current_request_id, "unsupported_event_type")
    except WebSocketDisconnect:
        conn.connected = False
        return
    finally:
        conn.connected = False


class ConversationMessageOut(BaseModel):
    message_id: str
    request_id: str
    role: str
    text: str
    created_at: str
    status: str


class ConversationListResponse(BaseModel):
    messages: list[ConversationMessageOut]
    has_more: bool = False


@app.get("/v1/watch/conversation", response_model=ConversationListResponse)
async def conversation_list(
    device_id: str = Query(...),
    after: str | None = Query(default=None),
    authorization: str | None = Header(default=None),
) -> ConversationListResponse:
    """手表离页 pending 轮询对话结果；server conversation store 是真相源。"""
    _require_device(device_id, authorization, "conversation_list")
    messages = _get_conversation_repo().list_after(device_id, after)
    return ConversationListResponse(
        messages=[
            ConversationMessageOut(**_conversation_message_payload(message))
            for message in messages
        ],
        has_more=False,
    )


@app.get("/v1/watch/health", response_model=HealthResponse)
async def health(
    device_id: str = Query(...),
    authorization: str | None = Header(default=None),
) -> HealthResponse:
    _require_device(device_id, authorization, "watch_health")
    try:
        async with httpx.AsyncClient(timeout=5, trust_env=False) as client:
            response = await client.get(f"{HERMES_API_URL}/health")
            response.raise_for_status()
        return HealthResponse(status="ok", hermes_status="online", device_id=device_id)
    except Exception:
        return HealthResponse(status="error", hermes_status="offline", device_id=device_id)


@app.post("/v1/watch/voice-command", response_model=WatchResponse)
async def voice_command(
    request_id: str = Form(...),
    device_id: str = Form(...),
    audio: UploadFile = File(...),
    clarification_id: str | None = Form(default=None),
    battery_percent: int | None = Form(default=None),
    charging: bool | None = Form(default=None),
    rssi: int | None = Form(default=None),
    firmware_version: str | None = Form(default=None),
    locale: str = Form("zh-CN"),
    timezone: str = Form("Asia/Shanghai"),
    source: str = Form("watch_hermes_page"),
    ui_state: str = Form("ready"),
    mock_asr_text: str | None = Form(default=None),
    authorization: str | None = Header(default=None),
) -> WatchResponse:
    _require_device(device_id, authorization, "voice_command")
    del battery_percent, charging, rssi, firmware_version, locale, timezone, source, ui_state
    normalized_request_id = _normalize_request_id(request_id)
    if normalized_request_id is None:
        response = _error_watch_response(INVALID_REQUEST_ID, "请求编号异常，请重新发送")
        return _record_request_result(device_id, INVALID_REQUEST_ID, response, time.monotonic(), 0)
    request_id = normalized_request_id
    key = (device_id, request_id)

    async with _request_lock:
        if key in _completed_requests:
            _record_request_event("cache_hits")
            return _completed_requests[key]
        if key in _canceled_requests:
            _record_request_event("canceled_hits")
            return _canceled_watch_response(request_id)
        task = _inflight_requests.get(key)
    if task is not None:
        _record_request_event("inflight_waits")
        return await _await_request_task(key, task)

    try:
        audio_bytes = await _read_audio(audio)
    except HTTPException as exc:
        if exc.status_code == 413:
            response = _error_watch_response(request_id, "语音太长，请说短一点")
            return _record_request_result(device_id, request_id, response, time.monotonic(), MAX_AUDIO_BYTES + 1)
        raise
    if not audio_bytes:
        response = _error_watch_response(request_id, "没有收到音频，请再说一次")
        return _record_request_result(device_id, request_id, response, time.monotonic(), 0)
    async with _request_lock:
        if key in _completed_requests:
            _record_request_event("cache_hits")
            return _completed_requests[key]
        if key in _canceled_requests:
            _record_request_event("canceled_hits")
            return _canceled_watch_response(request_id)
        task = _inflight_requests.get(key)
        if task is None:
            task = asyncio.create_task(
                _process_voice_command(
                    request_id=request_id,
                    device_id=device_id,
                    audio_bytes=audio_bytes,
                    audio_content_type=audio.content_type,
                    clarification_id=clarification_id,
                    mock_asr_text=mock_asr_text,
                )
            )
            _inflight_requests[key] = task
    return await _await_request_task(key, task)


@app.post("/v1/watch/text-command", response_model=WatchResponse)
async def text_command(
    request_id: str = Form(...),
    device_id: str = Form(...),
    text: str = Form(...),
    clarification_id: str | None = Form(default=None),
    battery_percent: int | None = Form(default=None),
    charging: bool | None = Form(default=None),
    rssi: int | None = Form(default=None),
    firmware_version: str | None = Form(default=None),
    locale: str = Form("zh-CN"),
    timezone: str = Form("Asia/Shanghai"),
    source: str = Form("watch_hermes_page"),
    ui_state: str = Form("ready"),
    authorization: str | None = Header(default=None),
) -> WatchResponse:
    _require_device(device_id, authorization, "text_command")
    del battery_percent, charging, rssi, firmware_version, locale, timezone, source, ui_state
    normalized_request_id = _normalize_request_id(request_id)
    if normalized_request_id is None:
        response = _error_watch_response(INVALID_REQUEST_ID, "请求编号异常，请重新发送")
        return _record_request_result(device_id, INVALID_REQUEST_ID, response, time.monotonic(), 0)
    request_id = normalized_request_id

    normalized_text = text.strip()
    if not normalized_text:
        response = _error_watch_response(request_id, "没有收到文本，请重新发送")
        return _record_request_result(device_id, request_id, response, time.monotonic(), 0)
    if len(normalized_text) > MAX_TEXT_CHARS:
        response = _error_watch_response(request_id, "文本太长，请发短一点")
        return _record_request_result(device_id, request_id, response, time.monotonic(), 0)

    key = (device_id, request_id)
    should_count_inflight_wait = False
    async with _request_lock:
        if key in _completed_requests:
            _record_request_event("cache_hits")
            return _completed_requests[key]
        if key in _canceled_requests:
            _record_request_event("canceled_hits")
            return _canceled_watch_response(request_id)
        task = _inflight_requests.get(key)
        if task is None:
            task = asyncio.create_task(
                _process_text_command(
                    request_id=request_id,
                    device_id=device_id,
                    text=normalized_text,
                    clarification_id=clarification_id,
                )
            )
            _inflight_requests[key] = task
        else:
            should_count_inflight_wait = True
    if should_count_inflight_wait:
        _record_request_event("inflight_waits")
    return await _await_request_task(key, task)


@app.post("/v1/watch/request/{request_id}/cancel", response_model=WatchResponse)
async def cancel_request(
    request_id: str,
    device_id: str = Form(...),
    authorization: str | None = Header(default=None),
) -> WatchResponse:
    _require_device(device_id, authorization, "cancel")
    normalized_request_id = _normalize_request_id(request_id)
    if normalized_request_id is None:
        response = _error_watch_response(INVALID_REQUEST_ID, "请求编号异常，请重新发送")
        return _record_request_result(device_id, INVALID_REQUEST_ID, response, time.monotonic(), 0)
    request_id = normalized_request_id
    key = (device_id, request_id)
    should_record_cancel = False
    async with _request_lock:
        if key in _completed_requests:
            return _completed_requests[key]
        _canceled_requests.add(key)
        _trim_canceled_requests()
        task = _inflight_requests.get(key)
        if task is not None and not task.done():
            task.cancel()
        if task is None:
            should_record_cancel = True
    response = _canceled_watch_response(request_id)
    if should_record_cancel:
        _record_request_result(device_id, request_id, response, time.monotonic(), 0)
    return response


@app.get("/health")
async def service_health() -> dict[str, object]:
    return {
        "status": "ok",
        "time": int(time.time()),
        "asr_provider": ASR_PROVIDER,
        "request_timeout_seconds": WATCH_REQUEST_TIMEOUT_SECONDS,
        "inflight_requests": len(_inflight_requests),
        "completed_requests": len(_completed_requests),
        "canceled_requests": len(_canceled_requests),
        "request_events": dict(_request_event_counts),
        "request_status_counts": dict(_request_status_counts),
        "auth_failures": dict(_auth_failure_counts),
        "websocket_enabled": WATCH_WS_ENABLED,
        "request_error_counts": dict(_request_error_counts),
        "last_request": dict(_last_request_summary) if _last_request_summary else None,
        "last_auth_failure": (
            dict(_last_auth_failure_summary) if _last_auth_failure_summary else None
        ),
    }


# ── Inbox Pydantic 响应模型 ──────────────────────────────────────────────────

class InboxItemOut(BaseModel):
    notification_id: str
    source: str
    kind: str
    created_at: str
    title: str
    preview: str
    body: str
    read: bool


class InboxCreateResponse(BaseModel):
    created: bool
    item: InboxItemOut


class InboxListResponse(BaseModel):
    items: list[InboxItemOut]
    unread_count: int


class InboxMarkReadResponse(BaseModel):
    read: bool
    notification_id: str


# ── Inbox 路由（薄适配层，所有校验与幂等逻辑在 inbox_repo.py）──────────────

@app.post("/v1/watch/inbox", status_code=201)
async def inbox_create(
    request: Request,
    device_id: str = Query(...),
    authorization: str | None = Header(default=None),
) -> InboxCreateResponse:
    """
    Hermes 主动提示写入。调用方：hermes_server。
    首次创建返回 201 + created=true；相同 device_id+notification_id 重复调用返回 200 + created=false。
    FastAPI 不支持在同一函数同时返回 201/200，因此用 JSONResponse 承载状态码差异。
    """
    from fastapi.responses import JSONResponse

    _require_device(device_id, authorization, "inbox_create")
    try:
        body = await request.json()
    except Exception:
        raise HTTPException(status_code=422, detail="request body must be a JSON object")
    if not isinstance(body, dict):
        raise HTTPException(status_code=422, detail="request body must be a JSON object")

    notification_id = str(body.get("notification_id") or "")
    kind = str(body.get("kind") or "")
    title = str(body.get("title") or "")
    preview = str(body.get("preview") or "")
    body_text = str(body.get("body") or "")

    try:
        result = _get_inbox_repo().create(
            device_id=device_id,
            notification_id=notification_id,
            kind=kind,
            title=title,
            preview=preview,
            body=body_text,
        )
    except InboxValidationError as e:
        raise HTTPException(status_code=422, detail=str(e))

    item_out = InboxItemOut(**asdict(result.item))
    payload = InboxCreateResponse(created=result.created, item=item_out)
    status_code = 201 if result.created else 200
    return JSONResponse(content=payload.model_dump(), status_code=status_code)


@app.get("/v1/watch/inbox", response_model=InboxListResponse)
async def inbox_list(
    device_id: str = Query(...),
    authorization: str | None = Header(default=None),
) -> InboxListResponse:
    """手表轮询拉取最近 20 条完整快照；空列表返回 200 + items=[] + unread_count=0。"""
    _require_device(device_id, authorization, "inbox_list")
    items, unread = _get_inbox_repo().list_items(device_id)
    return InboxListResponse(
        items=[InboxItemOut(**asdict(i)) for i in items],
        unread_count=unread,
    )


@app.post("/v1/watch/inbox/{notification_id}/read", response_model=InboxMarkReadResponse)
async def inbox_mark_read(
    notification_id: str,
    device_id: str = Query(...),
    authorization: str | None = Header(default=None),
) -> InboxMarkReadResponse:
    """幂等标记已读；目标不存在或已被 20 条上限淘汰时返回 404。"""
    _require_device(device_id, authorization, "inbox_mark_read")
    item = _get_inbox_repo().mark_read(device_id, notification_id)
    if item is None:
        raise HTTPException(status_code=404, detail="notification_not_found")
    return InboxMarkReadResponse(read=True, notification_id=notification_id)
