from __future__ import annotations

import importlib

import httpx
import pytest


@pytest.fixture()
def anyio_backend():
    return "asyncio"


@pytest.fixture()
def watch_app(monkeypatch):
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", "watch-001=test-token")
    monkeypatch.setenv("HERMES_API_KEY", "test-hermes-key")
    monkeypatch.setenv("WATCH_REPLY_MAX_CHARS", "20")
    import app

    module = importlib.reload(app)
    module._canceled_requests.clear()
    return module


@pytest.mark.anyio
async def test_voice_command_returns_watch_json(watch_app, monkeypatch):
    async def fake_call_hermes(device_id: str, asr_text: str, clarification_id: str | None) -> str:
        assert device_id == "watch-001"
        assert asr_text == "记一下明天看电池日志"
        assert clarification_id is None
        return "已记录：明天看电池日志，这段较长的回复会被截断。"

    monkeypatch.setattr(watch_app, "_call_hermes", fake_call_hermes)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/v1/watch/voice-command",
            headers={"Authorization": "Bearer test-token"},
            data={
                "request_id": "watch-001-test-0001",
                "device_id": "watch-001",
                "mock_asr_text": "记一下明天看电池日志",
            },
            files={"audio": ("command.ogg", b"OggS\x00\x01", "audio/ogg")},
        )

    assert response.status_code == 200
    payload = response.json()
    assert sorted(payload.keys()) == [
        "action",
        "asr_text",
        "clarification_id",
        "error_code",
        "reply_text",
        "request_id",
        "status",
    ]
    assert payload["status"] == "done"
    assert payload["action"] == "memory_saved"
    assert payload["reply_text"] == "已记录：明天看电池日志，这段较长的回复会"[:20]


@pytest.mark.anyio
async def test_voice_command_rejects_unknown_device(watch_app):
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/v1/watch/voice-command",
            headers={"Authorization": "Bearer test-token"},
            data={"request_id": "x", "device_id": "watch-unknown"},
            files={"audio": ("command.ogg", b"OggS", "audio/ogg")},
        )

    assert response.status_code == 403


@pytest.mark.anyio
async def test_cancel_records_canceled_request(watch_app):
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/v1/watch/request/watch-001-test-0002/cancel",
            headers={"Authorization": "Bearer test-token"},
            data={"device_id": "watch-001"},
        )

    assert response.status_code == 200
    payload = response.json()
    assert payload["status"] == "canceled"
    assert payload["action"] == "no_action"
