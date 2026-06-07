from __future__ import annotations

import asyncio
import base64
import os
import re
import time
from typing import Literal

import httpx
from fastapi import FastAPI, File, Form, Header, HTTPException, Query, UploadFile
from pydantic import BaseModel


HERMES_API_URL = os.getenv("HERMES_API_URL", "http://127.0.0.1:8642").rstrip("/")
HERMES_API_KEY = os.getenv("HERMES_API_KEY", "")
HERMES_MODEL = os.getenv("HERMES_MODEL", "hermes-agent")
HERMES_TIMEOUT_SECONDS = float(os.getenv("HERMES_TIMEOUT_SECONDS", "120"))
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
REPLY_MAX_CHARS = int(os.getenv("WATCH_REPLY_MAX_CHARS", "80"))
MIMO_ASR_DIRECT_MIME_TYPES = {"audio/wav", "audio/mp3", "audio/mpeg"}
REQUEST_ID_PATTERN = re.compile(r"^[A-Za-z0-9._:-]{1,96}$")
INVALID_REQUEST_ID = "invalid-request"

WATCH_INSTRUCTIONS = (
    "你是 AI Memory Watch 的 Hermes 大脑。输入来自 ESP32-S3 手表短语音转写。"
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


app = FastAPI(title="AI Memory Watch Voice Endpoint", version="0.1.0")
_canceled_requests: set[tuple[str, str]] = set()
_completed_requests: dict[tuple[str, str], WatchResponse] = {}
_inflight_requests: dict[tuple[str, str], asyncio.Task[WatchResponse]] = {}
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


def _require_device(device_id: str, authorization: str | None) -> None:
    tokens = _device_tokens()
    expected = tokens.get(device_id)
    if not expected:
        raise HTTPException(status_code=403, detail="device_not_allowed")
    prefix = "Bearer "
    if not authorization or not authorization.startswith(prefix):
        raise HTTPException(status_code=401, detail="missing_bearer_token")
    if authorization[len(prefix) :] != expected:
        raise HTTPException(status_code=403, detail="invalid_device_token")


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


def _normalize_request_id(request_id: str) -> str | None:
    normalized = request_id.strip()
    if not REQUEST_ID_PATTERN.fullmatch(normalized):
        return None
    return normalized


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


async def _process_voice_command(
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
        return WatchResponse(
            request_id=request_id,
            status="timeout",
            action="error",
            asr_text=asr_text,
            reply_text="Hermes 处理超时",
            error_code="server_timeout",
        )
    except Exception:
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
        if key in _canceled_requests:
            return _canceled_watch_response(request_id, response.asr_text)
        if response.status != "canceled":
            _completed_requests[key] = response
            _trim_completed_requests()
        return response


@app.get("/v1/watch/health", response_model=HealthResponse)
async def health(
    device_id: str = Query(...),
    authorization: str | None = Header(default=None),
) -> HealthResponse:
    _require_device(device_id, authorization)
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
    _require_device(device_id, authorization)
    del battery_percent, charging, rssi, firmware_version, locale, timezone, source, ui_state
    normalized_request_id = _normalize_request_id(request_id)
    if normalized_request_id is None:
        return _error_watch_response(INVALID_REQUEST_ID, "请求编号异常，请重新发送")
    request_id = normalized_request_id
    key = (device_id, request_id)

    async with _request_lock:
        if key in _completed_requests:
            return _completed_requests[key]
        if key in _canceled_requests:
            return _canceled_watch_response(request_id)
        task = _inflight_requests.get(key)
    if task is not None:
        return await _await_request_task(key, task)

    try:
        audio_bytes = await _read_audio(audio)
    except HTTPException as exc:
        if exc.status_code == 413:
            return _error_watch_response(request_id, "语音太长，请说短一点")
        raise
    if not audio_bytes:
        return _error_watch_response(request_id, "没有收到音频，请再说一次")
    async with _request_lock:
        if key in _completed_requests:
            return _completed_requests[key]
        if key in _canceled_requests:
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


@app.post("/v1/watch/request/{request_id}/cancel", response_model=WatchResponse)
async def cancel_request(
    request_id: str,
    device_id: str = Form(...),
    authorization: str | None = Header(default=None),
) -> WatchResponse:
    _require_device(device_id, authorization)
    normalized_request_id = _normalize_request_id(request_id)
    if normalized_request_id is None:
        return _error_watch_response(INVALID_REQUEST_ID, "请求编号异常，请重新发送")
    request_id = normalized_request_id
    key = (device_id, request_id)
    async with _request_lock:
        if key in _completed_requests:
            return _completed_requests[key]
        _canceled_requests.add(key)
        _trim_canceled_requests()
        task = _inflight_requests.get(key)
        if task is not None and not task.done():
            task.cancel()
    return _canceled_watch_response(request_id)


@app.get("/health")
async def service_health() -> dict[str, str | int]:
    return {
        "status": "ok",
        "time": int(time.time()),
        "asr_provider": ASR_PROVIDER,
        "inflight_requests": len(_inflight_requests),
        "completed_requests": len(_completed_requests),
        "canceled_requests": len(_canceled_requests),
    }
