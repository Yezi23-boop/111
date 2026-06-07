from __future__ import annotations

import os
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
MAX_AUDIO_BYTES = int(os.getenv("WATCH_MAX_AUDIO_BYTES", str(6 * 1024 * 1024)))
REPLY_MAX_CHARS = int(os.getenv("WATCH_REPLY_MAX_CHARS", "80"))

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


async def _read_audio_size(audio: UploadFile) -> int:
    total = 0
    while True:
        chunk = await audio.read(64 * 1024)
        if not chunk:
            break
        total += len(chunk)
        if total > MAX_AUDIO_BYTES:
            raise HTTPException(status_code=413, detail="audio_too_large")
    return total


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
    await _read_audio_size(audio)

    key = (device_id, request_id)
    if key in _canceled_requests:
        return WatchResponse(
            request_id=request_id,
            status="canceled",
            action="no_action",
            asr_text="",
            reply_text="已取消等待",
        )

    asr_text = (mock_asr_text or WATCH_MOCK_ASR_TEXT).strip()
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
        return WatchResponse(
            request_id=request_id,
            status="canceled",
            action="no_action",
            asr_text=asr_text,
            reply_text="已取消等待",
        )

    return WatchResponse(
        request_id=request_id,
        status="done",
        action=_infer_action(asr_text, reply_text),
        asr_text=asr_text,
        reply_text=reply_text[:REPLY_MAX_CHARS],
        clarification_id=None,
        error_code=None,
    )


@app.post("/v1/watch/request/{request_id}/cancel", response_model=WatchResponse)
async def cancel_request(
    request_id: str,
    device_id: str = Form(...),
    authorization: str | None = Header(default=None),
) -> WatchResponse:
    _require_device(device_id, authorization)
    _canceled_requests.add((device_id, request_id))
    if len(_canceled_requests) > 1024:
        _canceled_requests.clear()
    return WatchResponse(
        request_id=request_id,
        status="canceled",
        action="no_action",
        asr_text="",
        reply_text="已取消等待",
    )


@app.get("/health")
async def service_health() -> dict[str, str | int]:
    return {"status": "ok", "time": int(time.time())}
