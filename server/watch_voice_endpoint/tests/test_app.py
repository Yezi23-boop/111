from __future__ import annotations

import asyncio
import importlib
import json

import httpx
import pytest
from fastapi import HTTPException


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

AUTH_FAILURE_CASES = [
    ("missing_bearer", None, "watch-001", 401, "missing_bearer_token"),
    ("wrong_token", "Bearer wrong-token", "watch-001", 403, "invalid_device_token"),
    (
        "unknown_device",
        "Bearer test-token",
        "watch-unknown",
        403,
        "device_not_allowed",
    ),
]
ENDPOINT_AUTH_NAMES = {
    "health": "watch_health",
    "voice": "voice_command",
    "cancel": "cancel",
}


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
    module._request_event_counts.update(
        {
            "processed": 0,
            "cache_hits": 0,
            "inflight_waits": 0,
            "canceled_hits": 0,
        }
    )
    module._request_status_counts.update(
        {
            "done": 0,
            "error": 0,
            "timeout": 0,
            "canceled": 0,
        }
    )
    module._auth_failure_counts.update(
        {
            "missing_bearer_token": 0,
            "invalid_device_token": 0,
            "device_not_allowed": 0,
        }
    )
    module._request_error_counts.clear()
    module._last_request_summary.clear()
    module._last_auth_failure_summary.clear()
    return module


async def _call_watch_endpoint(
    client: httpx.AsyncClient,
    endpoint: str,
    device_id: str,
    authorization: str | None,
):
    headers = {} if authorization is None else {"Authorization": authorization}
    if endpoint == "health":
        return await client.get(
            "/v1/watch/health",
            headers=headers,
            params={"device_id": device_id},
        )
    if endpoint == "voice":
        return await client.post(
            "/v1/watch/voice-command",
            headers=headers,
            data={
                "request_id": f"{device_id}-auth-0001",
                "device_id": device_id,
                "mock_asr_text": "记一下明天看电池日志",
            },
            files={"audio": ("command.ogg", b"OggS\x00\x01", "audio/ogg")},
        )
    if endpoint == "cancel":
        return await client.post(
            f"/v1/watch/request/{device_id}-auth-0001/cancel",
            headers=headers,
            data={"device_id": device_id},
        )
    raise AssertionError(f"unknown endpoint: {endpoint}")


async def _post_voice_command(
    client: httpx.AsyncClient,
    request_id: str,
    audio_bytes: bytes = b"OggS\x00\x01",
    mock_asr_text: str = "记一下明天看电池日志",
):
    return await client.post(
        "/v1/watch/voice-command",
        headers={"Authorization": "Bearer test-token"},
        data={
            "request_id": request_id,
            "device_id": "watch-001",
            "mock_asr_text": mock_asr_text,
        },
        files={"audio": ("command.ogg", audio_bytes, "audio/ogg")},
    )


def _assert_watch_payload_shape(payload: dict) -> None:
    assert sorted(payload.keys()) == WATCH_RESPONSE_KEYS


def _assert_last_request_keeps_sensitive_text_out(watch_app) -> dict:
    last_request = watch_app._last_request_summary
    assert "asr_text" not in last_request
    assert "reply_text" not in last_request
    assert "authorization" not in last_request
    assert "token" not in last_request
    return last_request


async def _assert_service_health_excludes(
    client: httpx.AsyncClient,
    *sentinels: str,
) -> dict:
    health = await client.get("/health")
    assert health.status_code == 200
    payload = health.json()
    rendered = json.dumps(payload, ensure_ascii=False)
    for sentinel in sentinels:
        assert sentinel not in rendered
    return payload


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
    _assert_watch_payload_shape(payload)
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
@pytest.mark.parametrize("endpoint", ["health", "voice", "cancel"])
@pytest.mark.parametrize(
    "case_name,authorization,device_id,expected_status,expected_detail",
    AUTH_FAILURE_CASES,
)
async def test_watch_endpoints_reject_auth_failures(
    watch_app,
    endpoint,
    case_name,
    authorization,
    device_id,
    expected_status,
    expected_detail,
):
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await _call_watch_endpoint(
            client,
            endpoint,
            device_id,
            authorization,
        )
        health = await client.get("/health")

    assert case_name
    assert response.status_code == expected_status
    assert response.json() == {"detail": expected_detail}
    assert watch_app._request_event_counts["processed"] == 0
    assert watch_app._request_event_counts["cache_hits"] == 0
    assert watch_app._request_event_counts["inflight_waits"] == 0
    assert watch_app._request_event_counts["canceled_hits"] == 0
    assert watch_app._auth_failure_counts[expected_detail] == 1
    last_auth_failure = watch_app._last_auth_failure_summary
    assert last_auth_failure["endpoint"] == ENDPOINT_AUTH_NAMES[endpoint]
    assert last_auth_failure["device_id"] == device_id
    assert last_auth_failure["status_code"] == expected_status
    assert last_auth_failure["reason"] == expected_detail
    rendered = json.dumps(last_auth_failure, ensure_ascii=False)
    assert "Bearer" not in rendered
    assert "test-token" not in rendered
    assert "wrong-token" not in rendered
    assert health.status_code == 200
    health_payload = health.json()
    assert health_payload["auth_failures"][expected_detail] == 1
    assert health_payload["last_auth_failure"] == last_auth_failure
    rendered_health = json.dumps(health_payload, ensure_ascii=False)
    assert "Bearer" not in rendered_health
    assert "test-token" not in rendered_health
    assert "wrong-token" not in rendered_health


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
    _assert_watch_payload_shape(payload)
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
    _assert_watch_payload_shape(payload)
    assert payload["request_id"] == "watch-001-empty-audio"
    assert payload["status"] == "error"
    assert payload["action"] == "error"
    assert payload["error_code"] == "asr_or_agent_error"


@pytest.mark.anyio
async def test_voice_command_returns_watch_json_for_oversized_audio(watch_app, monkeypatch):
    async def fail_if_transcribed(audio_bytes, mime_type, mock_asr_text):
        raise AssertionError("ASR should not run for oversized audio")

    async def fail_if_called(device_id, asr_text, clarification_id):
        raise AssertionError("Hermes should not run for oversized audio")

    monkeypatch.setattr(watch_app, "MAX_AUDIO_BYTES", 4)
    monkeypatch.setattr(watch_app, "_transcribe_audio", fail_if_transcribed)
    monkeypatch.setattr(watch_app, "_call_hermes", fail_if_called)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await _post_voice_command(
            client,
            "watch-001-large-audio",
            audio_bytes=b"OggS\x00\x01",
        )

    assert response.status_code == 200
    payload = response.json()
    _assert_watch_payload_shape(payload)
    assert payload["request_id"] == "watch-001-large-audio"
    assert payload["status"] == "error"
    assert payload["action"] == "error"
    assert payload["error_code"] == "asr_or_agent_error"
    assert watch_app._request_status_counts["error"] == 1
    assert watch_app._request_error_counts["asr_or_agent_error"] == 1
    last_request = _assert_last_request_keeps_sensitive_text_out(watch_app)
    assert last_request["audio_bytes"] == 5
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        await _assert_service_health_excludes(
            client,
            "Bearer test-token",
            "test-hermes-key",
            "OggS",
        )


@pytest.mark.anyio
async def test_voice_command_returns_watch_json_for_asr_failure(watch_app, monkeypatch):
    async def fake_transcribe_audio(audio_bytes, mime_type, mock_asr_text):
        raise RuntimeError("asr failed")

    async def fail_if_called(device_id, asr_text, clarification_id):
        raise AssertionError("Hermes should not run when ASR fails")

    monkeypatch.setattr(watch_app, "_transcribe_audio", fake_transcribe_audio)
    monkeypatch.setattr(watch_app, "_call_hermes", fail_if_called)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await _post_voice_command(client, "watch-001-asr-failure")

    assert response.status_code == 200
    payload = response.json()
    _assert_watch_payload_shape(payload)
    assert payload["request_id"] == "watch-001-asr-failure"
    assert payload["status"] == "error"
    assert payload["action"] == "error"
    assert payload["asr_text"] == ""
    assert payload["error_code"] == "asr_or_agent_error"
    assert watch_app._request_status_counts["error"] == 1
    assert watch_app._request_error_counts["asr_or_agent_error"] == 1
    _assert_last_request_keeps_sensitive_text_out(watch_app)


@pytest.mark.anyio
async def test_voice_command_returns_watch_json_for_empty_asr_text(watch_app, monkeypatch):
    async def fake_transcribe_audio(audio_bytes, mime_type, mock_asr_text):
        return ""

    async def fail_if_called(device_id, asr_text, clarification_id):
        raise AssertionError("Hermes should not run without ASR text")

    monkeypatch.setattr(watch_app, "_transcribe_audio", fake_transcribe_audio)
    monkeypatch.setattr(watch_app, "_call_hermes", fail_if_called)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await _post_voice_command(client, "watch-001-empty-asr")

    assert response.status_code == 200
    payload = response.json()
    _assert_watch_payload_shape(payload)
    assert payload["request_id"] == "watch-001-empty-asr"
    assert payload["status"] == "error"
    assert payload["action"] == "error"
    assert payload["reply_text"] == "没有听清，请再说一次"
    assert payload["error_code"] == "asr_or_agent_error"
    assert watch_app._request_status_counts["error"] == 1
    assert watch_app._request_error_counts["asr_or_agent_error"] == 1
    _assert_last_request_keeps_sensitive_text_out(watch_app)


@pytest.mark.anyio
async def test_voice_command_maps_hermes_http_error_to_watch_error(watch_app, monkeypatch):
    async def fake_call_hermes(device_id, asr_text, clarification_id):
        request = httpx.Request("POST", "http://hermes.test/v1/responses")
        response = httpx.Response(500, request=request)
        raise httpx.HTTPStatusError("Hermes failed", request=request, response=response)

    monkeypatch.setattr(watch_app, "_call_hermes", fake_call_hermes)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await _post_voice_command(client, "watch-001-hermes-500")

    assert response.status_code == 200
    payload = response.json()
    _assert_watch_payload_shape(payload)
    assert payload["request_id"] == "watch-001-hermes-500"
    assert payload["status"] == "error"
    assert payload["action"] == "error"
    assert payload["asr_text"] == "记一下明天看电池日志"
    assert payload["error_code"] == "asr_or_agent_error"
    assert watch_app._request_status_counts["error"] == 1
    assert watch_app._request_error_counts["asr_or_agent_error"] == 1
    _assert_last_request_keeps_sensitive_text_out(watch_app)


@pytest.mark.anyio
async def test_voice_command_maps_empty_hermes_reply_to_watch_error(watch_app, monkeypatch):
    async def fake_call_hermes(device_id, asr_text, clarification_id):
        return ""

    monkeypatch.setattr(watch_app, "_call_hermes", fake_call_hermes)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await _post_voice_command(client, "watch-001-hermes-empty")

    assert response.status_code == 200
    payload = response.json()
    _assert_watch_payload_shape(payload)
    assert payload["request_id"] == "watch-001-hermes-empty"
    assert payload["status"] == "error"
    assert payload["action"] == "error"
    assert payload["asr_text"] == "记一下明天看电池日志"
    assert payload["reply_text"] == "没有处理成功，请再说一次"
    assert payload["error_code"] == "asr_or_agent_error"
    assert watch_app._request_status_counts["error"] == 1
    assert watch_app._request_error_counts["asr_or_agent_error"] == 1
    _assert_last_request_keeps_sensitive_text_out(watch_app)


@pytest.mark.anyio
async def test_voice_command_maps_hermes_timeout_to_watch_timeout(watch_app, monkeypatch):
    async def fake_call_hermes(device_id, asr_text, clarification_id):
        raise httpx.TimeoutException("Hermes timed out")

    monkeypatch.setattr(watch_app, "_call_hermes", fake_call_hermes)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await _post_voice_command(client, "watch-001-hermes-timeout")

    assert response.status_code == 200
    payload = response.json()
    _assert_watch_payload_shape(payload)
    assert payload["request_id"] == "watch-001-hermes-timeout"
    assert payload["status"] == "timeout"
    assert payload["action"] == "error"
    assert payload["asr_text"] == "记一下明天看电池日志"
    assert payload["error_code"] == "server_timeout"
    assert watch_app._request_status_counts["timeout"] == 1
    assert watch_app._request_error_counts["server_timeout"] == 1
    _assert_last_request_keeps_sensitive_text_out(watch_app)


@pytest.mark.anyio
async def test_voice_command_redacts_ffmpeg_transcode_failure_detail(watch_app, monkeypatch):
    async def fake_transcode_to_wav(audio_bytes):
        raise HTTPException(
            status_code=500,
            detail="audio_transcode_failed:SECRET_FFMPEG_DETAIL",
        )

    async def fail_if_called(device_id, asr_text, clarification_id):
        raise AssertionError("Hermes should not run when MiMo ASR fails")

    monkeypatch.setattr(watch_app, "ASR_PROVIDER", "mimo")
    monkeypatch.setattr(watch_app, "MIMO_ASR_BASE_URL", "https://asr.test/v1")
    monkeypatch.setattr(watch_app, "MIMO_ASR_API_KEY", "SECRET_MIMO_KEY")
    monkeypatch.setattr(watch_app, "_transcode_to_wav", fake_transcode_to_wav)
    monkeypatch.setattr(watch_app, "_call_hermes", fail_if_called)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await _post_voice_command(
            client,
            "watch-001-ffmpeg-failure",
            mock_asr_text="",
        )

        health = await _assert_service_health_excludes(
            client,
            "SECRET_FFMPEG_DETAIL",
            "SECRET_MIMO_KEY",
            "Bearer test-token",
        )

    assert response.status_code == 200
    payload = response.json()
    _assert_watch_payload_shape(payload)
    assert payload["request_id"] == "watch-001-ffmpeg-failure"
    assert payload["status"] == "error"
    assert payload["action"] == "error"
    assert payload["asr_text"] == ""
    assert payload["reply_text"] == "没有处理成功，请再说一次"
    assert payload["error_code"] == "asr_or_agent_error"
    assert health["request_status_counts"]["error"] == 1
    assert health["request_error_counts"]["asr_or_agent_error"] == 1


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
    _assert_watch_payload_shape(payload)
    assert payload["status"] == "timeout"
    assert payload["action"] == "error"
    assert payload["request_id"] == "watch-001-timeout-0001"
    assert payload["error_code"] == "server_timeout"


@pytest.mark.anyio
async def test_service_health_exposes_non_secret_request_metrics(watch_app, monkeypatch):
    async def fake_call_hermes(device_id: str, asr_text: str, clarification_id: str | None) -> str:
        return "已记录：明天看电池日志。"

    monkeypatch.setattr(watch_app, "_call_hermes", fake_call_hermes)
    transport = httpx.ASGITransport(app=watch_app.app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/v1/watch/voice-command",
            headers={"Authorization": "Bearer test-token"},
            data={
                "request_id": "watch-001-metrics-0001",
                "device_id": "watch-001",
                "mock_asr_text": "记一下明天看电池日志",
            },
            files={"audio": ("command.ogg", b"OggS\x00\x01", "audio/ogg")},
        )
        health = await client.get("/health")

    assert response.status_code == 200
    assert health.status_code == 200
    payload = health.json()
    assert payload["request_events"]["processed"] == 1
    assert payload["request_status_counts"]["done"] == 1
    assert payload["request_error_counts"] == {}
    last_request = payload["last_request"]
    assert last_request["device_id"] == "watch-001"
    assert last_request["request_id"] == "watch-001-metrics-0001"
    assert last_request["status"] == "done"
    assert last_request["action"] == "memory_saved"
    assert last_request["audio_bytes"] == 6
    assert last_request["duration_ms"] >= 0
    assert "asr_text" not in last_request
    assert "reply_text" not in last_request
    assert "authorization" not in last_request


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
