from __future__ import annotations

import pytest

from relay_spool import RelaySpool, RelaySpoolError


def test_turn_and_delivery_link_survive_spool_restart(tmp_path):
    db_path = tmp_path / "relay.db"
    spool = RelaySpool(db_path)
    turn = spool.claim_turn(
        "watch-001",
        "req-1",
        "记一下电池日志",
        chat_id="watch-001",
        session_key="agent:main:relay:dm:watch-001",
    )
    spool.put_inbound_frame("req-1", '{"type":"inbound"}', turn.message_id)
    spool.mark_inbound_sent("req-1")
    spool.close()

    reopened = RelaySpool(db_path)
    assert reopened.pending_inbounds() == [("req-1", '{"type":"inbound"}')]
    delivery = reopened.resolve_delivery(
        chat_id="watch-001",
        gateway_request_id="gateway-send-1",
        content="已记录",
        explicit_reply_to=None,
    )
    assert delivery is not None
    assert delivery.request_id == "req-1"
    assert delivery.reply_to == turn.message_id
    reopened.mark_delivery_result(delivery.delivery_id, True)
    assert reopened.pending_inbounds() == []
    assert reopened.active_turn("watch-001") is None


def test_only_one_active_watch_turn_is_allowed(tmp_path):
    spool = RelaySpool(tmp_path / "relay.db")
    spool.claim_turn("watch-001", "req-1", "one", chat_id="watch-001")

    with pytest.raises(RelaySpoolError, match="active chat"):
        spool.claim_turn("watch-001", "req-2", "two", chat_id="watch-001")


def test_retryable_turn_still_blocks_new_watch_turn(tmp_path):
    spool = RelaySpool(tmp_path / "relay.db")
    turn = spool.claim_turn("watch-001", "req-1", "one", chat_id="watch-001")
    spool.set_turn_state(turn.request_id, "retryable")

    with pytest.raises(RelaySpoolError, match="active chat"):
        spool.claim_turn("watch-001", "req-2", "two", chat_id="watch-001")

    assert spool.active_turn("watch-001").request_id == "req-1"


def test_unbound_outbound_cannot_be_assigned_to_old_turn(tmp_path):
    spool = RelaySpool(tmp_path / "relay.db")
    turn = spool.claim_turn("watch-001", "req-1", "one", chat_id="watch-001")
    spool.set_turn_state(turn.request_id, "completed")

    assert (
        spool.resolve_delivery(
            chat_id="watch-001",
            gateway_request_id="gateway-send-2",
            content="late message",
            explicit_reply_to=None,
        )
        is None
    )


def test_mismatched_explicit_reply_link_is_rejected(tmp_path):
    spool = RelaySpool(tmp_path / "relay.db")
    turn = spool.claim_turn("watch-001", "req-1", "one", chat_id="watch-001")
    spool.put_inbound_frame("req-1", '{"type":"inbound"}', turn.message_id)
    spool.mark_inbound_sent("req-1")

    assert (
        spool.resolve_delivery(
            chat_id="watch-001",
            gateway_request_id="gateway-send-1",
            content="reply",
            explicit_reply_to="watch:watch-001:other-request",
        )
        is None
    )
