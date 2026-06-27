from __future__ import annotations

import importlib
import time

import pytest
from fastapi.testclient import TestClient

from conversation_repo import ConversationRepo


@pytest.fixture()
def ws_app(monkeypatch, tmp_path):
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", "watch-001=test-token")
    monkeypatch.setenv("HERMES_API_KEY", "test-hermes-key")
    monkeypatch.setenv("WATCH_WS_ENABLED", "true")
    monkeypatch.setenv("CONVERSATION_DB_PATH", str(tmp_path / "conversation.db"))
    import app

    module = importlib.reload(app)
    module._conversation_repo = ConversationRepo(tmp_path / "conversation.db")
    return module


def test_websocket_disabled_does_not_remove_http_routes(monkeypatch, tmp_path):
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", "watch-001=test-token")
    monkeypatch.setenv("HERMES_API_KEY", "test-hermes-key")
    monkeypatch.setenv("WATCH_WS_ENABLED", "false")
    monkeypatch.setenv("CONVERSATION_DB_PATH", str(tmp_path / "conversation.db"))
    import app

    module = importlib.reload(app)
    client = TestClient(module.app)

    health = client.get(
        "/v1/watch/health",
        params={"device_id": "watch-001"},
        headers={"Authorization": "Bearer test-token"},
    )
    assert health.status_code == 200

    with client.websocket_connect("/v1/watch/ws") as websocket:
        payload = websocket.receive_json()
        assert payload == {"type": "error", "error_code": "websocket_disabled"}


def test_websocket_audio_flow_returns_asr_and_reply(ws_app, monkeypatch):
    async def fake_call_hermes(device_id: str, asr_text: str, clarification_id: str | None) -> str:
        assert device_id == "watch-001"
        assert asr_text == "帮我分析电池日志"
        assert clarification_id is None
        return "分析完成，主要问题是待机耗电偏高。"

    monkeypatch.setattr(ws_app, "_call_hermes", fake_call_hermes)
    client = TestClient(ws_app.app)

    with client.websocket_connect("/v1/watch/ws") as websocket:
        websocket.send_json(
            {
                "type": "auth",
                "device_id": "watch-001",
                "device_token": "test-token",
            }
        )
        assert websocket.receive_json()["type"] == "auth_ok"
        snapshot = websocket.receive_json()
        assert snapshot["type"] == "conversation_snapshot"
        assert snapshot["messages"] == []

        websocket.send_json(
            {
                "type": "audio_start",
                "request_id": "watch-001-ws-0001",
                "format": "ogg_opus",
                "mock_asr_text": "帮我分析电池日志",
            }
        )
        assert websocket.receive_json() == {
            "type": "audio_started",
            "request_id": "watch-001-ws-0001",
        }
        websocket.send_bytes(b"OggS\x00\x01")
        websocket.send_json({"type": "audio_end", "request_id": "watch-001-ws-0001"})

        asr_result = websocket.receive_json()
        assert asr_result["type"] == "asr_result"
        assert asr_result["request_id"] == "watch-001-ws-0001"
        assert asr_result["text"] == "帮我分析电池日志"
        assert asr_result["message_id"].startswith("msg_")

        assert websocket.receive_json() == {
            "type": "task_started",
            "request_id": "watch-001-ws-0001",
        }

        reply = websocket.receive_json()
        assert reply["type"] == "conversation_message"
        assert reply["request_id"] == "watch-001-ws-0001"
        assert reply["role"] == "assistant"
        assert reply["text"] == "分析完成，主要问题是待机耗电偏高。"
        assert reply["status"] == "done"


def test_websocket_reconnect_sends_conversation_snapshot(ws_app):
    user_message = ws_app._get_conversation_repo().add_message(
        "watch-001",
        "req-1",
        "user",
        "第一条",
    )
    reply_message = ws_app._get_conversation_repo().add_message(
        "watch-001",
        "req-1",
        "assistant",
        "第二条",
    )
    client = TestClient(ws_app.app)

    with client.websocket_connect("/v1/watch/ws") as websocket:
        websocket.send_json(
            {
                "type": "auth",
                "device_id": "watch-001",
                "device_token": "test-token",
                "last_seen_conversation_id": user_message.message_id,
            }
        )
        assert websocket.receive_json()["type"] == "auth_ok"
        snapshot = websocket.receive_json()

    assert snapshot["type"] == "conversation_snapshot"
    assert [message["message_id"] for message in snapshot["messages"]] == [
        reply_message.message_id
    ]
    assert snapshot["unread_reply_count"] == 1


def test_conversation_endpoint_lists_after_message(ws_app):
    user_message = ws_app._get_conversation_repo().add_message(
        "watch-001",
        "req-1",
        "user",
        "第一条",
    )
    reply_message = ws_app._get_conversation_repo().add_message(
        "watch-001",
        "req-1",
        "assistant",
        "第二条",
    )
    client = TestClient(ws_app.app)

    response = client.get(
        "/v1/watch/conversation",
        params={"device_id": "watch-001", "after": user_message.message_id},
        headers={"Authorization": "Bearer test-token"},
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["has_more"] is False
    assert [message["message_id"] for message in payload["messages"]] == [
        reply_message.message_id
    ]
    assert "device_token" not in response.text
    assert "Authorization" not in response.text


def test_conversation_endpoint_rejects_wrong_token(ws_app):
    client = TestClient(ws_app.app)

    response = client.get(
        "/v1/watch/conversation",
        params={"device_id": "watch-001"},
        headers={"Authorization": "Bearer wrong"},
    )

    assert response.status_code == 403


def test_websocket_disconnect_after_audio_end_still_persists_reply(ws_app, monkeypatch):
    async def fake_call_hermes(device_id: str, asr_text: str, clarification_id: str | None) -> str:
        assert device_id == "watch-001"
        assert asr_text == "离页后继续处理"
        assert clarification_id is None
        return "后台回复已完成"

    monkeypatch.setattr(ws_app, "_call_hermes", fake_call_hermes)
    client = TestClient(ws_app.app)

    with client.websocket_connect("/v1/watch/ws") as websocket:
        websocket.send_json(
            {
                "type": "auth",
                "device_id": "watch-001",
                "device_token": "test-token",
            }
        )
        assert websocket.receive_json()["type"] == "auth_ok"
        assert websocket.receive_json()["type"] == "conversation_snapshot"
        websocket.send_json(
            {
                "type": "audio_start",
                "request_id": "watch-001-ws-detach",
                "format": "ogg_opus",
                "mock_asr_text": "离页后继续处理",
            }
        )
        assert websocket.receive_json()["type"] == "audio_started"
        websocket.send_bytes(b"OggS\x00\x01")
        websocket.send_json({"type": "audio_end", "request_id": "watch-001-ws-detach"})

    deadline = time.monotonic() + 2.0
    messages = []
    while time.monotonic() < deadline:
        response = client.get(
            "/v1/watch/conversation",
            params={"device_id": "watch-001"},
            headers={"Authorization": "Bearer test-token"},
        )
        assert response.status_code == 200
        messages = response.json()["messages"]
        if any(message["role"] == "assistant" for message in messages):
            break
        time.sleep(0.05)

    assert [message["role"] for message in messages] == ["user", "assistant"]
    assert messages[0]["text"] == "离页后继续处理"
    assert messages[1]["text"] == "后台回复已完成"


def test_websocket_rejects_wrong_token(ws_app):
    client = TestClient(ws_app.app)

    with client.websocket_connect("/v1/watch/ws") as websocket:
        websocket.send_json(
            {
                "type": "auth",
                "device_id": "watch-001",
                "device_token": "wrong",
            }
        )
        assert websocket.receive_json() == {
            "type": "error",
            "error_code": "invalid_device_token",
        }
