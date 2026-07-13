from __future__ import annotations

import asyncio
import base64
import hashlib
import hmac
import json
import logging
import os
import re
import time
import uuid
from contextlib import asynccontextmanager
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Literal

import httpx
from fastapi import FastAPI, File, Form, Header, HTTPException, Query, Request, UploadFile, WebSocket
from pydantic import BaseModel
from starlette.websockets import WebSocketDisconnect

from conversation_repo import ConversationMessage, ConversationRepo, ConversationValidationError
from inbox_repo import InboxRepo, InboxValidationError
from session_repo import TERMINAL_STATES, SessionRepo, SessionValidationError, WatchSession


HERMES_API_URL = os.getenv("HERMES_API_URL", "http://127.0.0.1:8642").rstrip("/")
HERMES_API_KEY = os.getenv("HERMES_API_KEY", "")
HERMES_MODEL = os.getenv("HERMES_MODEL", "hermes-agent")
HERMES_TIMEOUT_SECONDS = float(os.getenv("HERMES_TIMEOUT_SECONDS", "120"))
HERMES_RUN_TIMEOUT_SECONDS = float(os.getenv("HERMES_RUN_TIMEOUT_SECONDS", "3600"))
HERMES_RUN_POLL_INTERVAL_SECONDS = float(
    os.getenv("HERMES_RUN_POLL_INTERVAL_SECONDS", "1")
)
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
WATCH_REPLY_MAX_UTF8_BYTES = int(os.getenv("WATCH_REPLY_MAX_UTF8_BYTES", "120"))
WATCH_ASR_MAX_UTF8_BYTES = int(os.getenv("WATCH_ASR_MAX_UTF8_BYTES", "255"))
WATCH_INTERNAL_API_KEY = os.getenv("WATCH_INTERNAL_API_KEY", "")
WATCH_PUBLIC_INBOX_CREATE_ENABLED = os.getenv(
    "WATCH_PUBLIC_INBOX_CREATE_ENABLED", "false"
).strip().lower() in ("1", "true", "yes", "on")
WATCH_HTTP_ASYNC_RUNS_ENABLED = os.getenv(
    "WATCH_HTTP_ASYNC_RUNS_ENABLED", "true"
).strip().lower() in ("1", "true", "yes", "on")
WATCH_WS_ENABLED = os.getenv("WATCH_WS_ENABLED", "false").strip().lower() in ("1", "true", "yes", "on")
WATCH_WS_MAX_MESSAGE_BYTES = int(os.getenv("WATCH_WS_MAX_MESSAGE_BYTES", str(6 * 1024 * 1024)))
MIMO_ASR_DIRECT_MIME_TYPES = {"audio/wav", "audio/mp3", "audio/mpeg"}
REQUEST_ID_PATTERN = re.compile(r"^[A-Za-z0-9._:-]{1,96}$")
INVALID_REQUEST_ID = "invalid-request"
logger = logging.getLogger("watch_voice_endpoint")

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


class DangerAlertIn(BaseModel):
    type: str = "danger_alert"
    event_id: str | None = None
    device_id: str = "watch-001"
    danger_type: str = "danger"
    danger_prob: float | None = None
    probability: float | None = None
    occurred_at: str | None = None
    message: str | None = None


_hermes_run_client: httpx.AsyncClient | None = None


@asynccontextmanager
async def _app_lifespan(_: FastAPI):
    """关闭进程级 Hermes 连接池，避免 reload/shutdown 遗留连接。"""
    try:
        yield
    finally:
        global _hermes_run_client
        if _hermes_run_client is not None:
            await _hermes_run_client.aclose()
            _hermes_run_client = None


app = FastAPI(
    title="AI Memory Watch Endpoint",
    version="0.1.0",
    lifespan=_app_lifespan,
)
_canceled_requests: set[tuple[str, str]] = set()
_completed_requests: dict[tuple[str, str], WatchResponse] = {}
_inflight_requests: dict[tuple[str, str], asyncio.Task[WatchResponse]] = {}

# SQLite inbox repository；数据库路径可通过环境变量覆盖，便于测试隔离。
_INBOX_DB_PATH = Path(os.getenv("INBOX_DB_PATH", "/data/inbox.db"))
_CONVERSATION_DB_PATH = Path(os.getenv("CONVERSATION_DB_PATH", "/data/conversation.db"))
_SESSION_DB_PATH = Path(os.getenv("SESSION_DB_PATH", "/data/session.db"))
_inbox_repo: InboxRepo | None = None
_conversation_repo: ConversationRepo | None = None
_session_repo: SessionRepo | None = None
_ws_background_tasks: set[asyncio.Task[None]] = set()
_watch_run_tasks: dict[tuple[str, str], asyncio.Task[None]] = {}
_device_run_locks: dict[str, asyncio.Lock] = {}
_alert_ws_clients: set[WebSocket] = set()
_alert_ws_lock = asyncio.Lock()


@dataclass
class WsConnectionState:
    """单条 watch WebSocket 的 best-effort 推送状态。"""

    websocket: WebSocket
    send_lock: asyncio.Lock
    connected: bool = True


@dataclass
class WsRequestMetrics:
    """单次 WS turn 的非秘密分段耗时；只保留最近一次用于现场诊断。"""

    device_id: str
    request_id: str
    upload_bytes: int = 0
    upload_ms: int = 0
    asr_ms: int = 0
    queue_wait_ms: int = 0
    hermes_ms: int = 0
    persist_ms: int = 0
    delivery_mode: str = "sync"
    terminal_state: str = "running"
    error_stage: str | None = None
    error_code: str | None = None
    recorded: bool = False


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


def _get_session_repo() -> SessionRepo:
    """懒初始化 watch_session store；V2.3 任务状态真相源。"""
    global _session_repo
    if _session_repo is None:
        _SESSION_DB_PATH.parent.mkdir(parents=True, exist_ok=True)
        _session_repo = SessionRepo(_SESSION_DB_PATH)
    return _session_repo


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
_ws_request_status_counts: dict[str, int] = {}
_last_ws_request_summary: dict[str, str | int | None] = {}
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


def _require_internal(authorization: str | None) -> None:
    """校验 server-to-server credential；该 key 不下发给 ESP32。"""
    if not WATCH_INTERNAL_API_KEY:
        raise HTTPException(status_code=503, detail="internal_api_key_not_configured")
    prefix = "Bearer "
    if not authorization or not authorization.startswith(prefix):
        raise HTTPException(status_code=401, detail="missing_internal_token")
    if not hmac.compare_digest(
        authorization[len(prefix) :], WATCH_INTERNAL_API_KEY
    ):
        raise HTTPException(status_code=403, detail="invalid_internal_token")


def _hermes_headers() -> dict[str, str]:
    if not HERMES_API_KEY:
        raise HTTPException(status_code=500, detail="hermes_api_key_missing")
    return {"Authorization": f"Bearer {HERMES_API_KEY}"}


def _get_hermes_run_client() -> httpx.AsyncClient:
    """复用 Hermes run 轮询连接；请求级 timeout 仍由各调用点控制。"""
    global _hermes_run_client
    if _hermes_run_client is None or _hermes_run_client.is_closed:
        _hermes_run_client = httpx.AsyncClient(trust_env=False)
    return _hermes_run_client


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


def _truncate_utf8(text: str, max_bytes: int) -> str:
    """按 UTF-8 字节上限截断，绝不返回半个多字节字符。"""
    if max_bytes <= 0:
        return ""
    encoded = text.encode("utf-8")
    if len(encoded) <= max_bytes:
        return text
    return encoded[:max_bytes].decode("utf-8", errors="ignore")


def _watch_reply_text(text: str) -> str:
    return _truncate_utf8(text[:REPLY_MAX_CHARS], WATCH_REPLY_MAX_UTF8_BYTES)


def _watch_asr_text(text: str) -> str:
    return _truncate_utf8(text, WATCH_ASR_MAX_UTF8_BYTES)


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
        asr_text=_watch_asr_text(asr_text),
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
        asr_text=_watch_asr_text(asr_text),
        reply_text=_watch_reply_text(reply_text),
        error_code="asr_or_agent_error",
    )


def _timeout_watch_response(request_id: str, asr_text: str = "") -> WatchResponse:
    return WatchResponse(
        request_id=request_id,
        status="timeout",
        action="error",
        asr_text=_watch_asr_text(asr_text),
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


def _record_ws_request_metrics(metrics: WsRequestMetrics) -> None:
    """发布 WS 主链路指标，不包含用户文本、音频内容或鉴权信息。"""
    if metrics.recorded:
        return
    metrics.recorded = True
    _ws_request_status_counts[metrics.terminal_state] = (
        _ws_request_status_counts.get(metrics.terminal_state, 0) + 1
    )
    _last_ws_request_summary.clear()
    _last_ws_request_summary.update(
        {
            "device_id": metrics.device_id,
            "request_id": metrics.request_id,
            "upload_bytes": metrics.upload_bytes,
            "upload_ms": metrics.upload_ms,
            "asr_ms": metrics.asr_ms,
            "queue_wait_ms": metrics.queue_wait_ms,
            "hermes_ms": metrics.hermes_ms,
            "persist_ms": metrics.persist_ms,
            "delivery_mode": metrics.delivery_mode,
            "terminal_state": metrics.terminal_state,
            "error_stage": metrics.error_stage,
            "error_code": metrics.error_code,
            "completed_at": int(time.time()),
        }
    )


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


def _hermes_run_history(device_id: str, request_id: str) -> list[dict[str, str]]:
    """构建 `/v1/runs` 的短历史，排除本次已单独作为 input 的 user 消息。"""
    history: list[dict[str, str]] = []
    for message in _get_conversation_repo().list_recent(device_id):
        if message.request_id == request_id and message.role == "user":
            continue
        history.append({"role": message.role, "content": message.text})
    return history


async def _start_hermes_run(device_id: str, request_id: str, asr_text: str) -> str:
    """启动 Hermes 原生异步 run，立即返回可轮询 run_id。"""
    body = {
        "model": HERMES_MODEL,
        "instructions": (
            f"{WATCH_INSTRUCTIONS}\n当前 watch request_id：{request_id}。"
            "工具如支持幂等键，应使用该 request_id。"
        ),
        "input": f"手表用户说：{asr_text}",
        "conversation_history": _hermes_run_history(device_id, request_id),
        "session_id": _conversation_for_device(device_id),
    }
    timeout = httpx.Timeout(connect=10.0, write=30.0, read=30.0, pool=10.0)
    response = await _get_hermes_run_client().post(
        f"{HERMES_API_URL}/v1/runs",
        headers={**_hermes_headers(), "Idempotency-Key": request_id},
        json=body,
        timeout=timeout,
    )
    response.raise_for_status()
    payload = response.json()
    run_id = str(payload.get("run_id") or "")
    if response.status_code != 202 or not run_id:
        raise RuntimeError("hermes_run_not_accepted")
    return run_id


async def _get_hermes_run(run_id: str) -> dict[str, object]:
    timeout = httpx.Timeout(connect=10.0, write=10.0, read=15.0, pool=10.0)
    response = await _get_hermes_run_client().get(
        f"{HERMES_API_URL}/v1/runs/{run_id}",
        headers=_hermes_headers(),
        timeout=timeout,
    )
    response.raise_for_status()
    payload = response.json()
    if not isinstance(payload, dict):
        raise RuntimeError("invalid_hermes_run_status")
    return payload


async def _stop_hermes_run(run_id: str) -> None:
    timeout = httpx.Timeout(connect=10.0, write=10.0, read=15.0, pool=10.0)
    response = await _get_hermes_run_client().post(
        f"{HERMES_API_URL}/v1/runs/{run_id}/stop",
        headers=_hermes_headers(),
        timeout=timeout,
    )
    if response.status_code not in (200, 404):
        response.raise_for_status()


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
            asr_text=_watch_asr_text(asr_text),
            reply_text="没有处理成功，请再说一次",
            error_code="asr_or_agent_error",
        )
    if not reply_text:
        return WatchResponse(
            request_id=request_id,
            status="error",
            action="error",
            asr_text=_watch_asr_text(asr_text),
            reply_text="没有处理成功，请再说一次",
            error_code="asr_or_agent_error",
        )

    if key in _canceled_requests:
        return _canceled_watch_response(request_id, asr_text)

    return WatchResponse(
        request_id=request_id,
        status="done",
        action=_infer_action(asr_text, reply_text),
        asr_text=_watch_asr_text(asr_text),
        reply_text=_watch_reply_text(reply_text),
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
            asr_text=_watch_asr_text(text),
            reply_text="没有处理成功，请再发送一次",
            error_code="asr_or_agent_error",
        )
    if not reply_text:
        return WatchResponse(
            request_id=request_id,
            status="error",
            action="error",
            asr_text=_watch_asr_text(text),
            reply_text="没有处理成功，请再发送一次",
            error_code="asr_or_agent_error",
        )

    if key in _canceled_requests:
        return _canceled_watch_response(request_id, text)

    return WatchResponse(
        request_id=request_id,
        status="done",
        action=_infer_action(text, reply_text),
        asr_text=_watch_asr_text(text),
        reply_text=_watch_reply_text(reply_text),
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
    if WATCH_HTTP_ASYNC_RUNS_ENABLED:
        await _ws_finish_audio(
            None,
            device_id,
            request_id,
            audio_bytes,
            mock_asr_text,
        )
        response = await _await_compat_session(
            device_id,
            request_id,
            fallback_user_text="",
        )
    else:
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
    if WATCH_HTTP_ASYNC_RUNS_ENABLED:
        await _ws_finish_text(None, device_id, request_id, text)
        response = await _await_compat_session(
            device_id,
            request_id,
            fallback_user_text=text,
        )
    else:
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
        cache_response = response.status != "canceled"
        if response.status == "timeout":
            try:
                session = _get_session_repo().get_by_request_id(device_id, request_id)
                cache_response = session.state in TERMINAL_STATES
            except SessionValidationError:
                pass
        if cache_response:
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


async def _ws_try_send_json(
    conn: WsConnectionState | None,
    payload: dict[str, object],
) -> bool:
    """向仍在线的 WS 连接 best-effort 推送；断线不影响后台任务落库。"""
    if conn is None or not conn.connected:
        return False
    async with conn.send_lock:
        if not conn.connected:
            return False
        try:
            await _ws_send_json(conn.websocket, payload)
            return True
        except Exception:
            conn.connected = False
            return False


async def _ws_try_send_error(
    conn: WsConnectionState | None,
    request_id: str | None,
    code: str,
) -> None:
    payload: dict[str, object] = {"type": "error", "error_code": code}
    if request_id:
        payload["request_id"] = request_id
    await _ws_try_send_json(conn, payload)


def _session_replay_message(session: WatchSession) -> ConversationMessage | None:
    """从 session 重建已淘汰的 terminal reply，不重新执行 Hermes。"""
    if not session.reply_text:
        return None
    message_id = session.last_delivered_message_id
    if not message_id:
        digest = hashlib.sha256(
            f"{session.device_id}:{session.request_id}".encode("utf-8")
        ).hexdigest()[:24]
        message_id = f"msg_replay_{digest}"
    status = session.state if session.state in ("done", "error", "timeout", "canceled") else "error"
    return ConversationMessage(
        message_id=message_id,
        request_id=session.request_id,
        role="assistant",
        text=session.reply_text,
        created_at=session.updated_at,
        status=status,
    )


async def _replay_terminal_session(
    conn: WsConnectionState | None,
    session: WatchSession,
) -> None:
    message = _get_conversation_repo().get_for_request_role(
        session.device_id, session.request_id, "assistant"
    )
    if message is None:
        message = _session_replay_message(session)
    if message is not None:
        await _ws_try_send_json(
            conn,
            {"type": "conversation_message", **_conversation_message_payload(message)},
        )
        return
    await _ws_try_send_error(
        conn,
        session.request_id,
        session.error_code or f"session_{session.state}",
    )


def _watch_run_task_done(
    key: tuple[str, str],
    task: asyncio.Task[None],
) -> None:
    _ws_background_tasks.discard(task)
    if _watch_run_tasks.get(key) is task:
        _watch_run_tasks.pop(key, None)


def _track_watch_run_task(
    device_id: str,
    request_id: str,
    task: asyncio.Task[None],
) -> bool:
    key = (device_id, request_id)
    existing = _watch_run_tasks.get(key)
    if existing is not None and not existing.done():
        task.cancel()
        return False
    _watch_run_tasks[key] = task
    _ws_background_tasks.add(task)
    task.add_done_callback(lambda finished: _watch_run_task_done(key, finished))
    return True


async def _finish_ws_session_error(
    conn: WsConnectionState | None,
    device_id: str,
    request_id: str,
    error_code: str,
    metrics: WsRequestMetrics | None = None,
    error_stage: str = "hermes",
) -> None:
    """只有抢到 running -> error 的 caller 才写错误消息。"""
    session_repo = _get_session_repo()
    reply_text = "没有处理成功，请再说一次"
    message_id = f"msg_{uuid.uuid4().hex}"
    persist_started_at = time.monotonic()
    try:
        session_repo.transition(
            device_id,
            request_id,
            "error",
            reply_text=reply_text,
            last_delivered_message_id=message_id,
            error_code=error_code,
        )
    except SessionValidationError:
        return
    try:
        message = _get_conversation_repo().add_message_once(
            device_id=device_id,
            request_id=request_id,
            role="assistant",
            text=reply_text,
            status="error",
            message_id=message_id,
        )
        if metrics is not None:
            metrics.persist_ms = int(
                (time.monotonic() - persist_started_at) * 1000
            )
        delivered = await _ws_try_send_json(
            conn,
            {"type": "conversation_message", **_conversation_message_payload(message)},
        )
        if metrics is not None:
            metrics.delivery_mode = "ws" if delivered else "sync"
            metrics.terminal_state = "error"
            metrics.error_stage = error_stage
            metrics.error_code = error_code
            _record_ws_request_metrics(metrics)
    except ConversationValidationError:
        await _ws_try_send_error(conn, request_id, error_code)


async def _ws_run_hermes_job(
    conn: WsConnectionState | None,
    device_id: str,
    request_id: str,
    asr_text: str,
    existing_run_id: str = "",
    metrics: WsRequestMetrics | None = None,
) -> None:
    """按 device 串行 Hermes conversation，避免同一历史并发乱序。"""
    request_metrics = metrics or WsRequestMetrics(
        device_id=device_id,
        request_id=request_id,
    )
    queued_at = time.monotonic()
    device_lock = _device_run_locks.setdefault(device_id, asyncio.Lock())
    async with device_lock:
        request_metrics.queue_wait_ms = int((time.monotonic() - queued_at) * 1000)
        await _ws_run_hermes_job_serialized(
            conn,
            device_id,
            request_id,
            asr_text,
            existing_run_id=existing_run_id,
            metrics=request_metrics,
        )


async def _ws_run_hermes_job_serialized(
    conn: WsConnectionState | None,
    device_id: str,
    request_id: str,
    asr_text: str,
    existing_run_id: str = "",
    metrics: WsRequestMetrics | None = None,
) -> None:
    """启动或重新挂接 Hermes run，结果先落 session 再写 conversation。"""
    session_repo = _get_session_repo()
    run_id = existing_run_id
    started_at = time.monotonic()
    try:
        session = session_repo.get(device_id, request_id)
        if session.state == "asr_ready":
            session = session_repo.transition(device_id, request_id, "running")
        elif session.state != "running":
            return

        await _ws_try_send_json(conn, {"type": "task_started", "request_id": request_id})
        if not run_id:
            run_id = await _start_hermes_run(device_id, request_id, asr_text)
            try:
                session_repo.attach_hermes_run(device_id, request_id, run_id)
            except SessionValidationError:
                await _stop_hermes_run(run_id)
                return

        while True:
            if time.monotonic() - started_at >= HERMES_RUN_TIMEOUT_SECONDS:
                await _stop_hermes_run(run_id)
                await _finish_ws_session_error(
                    conn,
                    device_id,
                    request_id,
                    "hermes_run_timeout",
                    metrics,
                )
                return

            payload = await _get_hermes_run(run_id)
            status = str(payload.get("status") or "")
            if status == "completed":
                reply_text = str(payload.get("output") or "").strip()
                if not reply_text:
                    raise RuntimeError("empty_hermes_reply")
                reply_text = _watch_reply_text(reply_text)
                message_id = f"msg_{uuid.uuid4().hex}"
                persist_started_at = time.monotonic()
                try:
                    session_repo.transition(
                        device_id,
                        request_id,
                        "done",
                        reply_text=reply_text,
                        last_delivered_message_id=message_id,
                    )
                except SessionValidationError:
                    return
                message = _get_conversation_repo().add_message_once(
                    device_id=device_id,
                    request_id=request_id,
                    role="assistant",
                    text=reply_text,
                    status="done",
                    message_id=message_id,
                )
                if metrics is not None:
                    metrics.hermes_ms = int(
                        (persist_started_at - started_at) * 1000
                    )
                    metrics.persist_ms = int(
                        (time.monotonic() - persist_started_at) * 1000
                    )
                delivered = await _ws_try_send_json(
                    conn,
                    {"type": "conversation_message", **_conversation_message_payload(message)},
                )
                if metrics is not None:
                    metrics.delivery_mode = "ws" if delivered else "sync"
                    metrics.terminal_state = "done"
                    _record_ws_request_metrics(metrics)
                return
            if status == "failed":
                await _finish_ws_session_error(
                    conn,
                    device_id,
                    request_id,
                    "hermes_run_failed",
                    metrics,
                )
                return
            if status == "cancelled":
                try:
                    session_repo.transition(
                        device_id, request_id, "canceled", error_code="request_canceled"
                    )
                except SessionValidationError:
                    pass
                if metrics is not None:
                    metrics.hermes_ms = int(
                        (time.monotonic() - started_at) * 1000
                    )
                    metrics.terminal_state = "canceled"
                    metrics.error_stage = "hermes"
                    metrics.error_code = "request_canceled"
                    _record_ws_request_metrics(metrics)
                return
            if status not in ("queued", "running", "waiting_for_approval", "stopping"):
                raise RuntimeError(f"unknown_hermes_run_status:{status}")
            await asyncio.sleep(HERMES_RUN_POLL_INTERVAL_SECONDS)
    except asyncio.CancelledError:
        if metrics is not None:
            metrics.hermes_ms = int((time.monotonic() - started_at) * 1000)
            metrics.terminal_state = "canceled"
            metrics.error_stage = "hermes"
            metrics.error_code = "request_canceled"
            _record_ws_request_metrics(metrics)
        raise
    except httpx.HTTPStatusError as exc:
        code = "hermes_run_not_found" if exc.response.status_code == 404 else "hermes_http_error"
        logger.warning(
            "Hermes run HTTP failure request_id=%s status=%s",
            request_id,
            exc.response.status_code,
        )
        if code == "hermes_run_not_found":
            try:
                session_repo.transition(
                    device_id,
                    request_id,
                    "interrupted",
                    error_code="hermes_run_lost",
                )
            except SessionValidationError:
                pass
            if metrics is not None:
                metrics.hermes_ms = int((time.monotonic() - started_at) * 1000)
                metrics.terminal_state = "interrupted"
                metrics.error_stage = "hermes"
                metrics.error_code = "hermes_run_lost"
                _record_ws_request_metrics(metrics)
            return
        await _finish_ws_session_error(
            conn, device_id, request_id, code, metrics
        )
    except Exception as exc:
        logger.warning(
            "Hermes run failure request_id=%s stage=run error=%s",
            request_id,
            type(exc).__name__,
        )
        await _finish_ws_session_error(
            conn,
            device_id,
            request_id,
            "hermes_run_error",
            metrics,
        )


async def _ws_finish_audio(
    conn: WsConnectionState | None,
    device_id: str,
    request_id: str,
    audio_bytes: bytes | bytearray,
    mock_asr_text: str | None,
    upload_ms: int = 0,
) -> None:
    metrics = WsRequestMetrics(
        device_id=device_id,
        request_id=request_id,
        upload_bytes=len(audio_bytes),
        upload_ms=upload_ms,
    )
    if not audio_bytes:
        await _ws_try_send_error(conn, request_id, "empty_audio")
        metrics.terminal_state = "error"
        metrics.error_stage = "upload"
        metrics.error_code = "empty_audio"
        _record_ws_request_metrics(metrics)
        return

    session_repo = _get_session_repo()
    session_id = request_id  # V2.3: session_id == request_id

    existing, created = session_repo.create_or_get(
        device_id=device_id,
        session_id=session_id,
        request_id=request_id,
    )
    if not created:
        if existing.state in TERMINAL_STATES:
            await _replay_terminal_session(conn, existing)
        else:
            await _ws_try_send_error(conn, request_id, "duplicate_request")
        return

    try:
        asr_started_at = time.monotonic()
        asr_text = await _transcribe_audio(audio_bytes, "audio/ogg", mock_asr_text)
        metrics.asr_ms = int((time.monotonic() - asr_started_at) * 1000)
        if not asr_text:
            await _ws_try_send_error(conn, request_id, "empty_asr_text")
            try:
                session_repo.transition(
                    device_id, session_id, "error", error_code="empty_asr_text"
                )
            except SessionValidationError:
                pass
            metrics.terminal_state = "error"
            metrics.error_stage = "asr"
            metrics.error_code = "empty_asr_text"
            _record_ws_request_metrics(metrics)
            return

        session_repo.transition(
            device_id, session_id, "asr_ready", user_text=asr_text
        )
        device_asr_text = _watch_asr_text(asr_text)

        message = _get_conversation_repo().add_message_once(
            device_id=device_id,
            request_id=request_id,
            role="user",
            text=device_asr_text,
            status="done",
        )
        await _ws_try_send_json(
            conn,
            {
                "type": "asr_result",
                "request_id": request_id,
                "message_id": message.message_id,
                "text": device_asr_text,
            },
        )
        task = asyncio.create_task(
            _ws_run_hermes_job(
                conn,
                device_id,
                request_id,
                asr_text,
                metrics=metrics,
            )
        )
        _track_watch_run_task(device_id, request_id, task)
    except ConversationValidationError:
        try:
            if session_repo.get(device_id, session_id).state == "canceled":
                return
        except SessionValidationError:
            pass
        try:
            session_repo.transition(
                device_id,
                session_id,
                "error",
                error_code="conversation_store_error",
            )
        except SessionValidationError:
            pass
        await _ws_try_send_error(conn, request_id, "conversation_store_error")
        metrics.terminal_state = "error"
        metrics.error_stage = "persist"
        metrics.error_code = "conversation_store_error"
        _record_ws_request_metrics(metrics)
    except Exception as exc:
        logger.warning(
            "Watch WS request failure request_id=%s stage=asr error=%s",
            request_id,
            type(exc).__name__,
        )
        try:
            if session_repo.get(device_id, session_id).state == "canceled":
                return
        except SessionValidationError:
            pass
        try:
            session_repo.transition(
                device_id, session_id, "error", error_code="asr_or_agent_error"
            )
        except SessionValidationError:
            pass
        await _ws_try_send_error(conn, request_id, "asr_or_agent_error")
        metrics.terminal_state = "error"
        metrics.error_stage = "asr"
        metrics.error_code = "asr_or_agent_error"
        _record_ws_request_metrics(metrics)


async def _ws_finish_text(
    conn: WsConnectionState | None,
    device_id: str,
    request_id: str,
    text: str,
) -> None:
    """让 HTTP text compatibility 与 WS 共用 session claim 和 Hermes run。"""
    metrics = WsRequestMetrics(device_id=device_id, request_id=request_id)
    session_repo = _get_session_repo()
    existing, created = session_repo.create_or_get(
        device_id=device_id,
        session_id=request_id,
        request_id=request_id,
    )
    if not created:
        if existing.state in TERMINAL_STATES:
            await _replay_terminal_session(conn, existing)
        return

    try:
        session_repo.transition(
            device_id,
            request_id,
            "asr_ready",
            user_text=text,
        )
        _get_conversation_repo().add_message_once(
            device_id=device_id,
            request_id=request_id,
            role="user",
            text=_watch_asr_text(text),
            status="done",
        )
        task = asyncio.create_task(
            _ws_run_hermes_job(
                conn,
                device_id,
                request_id,
                text,
                metrics=metrics,
            )
        )
        _track_watch_run_task(device_id, request_id, task)
    except (ConversationValidationError, SessionValidationError) as exc:
        logger.warning(
            "Watch text request failure request_id=%s stage=persist error=%s",
            request_id,
            type(exc).__name__,
        )
        try:
            session_repo.transition(
                device_id,
                request_id,
                "error",
                error_code="conversation_store_error",
            )
        except SessionValidationError:
            pass
        metrics.terminal_state = "error"
        metrics.error_stage = "persist"
        metrics.error_code = "conversation_store_error"
        _record_ws_request_metrics(metrics)


def _watch_response_from_session(
    session: WatchSession,
    fallback_user_text: str,
) -> WatchResponse:
    user_text = session.user_text or fallback_user_text
    if session.state == "done":
        return WatchResponse(
            request_id=session.request_id,
            status="done",
            action=_infer_action(user_text, session.reply_text),
            asr_text=_watch_asr_text(user_text),
            reply_text=_watch_reply_text(session.reply_text),
            error_code=None,
        )
    if session.state == "canceled":
        return _canceled_watch_response(session.request_id, user_text)
    if session.state in ("error", "interrupted"):
        return WatchResponse(
            request_id=session.request_id,
            status="error",
            action="error",
            asr_text=_watch_asr_text(user_text),
            reply_text=_watch_reply_text(
                session.reply_text or "没有处理成功，请再说一次"
            ),
            error_code=session.error_code or f"session_{session.state}",
        )
    if session.state == "timeout":
        return _timeout_watch_response(session.request_id, user_text)
    return _timeout_watch_response(session.request_id, user_text)


async def _await_compat_session(
    device_id: str,
    request_id: str,
    fallback_user_text: str,
) -> WatchResponse:
    """HTTP caller 最多等待 115 秒；超时不取消持久化 Hermes run。"""
    try:
        session = _get_session_repo().get_by_request_id(device_id, request_id)
    except SessionValidationError:
        return _error_watch_response(request_id)

    _ensure_watch_session_task(session)
    task = _watch_run_tasks.get((device_id, request_id))
    if task is not None and not task.done():
        try:
            await asyncio.wait_for(
                asyncio.shield(task),
                timeout=WATCH_REQUEST_TIMEOUT_SECONDS,
            )
        except asyncio.TimeoutError:
            return _timeout_watch_response(
                request_id,
                session.user_text or fallback_user_text,
            )
        except asyncio.CancelledError:
            pass

    try:
        session = _get_session_repo().get_by_request_id(device_id, request_id)
    except SessionValidationError:
        return _error_watch_response(request_id)
    return _watch_response_from_session(session, fallback_user_text)


def _ws_track_background_task(task: asyncio.Task[None]) -> None:
    _ws_background_tasks.add(task)
    task.add_done_callback(_ws_background_tasks.discard)


def _ensure_watch_session_task(
    session: WatchSession,
    conn: WsConnectionState | None = None,
) -> None:
    """为可恢复 session 补建本进程轮询 task；重复调用无副作用。"""
    key = (session.device_id, session.request_id)
    existing = _watch_run_tasks.get(key)
    if existing is not None and not existing.done():
        return
    if session.state not in ("asr_ready", "running"):
        return
    task = asyncio.create_task(
        _ws_run_hermes_job(
            conn,
            session.device_id,
            session.request_id,
            session.user_text,
            existing_run_id=session.hermes_run_id,
        )
    )
    _track_watch_run_task(session.device_id, session.request_id, task)


def _resume_active_watch_sessions() -> None:
    for session in _get_session_repo().list_active_all():
        _ensure_watch_session_task(session)


def _normalize_danger_alert(alert: DangerAlertIn) -> dict[str, object]:
    payload = alert.dict(exclude_none=True)
    device_id = str(payload.get("device_id") or "watch-001")
    danger_type = str(payload.get("danger_type") or "danger")
    message = str(payload.get("message") or f"检测到危险声音：{danger_type}")
    event: dict[str, object] = {
        "type": "danger_alert",
        "event_id": str(payload.get("event_id") or f"{device_id}-{int(time.time())}"),
        "device_id": device_id,
        "danger_type": danger_type,
        "message": message,
    }
    probability = payload.get("danger_prob", payload.get("probability"))
    if probability is not None:
        event["danger_prob"] = probability
    if payload.get("occurred_at"):
        event["occurred_at"] = str(payload["occurred_at"])
    return event


@app.websocket("/v1/watch/alerts/ws")
async def watch_alerts_websocket(websocket: WebSocket) -> None:
    device_id = websocket.query_params.get("device_id", "watch-001")
    await websocket.accept()
    async with _alert_ws_lock:
        _alert_ws_clients.add(websocket)
    await _ws_send_json(websocket, {"type": "connected", "device_id": device_id})
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        async with _alert_ws_lock:
            _alert_ws_clients.discard(websocket)


@app.post("/v1/watch/alerts")
async def watch_alerts_create(
    alert: DangerAlertIn,
    authorization: str | None = Header(default=None),
) -> dict[str, object]:
    _require_device(alert.device_id, authorization, "watch_alerts")
    event = _normalize_danger_alert(alert)
    dead_clients: list[WebSocket] = []
    sent = 0
    async with _alert_ws_lock:
        for client in list(_alert_ws_clients):
            try:
                await _ws_send_json(client, event)
                sent += 1
            except Exception:
                dead_clients.append(client)
        for client in dead_clients:
            _alert_ws_clients.discard(client)
    return {"ok": True, "sent": sent, "event": event}


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
    audio_buffer = bytearray()
    audio_total = 0
    audio_started_at: float | None = None
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
        _resume_active_watch_sessions()
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
                    audio_buffer.clear()
                    audio_total = 0
                    audio_started_at = None
                    await _ws_try_send_error(conn, None, "audio_too_large")
                    continue
                audio_buffer.extend(chunk)
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
                audio_buffer = bytearray()
                audio_total = 0
                audio_started_at = time.monotonic()
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
                finished_audio = audio_buffer
                upload_ms = int(
                    (time.monotonic() - audio_started_at) * 1000
                ) if audio_started_at is not None else 0
                current_request_id = None
                audio_buffer = bytearray()
                audio_total = 0
                audio_started_at = None
                task = asyncio.create_task(
                    _ws_finish_audio(
                        conn,
                        device_id,
                        finished_request_id,
                        finished_audio,
                        mock_asr_text,
                        upload_ms,
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


class SessionStateOut(BaseModel):
    session_id: str
    request_id: str
    state: str
    created_at: str
    updated_at: str


class SessionListResponse(BaseModel):
    sessions: list[SessionStateOut]
    active_count: int  # 非终态 session 数


class SyncConversationResponse(BaseModel):
    has_pending: bool
    session_state: Literal["none", "running", "done", "error", "timeout", "canceled"]
    messages: list[ConversationMessageOut]


class SyncLatestUnreadOut(BaseModel):
    notification_id: str
    title: str
    preview: str
    created_at: str


class SyncInboxResponse(BaseModel):
    unread_count: int
    latest_unread: SyncLatestUnreadOut | None = None


class WatchSyncResponse(BaseModel):
    schema_version: int = 1
    conversation: SyncConversationResponse
    inbox: SyncInboxResponse


def _public_session_state(state: str) -> Literal["running", "done", "error", "timeout", "canceled"]:
    if state in ("accepted", "asr_ready", "running"):
        return "running"
    if state in ("done", "error", "timeout", "canceled"):
        return state  # type: ignore[return-value]
    return "error"


def _sync_messages_for(
    device_id: str,
    after_message_id: str | None,
    max_messages: int,
    request_id: str | None = None,
) -> list[ConversationMessage]:
    if max_messages <= 0:
        return []
    messages = _get_conversation_repo().list_after(device_id, after_message_id)
    if request_id:
        messages = [message for message in messages if message.request_id == request_id]
    if max_messages > 0 and len(messages) > max_messages:
        if after_message_id:
            return messages[:max_messages]
        return messages[-max_messages:]
    return messages


def _latest_unread_for(device_id: str) -> tuple[int, SyncLatestUnreadOut | None]:
    items, unread_count = _get_inbox_repo().list_items(device_id)
    latest_unread = next((item for item in items if not item.read), None)
    if latest_unread is None:
        return unread_count, None
    return unread_count, SyncLatestUnreadOut(
        notification_id=latest_unread.notification_id,
        title=latest_unread.title,
        preview=latest_unread.preview,
        created_at=latest_unread.created_at,
    )


@app.get("/v1/watch/session", response_model=SessionListResponse)
async def session_list(
    device_id: str = Query(...),
    request_id: str | None = Query(default=None),
    authorization: str | None = Header(default=None),
) -> SessionListResponse:
    """查询 device 的 session 状态。ESP32 用于判断 pending 是否完成。

    - 不带 request_id：返回所有 session（最近 20 条）
    - 带 request_id：只返回匹配的 session
    """
    _require_device(device_id, authorization, "session_list")
    session_repo = _get_session_repo()

    if request_id:
        try:
            s = session_repo.get_by_request_id(device_id, request_id)
            sessions = [s]
        except SessionValidationError:
            sessions = []
    else:
        sessions = session_repo.list_recent(device_id)

    active_count = sum(
        1
        for session in sessions
        if session.state
        not in ("done", "error", "timeout", "canceled", "interrupted")
    )
    return SessionListResponse(
        sessions=[
            SessionStateOut(
                session_id=s.session_id,
                request_id=s.request_id,
                state=s.state,
                created_at=s.created_at,
                updated_at=s.updated_at,
            )
            for s in sessions
        ],
        active_count=active_count,
    )


@app.get("/v1/watch/sync", response_model=WatchSyncResponse)
async def watch_sync(
    device_id: str = Query(...),
    mode: Literal["background", "foreground_reconcile"] = Query("background"),
    pending_request_id: str | None = Query(default=None),
    after_message_id: str | None = Query(default=None),
    max_messages: int = Query(default=10, ge=0, le=20),
    authorization: str | None = Header(default=None),
) -> WatchSyncResponse:
    """ESP32 后台统一 delta sync；server 内部仍分 session/conversation/inbox 三本账。"""
    _require_device(device_id, authorization, "watch_sync")
    _resume_active_watch_sessions()

    session_state: Literal["none", "running", "done", "error", "timeout", "canceled"] = "none"
    messages: list[ConversationMessage] = []
    if pending_request_id:
        try:
            session = _get_session_repo().get_by_request_id(device_id, pending_request_id)
            session_state = _public_session_state(session.state)
            messages = _sync_messages_for(
                device_id,
                after_message_id,
                max_messages,
                request_id=pending_request_id,
            )
        except SessionValidationError:
            session_state = "none"
            messages = []
    elif mode == "foreground_reconcile":
        messages = _sync_messages_for(device_id, after_message_id, max_messages)

    unread_count, latest_unread = _latest_unread_for(device_id)
    return WatchSyncResponse(
        conversation=SyncConversationResponse(
            has_pending=session_state == "running",
            session_state=session_state,
            messages=[
                ConversationMessageOut(**_conversation_message_payload(message))
                for message in messages
            ],
        ),
        inbox=SyncInboxResponse(
            unread_count=unread_count,
            latest_unread=latest_unread,
        ),
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

    try:
        session = _get_session_repo().get_by_request_id(device_id, request_id)
    except SessionValidationError:
        session = None

    if session is not None:
        if session.state == "done":
            return WatchResponse(
                request_id=request_id,
                status="done",
                action=_infer_action(session.user_text, session.reply_text),
                asr_text=session.user_text,
                reply_text=session.reply_text,
                error_code=None,
            )
        if session.state in TERMINAL_STATES:
            if session.state == "canceled":
                return _canceled_watch_response(request_id, session.user_text)
            return WatchResponse(
                request_id=request_id,
                status="timeout" if session.state == "timeout" else "error",
                action="error",
                asr_text=session.user_text,
                reply_text=session.reply_text,
                error_code=session.error_code or f"session_{session.state}",
            )

        try:
            session = _get_session_repo().transition(
                device_id,
                session.session_id,
                "canceled",
                error_code="request_canceled",
            )
        except SessionValidationError:
            session = _get_session_repo().get_by_request_id(device_id, request_id)
        if session.state == "canceled":
            if session.hermes_run_id:
                try:
                    await _stop_hermes_run(session.hermes_run_id)
                except Exception as exc:
                    logger.warning(
                        "Hermes stop failure request_id=%s error=%s",
                        request_id,
                        type(exc).__name__,
                    )
            run_task = _watch_run_tasks.get(key)
            if run_task is not None and not run_task.done():
                run_task.cancel()
            return _canceled_watch_response(request_id, session.user_text)

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
        "ws_request_status_counts": dict(_ws_request_status_counts),
        "last_ws_request": (
            dict(_last_ws_request_summary) if _last_ws_request_summary else None
        ),
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

async def _create_inbox_response(
    request: Request,
    device_id: str = Query(...),
) -> InboxCreateResponse:
    """解析并幂等写入 Inbox；鉴权由 public/internal adapter 分别负责。"""
    from fastapi.responses import JSONResponse

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


@app.post("/internal/watch/inbox", status_code=201)
async def internal_inbox_create(
    request: Request,
    device_id: str = Query(...),
    authorization: str | None = Header(default=None),
) -> InboxCreateResponse:
    """Hermes/server 内部主动提示写入，不接受手表 device token。"""
    _require_internal(authorization)
    if device_id not in _device_tokens():
        raise HTTPException(status_code=403, detail="device_not_allowed")
    return await _create_inbox_response(request, device_id)


@app.post("/v1/watch/inbox", status_code=201)
async def inbox_create(
    request: Request,
    device_id: str = Query(...),
    authorization: str | None = Header(default=None),
) -> InboxCreateResponse:
    """迁移期兼容入口；默认关闭，避免 device token 具备通知生产权限。"""
    if not WATCH_PUBLIC_INBOX_CREATE_ENABLED:
        raise HTTPException(status_code=404, detail="not_found")
    _require_device(device_id, authorization, "inbox_create")
    return await _create_inbox_response(request, device_id)


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
