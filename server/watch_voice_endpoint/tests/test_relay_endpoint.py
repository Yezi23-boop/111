from __future__ import annotations

import asyncio
import importlib

from fastapi.testclient import TestClient

from relay_transport import RelaySessionBusyError


def _prepare_relay_app(monkeypatch, tmp_path):
    monkeypatch.setenv("HERMES_API_KEY", "test-hermes-key")
    monkeypatch.setenv("WATCH_INTERNAL_API_KEY", "test-internal-key")
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", "watch-001=test-device-token")
    monkeypatch.setenv("WATCH_RELAY_ENDPOINT_TOKEN", "relay-endpoint-token")
    monkeypatch.setenv("SESSION_DB_PATH", str(tmp_path / "session.db"))
    monkeypatch.setenv("CONVERSATION_DB_PATH", str(tmp_path / "conversation.db"))
    monkeypatch.setenv("INBOX_DB_PATH", str(tmp_path / "inbox.db"))
    import app

    module = importlib.reload(app)
    session = module._get_session_repo()
    session.create_or_get("watch-001", "relay-1", "relay-1", transport="relay")
    session.transition("watch-001", "relay-1", "asr_ready", user_text="记一下日志")
    session.transition("watch-001", "relay-1", "running")
    session.attach_relay_inbound(
        "watch-001",
        "relay-1",
        "watch:watch-001:relay-1",
        relay_state="sent",
        session_key="agent:main:relay:dm:watch-001",
    )
    module._get_conversation_repo().add_message_once(
        device_id="watch-001",
        request_id="relay-1",
        role="user",
        text="记一下日志",
        status="done",
        message_id="watch-relay-1",
    )
    return module


def test_relay_outbound_requires_private_token(monkeypatch, tmp_path):
    module = _prepare_relay_app(monkeypatch, tmp_path)
    response = TestClient(module.app).post(
        "/internal/relay/outbound",
        json={
            "device_id": "watch-001",
            "request_id": "relay-1",
            "delivery_id": "delivery-1",
            "content": "已记录",
        },
    )
    assert response.status_code == 401


def test_relay_outbound_persists_once_and_replays_duplicate(monkeypatch, tmp_path):
    module = _prepare_relay_app(monkeypatch, tmp_path)
    client = TestClient(module.app)
    headers = {"Authorization": "Bearer relay-endpoint-token"}
    body = {
        "device_id": "watch-001",
        "request_id": "relay-1",
        "delivery_id": "delivery-1",
        "content": "已记录",
        "reply_to": "watch:watch-001:relay-1",
    }

    first = client.post("/internal/relay/outbound", headers=headers, json=body)
    duplicate = client.post("/internal/relay/outbound", headers=headers, json=body)

    assert first.status_code == 200
    assert first.json()["duplicate"] is False
    assert duplicate.status_code == 200
    assert duplicate.json()["duplicate"] is True
    session = module._get_session_repo().get("watch-001", "relay-1")
    assert session.state == "done"
    assert session.relay_state == "completed"
    messages = module._get_conversation_repo().list_recent("watch-001")
    assert [message.role for message in messages] == ["user", "assistant"]


def test_relay_outbound_rejects_conflicting_duplicate(monkeypatch, tmp_path):
    module = _prepare_relay_app(monkeypatch, tmp_path)
    client = TestClient(module.app)
    headers = {"Authorization": "Bearer relay-endpoint-token"}
    base = {
        "device_id": "watch-001",
        "request_id": "relay-1",
        "delivery_id": "delivery-1",
        "content": "已记录",
    }
    assert client.post("/internal/relay/outbound", headers=headers, json=base).status_code == 200
    base["content"] = "不同回复"
    response = client.post("/internal/relay/outbound", headers=headers, json=base)
    assert response.status_code == 409


def test_relay_busy_maps_to_fixed_watch_error(monkeypatch, tmp_path):
    module = _prepare_relay_app(monkeypatch, tmp_path)

    async def reject_busy(*args, **kwargs):
        raise RelaySessionBusyError("relay_session_busy")

    monkeypatch.setattr(module._relay_transport_client, "submit_turn", reject_busy)
    asyncio.run(
        module._ws_run_hermes_job_serialized(
            None,
            "watch-001",
            "relay-1",
            "记一下日志",
        )
    )

    session = module._get_session_repo().get("watch-001", "relay-1")
    response = module._watch_response_from_session(session, "记一下日志")
    assert response.model_dump() == {
        "request_id": "relay-1",
        "status": "error",
        "action": "error",
        "asr_text": "记一下日志",
        "reply_text": "当前有任务正在处理，请稍后再试",
        "clarification_id": None,
        "error_code": "relay_session_busy",
    }
