from __future__ import annotations

import importlib
import asyncio
import time

import pytest
from fastapi.testclient import TestClient

from conversation_repo import ConversationRepo
from session_repo import SessionRepo


@pytest.fixture()
def ws_app(monkeypatch, tmp_path):
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", "watch-001=test-token")
    monkeypatch.setenv("HERMES_API_KEY", "test-hermes-key")
    monkeypatch.setenv("WATCH_WS_ENABLED", "true")
    monkeypatch.setenv("CONVERSATION_DB_PATH", str(tmp_path / "conversation.db"))
    monkeypatch.setenv("SESSION_DB_PATH", str(tmp_path / "session.db"))
    import app

    module = importlib.reload(app)
    module._conversation_repo = ConversationRepo(tmp_path / "conversation.db")
    module._session_repo = SessionRepo(tmp_path / "session.db")
    module._watch_run_tasks.clear()
    module._ws_background_tasks.clear()
    module._device_run_locks.clear()
    module._ws_request_status_counts.clear()
    module._last_ws_request_summary.clear()
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
    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        assert device_id == "watch-001"
        assert request_id == "watch-001-ws-0001"
        assert asr_text == "帮我分析电池日志"
        return "run-test-1"

    async def fake_get(run_id: str) -> dict[str, object]:
        assert run_id == "run-test-1"
        return {"status": "completed", "output": "分析完成，主要问题是待机耗电偏高。"}

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)
    monkeypatch.setattr(ws_app, "_get_hermes_run", fake_get)
    monkeypatch.setattr(ws_app, "HERMES_RUN_POLL_INTERVAL_SECONDS", 0)
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
    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        assert device_id == "watch-001"
        assert request_id == "watch-001-ws-detach"
        assert asr_text == "离页后继续处理"
        return "run-detach"

    async def fake_get(run_id: str) -> dict[str, object]:
        assert run_id == "run-detach"
        return {"status": "completed", "output": "后台回复已完成"}

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)
    monkeypatch.setattr(ws_app, "_get_hermes_run", fake_get)
    monkeypatch.setattr(ws_app, "HERMES_RUN_POLL_INTERVAL_SECONDS", 0)
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


@pytest.mark.anyio
async def test_concurrent_duplicate_request_starts_one_hermes_run(ws_app, monkeypatch):
    start_count = 0

    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        nonlocal start_count
        start_count += 1
        return "run-once"

    async def fake_get(run_id: str) -> dict[str, object]:
        return {"status": "completed", "output": "只执行一次"}

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)
    monkeypatch.setattr(ws_app, "_get_hermes_run", fake_get)
    monkeypatch.setattr(ws_app, "HERMES_RUN_POLL_INTERVAL_SECONDS", 0)

    await asyncio.gather(
        ws_app._ws_finish_audio(None, "watch-001", "watch-001-race", b"OggS", "测试"),
        ws_app._ws_finish_audio(None, "watch-001", "watch-001-race", b"OggS", "测试"),
    )
    for _ in range(50):
        if not ws_app._watch_run_tasks:
            break
        await asyncio.sleep(0.01)

    assert start_count == 1
    messages = ws_app._get_conversation_repo().list_recent("watch-001")
    assert [message.role for message in messages] == ["user", "assistant"]


@pytest.mark.anyio
async def test_terminal_session_without_message_never_restarts_hermes(ws_app, monkeypatch):
    session_repo = ws_app._get_session_repo()
    session_repo.create("watch-001", "watch-001-replay", "watch-001-replay")
    session_repo.transition(
        "watch-001", "watch-001-replay", "asr_ready", user_text="原问题"
    )
    session_repo.transition("watch-001", "watch-001-replay", "running")
    session_repo.transition(
        "watch-001",
        "watch-001-replay",
        "done",
        reply_text="原结果",
        last_delivered_message_id="msg_evicted",
    )
    starts = 0

    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        nonlocal starts
        starts += 1
        return "run-should-not-start"

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)

    await ws_app._ws_finish_audio(
        None, "watch-001", "watch-001-replay", b"OggS", "新问题"
    )

    assert starts == 0
    assert session_repo.get("watch-001", "watch-001-replay").reply_text == "原结果"


@pytest.mark.anyio
async def test_cancel_running_ws_run_stops_delivery(ws_app, monkeypatch):
    started = asyncio.Event()
    stopped: list[str] = []

    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        return "run-cancel"

    async def fake_get(run_id: str) -> dict[str, object]:
        started.set()
        await asyncio.sleep(60)
        return {"status": "completed", "output": "不应投递"}

    async def fake_stop(run_id: str) -> None:
        stopped.append(run_id)

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)
    monkeypatch.setattr(ws_app, "_get_hermes_run", fake_get)
    monkeypatch.setattr(ws_app, "_stop_hermes_run", fake_stop)

    await ws_app._ws_finish_audio(
        None, "watch-001", "watch-001-cancel", b"OggS", "取消测试"
    )
    await asyncio.wait_for(started.wait(), timeout=1)
    response = await ws_app.cancel_request(
        request_id="watch-001-cancel",
        device_id="watch-001",
        authorization="Bearer test-token",
    )
    await asyncio.sleep(0)

    assert response.status == "canceled"
    assert stopped == ["run-cancel"]
    assert ws_app._get_session_repo().get(
        "watch-001", "watch-001-cancel"
    ).state == "canceled"
    assert ws_app._get_conversation_repo().get_for_request_role(
        "watch-001", "watch-001-cancel", "assistant"
    ) is None


@pytest.mark.anyio
async def test_same_device_hermes_runs_are_serialized(ws_app, monkeypatch):
    first_poll_started = asyncio.Event()
    release_first = asyncio.Event()
    started_requests: list[str] = []

    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        started_requests.append(request_id)
        return f"run-{request_id}"

    async def fake_get(run_id: str) -> dict[str, object]:
        if run_id.endswith("serial-1"):
            first_poll_started.set()
            await release_first.wait()
        return {"status": "completed", "output": "处理完成"}

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)
    monkeypatch.setattr(ws_app, "_get_hermes_run", fake_get)
    monkeypatch.setattr(ws_app, "HERMES_RUN_POLL_INTERVAL_SECONDS", 0)

    await ws_app._ws_finish_audio(
        None, "watch-001", "watch-001-serial-1", b"OggS", "第一个任务"
    )
    await asyncio.wait_for(first_poll_started.wait(), timeout=1)
    await ws_app._ws_finish_audio(
        None, "watch-001", "watch-001-serial-2", b"OggS", "第二个任务"
    )
    await asyncio.sleep(0.02)

    assert started_requests == ["watch-001-serial-1"]
    release_first.set()
    for _ in range(100):
        if not ws_app._watch_run_tasks:
            break
        await asyncio.sleep(0.01)

    assert started_requests == ["watch-001-serial-1", "watch-001-serial-2"]


@pytest.mark.anyio
async def test_ws_request_metrics_cover_main_path_without_private_text(
    ws_app, monkeypatch
):
    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        return "run-metrics"

    async def fake_get(run_id: str) -> dict[str, object]:
        return {"status": "completed", "output": "这是不能进入指标的回复"}

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)
    monkeypatch.setattr(ws_app, "_get_hermes_run", fake_get)
    monkeypatch.setattr(ws_app, "HERMES_RUN_POLL_INTERVAL_SECONDS", 0)

    await ws_app._ws_finish_audio(
        None,
        "watch-001",
        "watch-001-metrics",
        b"OggS-metrics",
        "这是不能进入指标的转写",
        upload_ms=17,
    )
    for _ in range(100):
        if not ws_app._watch_run_tasks:
            break
        await asyncio.sleep(0.01)

    health = await ws_app.service_health()
    last = health["last_ws_request"]
    assert health["ws_request_status_counts"]["done"] == 1
    assert last["request_id"] == "watch-001-metrics"
    assert last["upload_bytes"] == len(b"OggS-metrics")
    assert last["upload_ms"] == 17
    assert last["asr_ms"] >= 0
    assert last["queue_wait_ms"] >= 0
    assert last["hermes_ms"] >= 0
    assert last["persist_ms"] >= 0
    assert last["delivery_mode"] == "sync"
    assert last["terminal_state"] == "done"
    rendered = str(last)
    assert "不能进入指标" not in rendered


@pytest.mark.anyio
async def test_ws_run_outlives_legacy_http_wait_timeout(ws_app, monkeypatch):
    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        return "run-longer-than-http"

    async def fake_get(run_id: str) -> dict[str, object]:
        await asyncio.sleep(0.02)
        return {"status": "completed", "output": "后台长任务完成"}

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)
    monkeypatch.setattr(ws_app, "_get_hermes_run", fake_get)
    monkeypatch.setattr(ws_app, "WATCH_REQUEST_TIMEOUT_SECONDS", 0.001)
    monkeypatch.setattr(ws_app, "HERMES_RUN_TIMEOUT_SECONDS", 1)
    monkeypatch.setattr(ws_app, "HERMES_RUN_POLL_INTERVAL_SECONDS", 0)

    await ws_app._ws_finish_audio(
        None,
        "watch-001",
        "watch-001-long-run",
        b"OggS",
        "执行一个长任务",
    )
    await asyncio.sleep(0.005)
    assert ws_app._get_session_repo().get(
        "watch-001", "watch-001-long-run"
    ).state == "running"

    for _ in range(100):
        if not ws_app._watch_run_tasks:
            break
        await asyncio.sleep(0.01)

    session = ws_app._get_session_repo().get(
        "watch-001", "watch-001-long-run"
    )
    assert session.state == "done"
    assert session.reply_text == "后台长任务完成"


@pytest.mark.anyio
async def test_http_and_ws_share_one_session_claim(ws_app, monkeypatch):
    starts = 0

    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        nonlocal starts
        starts += 1
        return "run-shared-transport"

    async def fake_get(run_id: str) -> dict[str, object]:
        await asyncio.sleep(0.01)
        return {"status": "completed", "output": "跨协议只执行一次"}

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)
    monkeypatch.setattr(ws_app, "_get_hermes_run", fake_get)
    monkeypatch.setattr(ws_app, "HERMES_RUN_POLL_INTERVAL_SECONDS", 0)

    http_task = asyncio.create_task(
        ws_app._process_voice_command(
            request_id="watch-001-shared-transport",
            device_id="watch-001",
            audio_bytes=b"OggS",
            audio_content_type="audio/ogg",
            clarification_id=None,
            mock_asr_text="同一个请求",
        )
    )
    await asyncio.sleep(0)
    await ws_app._ws_finish_audio(
        None,
        "watch-001",
        "watch-001-shared-transport",
        b"OggS",
        "同一个请求",
    )
    response = await http_task

    assert response.status == "done"
    assert response.reply_text == "跨协议只执行一次"
    assert starts == 1


@pytest.mark.anyio
async def test_http_caller_timeout_keeps_server_session_running(ws_app, monkeypatch):
    async def fake_start(device_id: str, request_id: str, asr_text: str) -> str:
        return "run-http-detached"

    async def fake_get(run_id: str) -> dict[str, object]:
        await asyncio.sleep(0.03)
        return {"status": "completed", "output": "调用者离开后仍完成"}

    monkeypatch.setattr(ws_app, "_start_hermes_run", fake_start)
    monkeypatch.setattr(ws_app, "_get_hermes_run", fake_get)
    monkeypatch.setattr(ws_app, "WATCH_REQUEST_TIMEOUT_SECONDS", 0.001)
    monkeypatch.setattr(ws_app, "HERMES_RUN_POLL_INTERVAL_SECONDS", 0)

    response = await ws_app._process_text_command(
        request_id="watch-001-http-detached",
        device_id="watch-001",
        text="执行长任务",
        clarification_id=None,
    )

    assert response.status == "timeout"
    assert ws_app._get_session_repo().get(
        "watch-001", "watch-001-http-detached"
    ).state == "running"
    for _ in range(100):
        if not ws_app._watch_run_tasks:
            break
        await asyncio.sleep(0.01)
    assert ws_app._get_session_repo().get(
        "watch-001", "watch-001-http-detached"
    ).state == "done"
