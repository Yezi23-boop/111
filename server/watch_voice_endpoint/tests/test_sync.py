from __future__ import annotations

import importlib

import pytest
from fastapi.testclient import TestClient

from conversation_repo import ConversationRepo
from inbox_repo import InboxRepo
from session_repo import SessionRepo


DEVICE_ID = "watch-001"
DEVICE_TOKEN = "test-token"
AUTH_HEADER = {"Authorization": f"Bearer {DEVICE_TOKEN}"}


@pytest.fixture()
def sync_app(monkeypatch, tmp_path):
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", f"{DEVICE_ID}={DEVICE_TOKEN}")
    monkeypatch.setenv("WATCH_INTERNAL_API_KEY", "test-internal-server-key")
    monkeypatch.setenv("HERMES_API_KEY", "test-hermes-key")
    monkeypatch.setenv("CONVERSATION_DB_PATH", str(tmp_path / "conversation.db"))
    monkeypatch.setenv("INBOX_DB_PATH", str(tmp_path / "inbox.db"))
    monkeypatch.setenv("SESSION_DB_PATH", str(tmp_path / "session.db"))
    import app

    module = importlib.reload(app)
    module._conversation_repo = ConversationRepo(tmp_path / "conversation.db")
    module._inbox_repo = InboxRepo(tmp_path / "inbox.db")
    module._session_repo = SessionRepo(tmp_path / "session.db")
    return module


@pytest.fixture()
def client(sync_app):
    return TestClient(sync_app.app)


def _auth_get(client: TestClient, **params):
    merged = {"device_id": DEVICE_ID, **params}
    return client.get("/v1/watch/sync", params=merged, headers=AUTH_HEADER)


def _create_done_session(sync_app, request_id: str, reply_text: str):
    session = sync_app._get_session_repo().create(DEVICE_ID, request_id, request_id)
    sync_app._get_session_repo().transition(
        DEVICE_ID, session.session_id, "asr_ready", user_text="帮我整理日志"
    )
    sync_app._get_session_repo().transition(DEVICE_ID, session.session_id, "running")
    message = sync_app._get_conversation_repo().add_message(
        DEVICE_ID,
        request_id,
        "assistant",
        reply_text,
        status="done",
    )
    sync_app._get_session_repo().transition(
        DEVICE_ID,
        session.session_id,
        "done",
        reply_text=reply_text,
        last_delivered_message_id=message.message_id,
    )
    return message


def test_sync_requires_authorization(client):
    response = client.get("/v1/watch/sync", params={"device_id": DEVICE_ID})

    assert response.status_code == 401


def test_background_without_pending_returns_small_schema(client):
    response = _auth_get(client, mode="background")

    assert response.status_code == 200
    payload = response.json()
    assert payload == {
        "schema_version": 1,
        "conversation": {
            "has_pending": False,
            "session_state": "none",
            "messages": [],
        },
        "inbox": {
            "unread_count": 0,
            "latest_unread": None,
        },
    }


def test_background_without_pending_does_not_return_conversation_messages(sync_app, client):
    sync_app._get_conversation_repo().add_message(DEVICE_ID, "req-1", "user", "第一条")

    response = _auth_get(client, mode="background")

    assert response.status_code == 200
    payload = response.json()
    assert payload["conversation"]["messages"] == []
    assert payload["conversation"]["session_state"] == "none"


def test_foreground_reconcile_returns_recent_messages_in_created_order(sync_app, client):
    first = sync_app._get_conversation_repo().add_message(DEVICE_ID, "req-1", "user", "第一条")
    second = sync_app._get_conversation_repo().add_message(DEVICE_ID, "req-1", "assistant", "第二条")

    response = _auth_get(client, mode="foreground_reconcile", max_messages=10)

    assert response.status_code == 200
    payload = response.json()
    assert [message["message_id"] for message in payload["conversation"]["messages"]] == [
        first.message_id,
        second.message_id,
    ]


def test_foreground_reconcile_honors_after_message_id(sync_app, client):
    first = sync_app._get_conversation_repo().add_message(DEVICE_ID, "req-1", "user", "第一条")
    second = sync_app._get_conversation_repo().add_message(DEVICE_ID, "req-1", "assistant", "第二条")

    response = _auth_get(
        client,
        mode="foreground_reconcile",
        after_message_id=first.message_id,
        max_messages=10,
    )

    assert response.status_code == 200
    payload = response.json()
    assert [message["message_id"] for message in payload["conversation"]["messages"]] == [
        second.message_id,
    ]


def test_max_messages_zero_returns_no_conversation_messages(sync_app, client):
    sync_app._get_conversation_repo().add_message(DEVICE_ID, "req-1", "user", "第一条")

    response = _auth_get(client, mode="foreground_reconcile", max_messages=0)

    assert response.status_code == 200
    assert response.json()["conversation"]["messages"] == []


@pytest.mark.parametrize("internal_state", ["accepted", "asr_ready", "running"])
def test_pending_internal_active_states_map_to_public_running(sync_app, client, internal_state):
    request_id = f"req-{internal_state}"
    session = sync_app._get_session_repo().create(DEVICE_ID, request_id, request_id)
    if internal_state in ("asr_ready", "running"):
        sync_app._get_session_repo().transition(
            DEVICE_ID, session.session_id, "asr_ready", user_text="测试"
        )
    if internal_state == "running":
        sync_app._get_session_repo().transition(DEVICE_ID, session.session_id, "running")

    response = _auth_get(
        client,
        mode="background",
        pending_request_id=request_id,
        max_messages=10,
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["conversation"]["has_pending"] is True
    assert payload["conversation"]["session_state"] == "running"
    assert payload["conversation"]["messages"] == []


def test_pending_done_returns_assistant_message_when_not_seen(sync_app, client):
    message = _create_done_session(sync_app, "req-done", "已经整理好了。")

    response = _auth_get(
        client,
        mode="background",
        pending_request_id="req-done",
        max_messages=10,
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["conversation"]["has_pending"] is False
    assert payload["conversation"]["session_state"] == "done"
    assert [item["message_id"] for item in payload["conversation"]["messages"]] == [
        message.message_id
    ]


def test_pending_done_replays_session_when_assistant_message_is_missing(
    sync_app, client
):
    repo = sync_app._get_session_repo()
    repo.create(DEVICE_ID, "req-replay", "req-replay")
    repo.transition(
        DEVICE_ID, "req-replay", "asr_ready", user_text="原问题"
    )
    repo.transition(DEVICE_ID, "req-replay", "running")
    repo.transition(
        DEVICE_ID,
        "req-replay",
        "done",
        reply_text="session 保留的回复",
        last_delivered_message_id="msg_evicted",
    )

    response = _auth_get(
        client,
        mode="background",
        pending_request_id="req-replay",
        max_messages=10,
    )

    payload = response.json()["conversation"]
    assert payload["session_state"] == "done"
    assert payload["messages"] == [
        {
            "message_id": "msg_evicted",
            "request_id": "req-replay",
            "role": "assistant",
            "text": "session 保留的回复",
            "created_at": repo.get(DEVICE_ID, "req-replay").updated_at,
            "status": "done",
        }
    ]


def test_pending_done_does_not_repeat_seen_assistant_message(sync_app, client):
    message = _create_done_session(sync_app, "req-done", "已经整理好了。")

    response = _auth_get(
        client,
        mode="background",
        pending_request_id="req-done",
        after_message_id=message.message_id,
        max_messages=10,
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["conversation"]["session_state"] == "done"
    assert payload["conversation"]["messages"] == []


def test_missing_pending_session_maps_to_none(sync_app, client):
    response = _auth_get(
        client,
        mode="background",
        pending_request_id="missing-request",
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["conversation"]["has_pending"] is False
    assert payload["conversation"]["session_state"] == "none"
    assert payload["conversation"]["messages"] == []


def test_sync_returns_latest_unread_summary_without_body_or_items(sync_app, client):
    sync_app._get_inbox_repo().create(
        DEVICE_ID,
        "note-1",
        "info",
        "第一条",
        "第一条预览",
        "第一条完整正文",
    )
    latest = sync_app._get_inbox_repo().create(
        DEVICE_ID,
        "note-2",
        "reminder",
        "第二条",
        "第二条预览",
        "第二条完整正文",
    )

    response = _auth_get(client, mode="background")

    assert response.status_code == 200
    inbox = response.json()["inbox"]
    assert inbox["unread_count"] == 2
    assert inbox["latest_unread"] == {
        "notification_id": latest.item.notification_id,
        "title": latest.item.title,
        "preview": latest.item.preview,
        "created_at": latest.item.created_at,
    }
    assert "items" not in inbox
    assert "body" not in response.text


def test_sync_does_not_mark_inbox_read(sync_app, client):
    sync_app._get_inbox_repo().create(
        DEVICE_ID,
        "note-1",
        "info",
        "标题",
        "预览",
        "完整正文",
    )

    response = _auth_get(client, mode="foreground_reconcile", max_messages=10)

    assert response.status_code == 200
    _, unread = sync_app._get_inbox_repo().list_items(DEVICE_ID)
    assert unread == 1
