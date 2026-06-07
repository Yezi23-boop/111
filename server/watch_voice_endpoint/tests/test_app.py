from __future__ import annotations

import asyncio
import importlib

import httpx
import pytest


@pytest.fixture()
def anyio_backend():
    return "asyncio"


WATCH_RESPONSE_KEYS = [
    "action",
    "asr_text",
    "clarification_id",
    "error_code",
    "reply_text",
    "request_id",
    "status",
]


@pytest.fixture()
def watch_app(monkeypatch):
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", "watch-001=test-token")
    monkeypatch.setenv("HERMES_API_KEY", "test-hermes-key")
    monkeypatch.setenv("WATCH_REPLY_MAX_CHARS", "20")
    import app

    module = importlib.reload(app)
    module._canceled_requests.clear()
    module._completed_requests.clear()
    module._inflight_requests.clear()
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
    assert sorted(payload.keys()) == WATCH_RESPONSE_KEYS
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
async def test_voice_command_returns_watch_json_for_invalid_request_id(watch_app):
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/v1/watch/voice-command",
            headers={"Authorization": "Bearer test-token"},
            data={
                "request_id": "bad request id",
                "device_id": "watch-001",
                "mock_asr_text": "记一下明天看电池日志",
            },
            files={"audio": ("command.ogg", b"OggS\x00\x01", "audio/ogg")},
        )

    assert response.status_code == 200
    payload = response.json()
    assert sorted(payload.keys()) == WATCH_RESPONSE_KEYS
    assert payload["request_id"] == "invalid-request"
    assert payload["status"] == "error"
    assert payload["action"] == "error"
    assert payload["error_code"] == "asr_or_agent_error"


@pytest.mark.anyio
async def test_voice_command_returns_watch_json_for_empty_audio(watch_app):
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/v1/watch/voice-command",
            headers={"Authorization": "Bearer test-token"},
            data={
                "request_id": "watch-001-empty-audio",
                "device_id": "watch-001",
                "mock_asr_text": "记一下明天看电池日志",
            },
            files={"audio": ("command.ogg", b"", "audio/ogg")},
        )

    assert response.status_code == 200
    payload = response.json()
    assert sorted(payload.keys()) == WATCH_RESPONSE_KEYS
    assert payload["request_id"] == "watch-001-empty-audio"
    assert payload["status"] == "error"
    assert payload["action"] == "error"
    assert payload["error_code"] == "asr_or_agent_error"


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


@pytest.mark.anyio
async def test_voice_command_reuses_completed_request_result(watch_app, monkeypatch):
    calls = 0

    async def fake_call_hermes(device_id: str, asr_text: str, clarification_id: str | None) -> str:
        nonlocal calls
        calls += 1
        return "已记录：明天看电池日志。"

    monkeypatch.setattr(watch_app, "_call_hermes", fake_call_hermes)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        first = await client.post(
            "/v1/watch/voice-command",
            headers={"Authorization": "Bearer test-token"},
            data={
                "request_id": "watch-001-idem-0001",
                "device_id": "watch-001",
                "mock_asr_text": "记一下明天看电池日志",
            },
            files={"audio": ("command.ogg", b"OggS\x00\x01", "audio/ogg")},
        )
        second = await client.post(
            "/v1/watch/voice-command",
            headers={"Authorization": "Bearer test-token"},
            data={
                "request_id": "watch-001-idem-0001",
                "device_id": "watch-001",
                "mock_asr_text": "这是重复请求，不应重新处理",
            },
            files={"audio": ("command.ogg", b"OggS\x00\x02", "audio/ogg")},
        )

    assert first.status_code == 200
    assert second.status_code == 200
    assert calls == 1
    assert second.json() == first.json()


@pytest.mark.anyio
async def test_concurrent_duplicate_voice_command_shares_inflight_task(watch_app, monkeypatch):
    calls = 0

    async def fake_call_hermes(device_id: str, asr_text: str, clarification_id: str | None) -> str:
        nonlocal calls
        calls += 1
        await asyncio.sleep(0.03)
        return "已记录：明天看电池日志。"

    monkeypatch.setattr(watch_app, "_call_hermes", fake_call_hermes)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        async def send_duplicate():
            return await client.post(
                "/v1/watch/voice-command",
                headers={"Authorization": "Bearer test-token"},
                data={
                    "request_id": "watch-001-idem-0002",
                    "device_id": "watch-001",
                    "mock_asr_text": "记一下明天看电池日志",
                },
                files={"audio": ("command.ogg", b"OggS\x00\x01", "audio/ogg")},
            )

        first, second = await asyncio.gather(send_duplicate(), send_duplicate())

    assert first.status_code == 200
    assert second.status_code == 200
    assert calls == 1
    assert first.json() == second.json()


@pytest.mark.anyio
async def test_cancel_completed_request_returns_completed_result(watch_app, monkeypatch):
    async def fake_call_hermes(device_id: str, asr_text: str, clarification_id: str | None) -> str:
        return "已记录：明天看电池日志。"

    monkeypatch.setattr(watch_app, "_call_hermes", fake_call_hermes)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        completed = await client.post(
            "/v1/watch/voice-command",
            headers={"Authorization": "Bearer test-token"},
            data={
                "request_id": "watch-001-idem-0003",
                "device_id": "watch-001",
                "mock_asr_text": "记一下明天看电池日志",
            },
            files={"audio": ("command.ogg", b"OggS\x00\x01", "audio/ogg")},
        )
        canceled = await client.post(
            "/v1/watch/request/watch-001-idem-0003/cancel",
            headers={"Authorization": "Bearer test-token"},
            data={"device_id": "watch-001"},
        )

    assert completed.status_code == 200
    assert canceled.status_code == 200
    assert canceled.json() == completed.json()


@pytest.mark.anyio
async def test_voice_command_returns_timeout_before_watch_deadline(watch_app, monkeypatch):
    async def fake_call_hermes(device_id: str, asr_text: str, clarification_id: str | None) -> str:
        await asyncio.sleep(0.05)
        return "这条回复不应赶在总预算内返回"

    monkeypatch.setattr(watch_app, "WATCH_REQUEST_TIMEOUT_SECONDS", 0.01)
    monkeypatch.setattr(watch_app, "_call_hermes", fake_call_hermes)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/v1/watch/voice-command",
            headers={"Authorization": "Bearer test-token"},
            data={
                "request_id": "watch-001-timeout-0001",
                "device_id": "watch-001",
                "mock_asr_text": "记一下明天看电池日志",
            },
            files={"audio": ("command.ogg", b"OggS\x00\x01", "audio/ogg")},
        )

    assert response.status_code == 200
    payload = response.json()
    assert sorted(payload.keys()) == WATCH_RESPONSE_KEYS
    assert payload["status"] == "timeout"
    assert payload["action"] == "error"
    assert payload["request_id"] == "watch-001-timeout-0001"
    assert payload["error_code"] == "server_timeout"


@pytest.mark.anyio
async def test_mimo_asr_adapter_uses_openai_compatible_audio_payload(watch_app, monkeypatch):
    seen = {}
    transcode_seen = {}

    class FakeAsyncClient:
        def __init__(self, timeout, trust_env):
            seen["timeout"] = timeout
            seen["trust_env"] = trust_env

        async def __aenter__(self):
            return self

        async def __aexit__(self, exc_type, exc, tb):
            return False

        async def post(self, url, headers, json):
            seen["url"] = url
            seen["headers"] = headers
            seen["json"] = json
            return httpx.Response(
                200,
                json={
                    "choices": [
                        {
                            "message": {
                                "content": "记一下明天看电池日志",
                            }
                        }
                    ]
                },
                request=httpx.Request("POST", url),
            )

    monkeypatch.setattr(watch_app.httpx, "AsyncClient", FakeAsyncClient)

    async def fake_transcode(audio_bytes: bytes):
        transcode_seen["audio_bytes"] = audio_bytes
        return b"RIFF-test-wav", "audio/wav"

    monkeypatch.setattr(watch_app, "_transcode_to_wav", fake_transcode)
    watch_app.MIMO_ASR_BASE_URL = "https://token-plan-cn.xiaomimimo.com/v1"
    watch_app.MIMO_ASR_API_KEY = "test-asr-key"

    text = await watch_app._call_mimo_asr(b"OggS\x00\x01", "audio/ogg")

    assert text == "记一下明天看电池日志"
    assert seen["url"] == "https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
    assert seen["headers"] == {"Authorization": "Bearer test-asr-key"}
    assert seen["trust_env"] is False
    assert transcode_seen["audio_bytes"] == b"OggS\x00\x01"
    body = seen["json"]
    assert body["model"] == "mimo-v2.5-asr"
    assert body["asr_options"] == {"language": "auto"}
    content = body["messages"][0]["content"][0]
    assert content["type"] == "input_audio"
    assert content["input_audio"]["data"].startswith("data:audio/wav;base64,")
