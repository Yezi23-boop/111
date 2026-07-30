from __future__ import annotations

import base64
import hashlib
import hmac
import time

from fastapi.testclient import TestClient
from starlette.websockets import WebSocketDisconnect
import pytest


def _upgrade_token(gateway_id: str, secret: str) -> str:
    expiry = int(time.time()) + 60
    signed = f"{gateway_id}:{expiry}"
    signature = hmac.new(secret.encode(), signed.encode(), hashlib.sha256).hexdigest()
    return base64.urlsafe_b64encode(f"{signed}:{signature}".encode()).decode().rstrip("=")


def _headers() -> dict[str, str]:
    return {"Authorization": f"Bearer {_upgrade_token('gateway-poc', 'relay-test-secret')}"}


def test_health_does_not_expose_secret_or_trace(relay_app):
    response = TestClient(relay_app.app).get("/health")

    assert response.status_code == 200
    assert response.json() == {
        "status": "ok",
        "relay_contract_version": 1,
        "gateway_connected": False,
        "test_mode": True,
        "spool_configured": True,
    }


def test_private_status_exposes_connection_metrics_only(relay_app):
    response = TestClient(relay_app.app).get(
        "/internal/relay/status",
        headers={"Authorization": "Bearer relay-internal-token"},
    )

    assert response.status_code == 200
    payload = response.json()
    assert payload["connected"] is False
    assert payload["handshake_count"] == 0
    assert payload["disconnect_count"] == 0
    assert payload["replay_count"] == 0
    assert payload["pending_inbounds"] == 0
    assert payload["active_turn_request_id"] is None
    assert payload["active_turn_state"] is None
    assert "text" not in str(payload)


def test_relay_rejects_missing_upgrade_token(relay_app):
    with pytest.raises(WebSocketDisconnect) as exc_info:
        with TestClient(relay_app.app).websocket_connect("/relay"):
            pass

    assert exc_info.value.code == 4401


def test_hello_returns_narrow_watch_descriptor(relay_app):
    with TestClient(relay_app.app).websocket_connect("/relay", headers=_headers()) as websocket:
        websocket.send_json({"type": "hello", "platform": "relay", "botId": ""})
        frame = websocket.receive_json()

    descriptor = frame["descriptor"]
    assert frame["type"] == "descriptor"
    assert descriptor["contract_version"] == 1
    assert descriptor["platform"] == "relay"
    assert descriptor["supports_draft_streaming"] is False
    assert descriptor["supports_edit"] is False
    assert descriptor["supported_ops"] == ["send", "typing"]


def test_synthetic_inbound_and_outbound_trace_excludes_text(relay_app):
    client = TestClient(relay_app.app)
    test_headers = {"X-Relay-Test-Token": "test-route-token"}
    with client.websocket_connect("/relay", headers=_headers()) as websocket:
        websocket.send_json({"type": "hello", "platform": "relay", "botId": ""})
        assert websocket.receive_json()["type"] == "descriptor"

        response = client.post("/test/inbound", headers=test_headers, json={"request_id": "poc-1", "text": "private text"})
        assert response.status_code == 200
        inbound = websocket.receive_json()
        assert inbound["type"] == "inbound"
        assert inbound["event"]["message_id"] == "watch:watch-001:poc-1"

        websocket.send_json({
            "type": "outbound",
            "requestId": "out-1",
            "action": {"op": "send", "chat_id": "watch-001", "content": "private reply", "reply_to": "watch:watch-001:poc-1"},
        })
        result = websocket.receive_json()
        assert result == {
            "type": "outbound_result",
            "requestId": "out-1",
            "result": {"success": True, "message_id": "relay-delivery-out-1"},
        }

    trace_response = client.get("/test/trace", headers=test_headers)
    assert trace_response.status_code == 200
    trace = trace_response.json()["events"]
    trace_text = str(trace)
    assert "private text" not in trace_text
    assert "private reply" not in trace_text
    assert "watch:watch-001:poc-1" in trace_text
    assert "out-1" in trace_text
    outbound = next(event for event in trace if event.get("type") == "outbound")
    assert outbound["reply_to_present"] == "true"


def test_successive_turns_reuse_one_relay_handshake(relay_app):
    client = TestClient(relay_app.app)
    test_headers = {"X-Relay-Test-Token": "test-route-token"}
    internal_headers = {"Authorization": "Bearer relay-internal-token"}
    with client.websocket_connect("/relay", headers=_headers()) as websocket:
        websocket.send_json({"type": "hello", "platform": "relay", "botId": ""})
        assert websocket.receive_json()["type"] == "descriptor"

        first = client.post(
            "/test/inbound",
            headers=test_headers,
            json={"request_id": "reuse-1", "text": "第一条"},
        )
        assert first.status_code == 200
        assert websocket.receive_json()["type"] == "inbound"
        websocket.send_json(
            {
                "type": "outbound",
                "requestId": "reuse-out-1",
                "action": {
                    "op": "send",
                    "chat_id": "watch-001",
                    "content": "第一条完成",
                    "reply_to": "watch:watch-001:reuse-1",
                },
            }
        )
        assert websocket.receive_json()["type"] == "outbound_result"

        second = client.post(
            "/test/inbound",
            headers=test_headers,
            json={"request_id": "reuse-2", "text": "第二条"},
        )
        assert second.status_code == 200
        assert websocket.receive_json()["type"] == "inbound"

        status = client.get("/internal/relay/status", headers=internal_headers)
        assert status.status_code == 200
        payload = status.json()
        assert payload["connected"] is True
        assert payload["handshake_count"] == 1
        assert payload["active_turn_request_id"] == "reuse-2"
        assert payload["active_turn_state"] == "awaiting_reply"


def test_unknown_outbound_is_rejected_without_echoing_content(relay_app):
    with TestClient(relay_app.app).websocket_connect("/relay", headers=_headers()) as websocket:
        websocket.send_json({"type": "hello", "platform": "relay", "botId": ""})
        websocket.receive_json()
        websocket.send_json({
            "type": "outbound",
            "requestId": "reject-1",
            "action": {"op": "send_media", "chat_id": "watch-001", "content": "must not echo"},
        })
        result = websocket.receive_json()

    assert result["result"] == {"success": False, "error": "unsupported POC outbound action"}


def test_cancel_without_gateway_keeps_turn_retryable(relay_app):
    client = TestClient(relay_app.app)
    test_headers = {"X-Relay-Test-Token": "test-route-token"}
    internal_headers = {"Authorization": "Bearer relay-internal-token"}

    created = client.post(
        "/test/inbound",
        headers=test_headers,
        json={"request_id": "cancel-1", "text": "稍后处理"},
    )
    assert created.status_code == 200

    response = client.post(
        "/internal/relay/turn/cancel-1/cancel",
        headers=internal_headers,
        json={"device_id": "watch-001"},
    )
    assert response.status_code == 200
    assert response.json() == {
        "accepted": False,
        "error": "relay_gateway_unavailable",
    }
    assert relay_app.harness.spool.get_turn("cancel-1").state == "retryable"


def test_internal_turn_requires_private_token_and_is_idempotent(relay_app):
    client = TestClient(relay_app.app)
    body = {
        "device_id": "watch-001",
        "request_id": "internal-1",
        "text": "记录一条内部测试",
    }
    missing = client.post("/internal/relay/turn", json=body)
    assert missing.status_code == 401

    headers = {"Authorization": "Bearer relay-internal-token"}
    first = client.post("/internal/relay/turn", headers=headers, json=body)
    duplicate = client.post("/internal/relay/turn", headers=headers, json=body)
    assert first.status_code == 200
    assert duplicate.status_code == 200
    assert duplicate.json()["request_id"] == "internal-1"
    assert len(relay_app.harness.spool.pending_inbounds()) == 1

    forbidden = dict(body)
    forbidden["device_id"] = "other-device"
    assert client.post("/internal/relay/turn", headers=headers, json=forbidden).status_code == 403
