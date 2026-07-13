from __future__ import annotations

from conversation_repo import ConversationRepo, ConversationValidationError


def test_conversation_repo_keeps_recent_20_messages(tmp_path):
    repo = ConversationRepo(tmp_path / "conversation.db")

    for index in range(25):
        repo.add_message(
            device_id="watch-001",
            request_id=f"req-{index}",
            role="user" if index % 2 == 0 else "assistant",
            text=f"message {index}",
        )

    messages = repo.list_recent("watch-001")
    assert len(messages) == 20
    assert messages[0].text == "message 5"
    assert messages[-1].text == "message 24"


def test_conversation_repo_lists_after_seen_message(tmp_path):
    repo = ConversationRepo(tmp_path / "conversation.db")
    first = repo.add_message("watch-001", "req-1", "user", "first")
    second = repo.add_message("watch-001", "req-1", "assistant", "second")
    third = repo.add_message("watch-001", "req-2", "user", "third")

    del first
    messages = repo.list_after("watch-001", second.message_id)

    assert [message.message_id for message in messages] == [third.message_id]


def test_conversation_repo_unknown_last_seen_returns_recent_snapshot(tmp_path):
    repo = ConversationRepo(tmp_path / "conversation.db")
    message = repo.add_message("watch-001", "req-1", "user", "hello")

    messages = repo.list_after("watch-001", "msg_missing")

    assert [item.message_id for item in messages] == [message.message_id]


def test_conversation_repo_rejects_invalid_role(tmp_path):
    repo = ConversationRepo(tmp_path / "conversation.db")

    try:
        repo.add_message("watch-001", "req-1", "system", "hello")
    except ConversationValidationError as exc:
        assert "role" in str(exc)
    else:
        raise AssertionError("expected ConversationValidationError")


def test_conversation_repo_add_message_once_returns_existing(tmp_path):
    repo = ConversationRepo(tmp_path / "conversation.db")

    first = repo.add_message_once("watch-001", "req-1", "assistant", "完成")
    second = repo.add_message_once("watch-001", "req-1", "assistant", "完成")

    assert second.message_id == first.message_id
    assert len(repo.list_recent("watch-001")) == 1


def test_conversation_repo_rejects_conflicting_retry(tmp_path):
    repo = ConversationRepo(tmp_path / "conversation.db")
    repo.add_message_once("watch-001", "req-1", "assistant", "原结果")

    try:
        repo.add_message_once("watch-001", "req-1", "assistant", "不同结果")
    except ConversationValidationError as exc:
        assert "different content" in str(exc)
    else:
        raise AssertionError("expected ConversationValidationError")
