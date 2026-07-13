"""test_session_repo.py — V2.3 watch_session repository 单元测试。

覆盖：创建、状态转移合法路径、终态不可回退、非法转移拒绝、
启动恢复、过期淘汰、session_id/request_id 1:1 映射。
"""
from __future__ import annotations

import time

import pytest

from session_repo import SessionRepo, SessionValidationError, WatchSession


# ── helpers ───────────────────────────────────────────────────────────────────

def _create(repo: SessionRepo, device_id: str = "watch-001",
            session_id: str = "s1", request_id: str = "s1") -> WatchSession:
    return repo.create(device_id=device_id, session_id=session_id,
                       request_id=request_id)


def _transition(repo: SessionRepo, device_id: str, session_id: str,
                new_state: str, **kwargs) -> WatchSession:
    return repo.transition(device_id, session_id, new_state, **kwargs)


# ── 创建 ──────────────────────────────────────────────────────────────────────

def test_create_session_sets_accepted_state(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    session = _create(repo)
    assert session.state == "accepted"
    assert session.session_id == "s1"
    assert session.request_id == "s1"
    assert session.device_id == "watch-001"
    assert session.user_text == ""
    assert session.reply_text == ""
    assert session.last_delivered_message_id == ""
    assert session.created_at == session.updated_at


def test_create_duplicate_session_id_returns_existing(tmp_path):
    """INSERT OR IGNORE：重复 session_id 不抛异常，返回已有记录。"""
    repo = SessionRepo(tmp_path / "test.db")
    first = _create(repo, session_id="dup", request_id="dup")
    second = _create(repo, session_id="dup", request_id="dup")
    assert second.session_id == first.session_id
    assert second.state == first.state


def test_create_or_get_reports_only_first_claim_as_created(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")

    first, first_created = repo.create_or_get("watch-001", "claim", "claim")
    second, second_created = repo.create_or_get("watch-001", "claim", "claim")

    assert first_created is True
    assert second_created is False
    assert second.session_id == first.session_id


def test_create_rejects_blank_device_id(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    for blank in ("", "   "):
        with pytest.raises(SessionValidationError, match="device_id"):
            repo.create(device_id=blank, session_id="s1", request_id="s1")


def test_create_rejects_blank_session_id(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    with pytest.raises(SessionValidationError, match="session_id"):
        repo.create(device_id="watch-001", session_id="", request_id="s1")


def test_create_rejects_blank_request_id(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    with pytest.raises(SessionValidationError, match="request_id"):
        repo.create(device_id="watch-001", session_id="s1", request_id="")


def test_create_rejects_session_id_different_from_request_id(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    with pytest.raises(SessionValidationError, match="must equal"):
        repo.create_or_get("watch-001", "session-1", "request-1")


# ── 状态转移 ─────────────────────────────────────────────────────────────────

def test_full_transition_path_accepted_to_done(tmp_path):
    """accepted -> asr_ready -> running -> done。"""
    repo = SessionRepo(tmp_path / "test.db")

    s = _create(repo, session_id="full", request_id="full")
    assert s.state == "accepted"

    s = _transition(repo, "watch-001", "full", "asr_ready",
                    user_text="帮我分析电池日志")
    assert s.state == "asr_ready"
    assert s.user_text == "帮我分析电池日志"

    s = _transition(repo, "watch-001", "full", "running")
    assert s.state == "running"

    s = _transition(repo, "watch-001", "full", "done",
                    reply_text="分析完成，待机耗电偏高。")
    assert s.state == "done"
    assert s.reply_text == "分析完成，待机耗电偏高。"


def test_accepted_can_be_canceled(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="c1", request_id="c1")
    s = _transition(repo, "watch-001", "c1", "canceled")
    assert s.state == "canceled"


@pytest.mark.parametrize("terminal_state", ["error", "timeout"])
def test_accepted_can_transition_to_terminal_before_asr_ready(tmp_path, terminal_state):
    """ASR 前置阶段失败时，session 不能残留在 accepted 假 pending。"""
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id=f"early-{terminal_state}", request_id=f"early-{terminal_state}")
    s = _transition(repo, "watch-001", f"early-{terminal_state}", terminal_state)
    assert s.state == terminal_state


def test_asr_ready_can_transition_to_error(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="e1", request_id="e1")
    _transition(repo, "watch-001", "e1", "asr_ready", user_text="测试")
    s = _transition(repo, "watch-001", "e1", "error")
    assert s.state == "error"


def test_asr_ready_can_transition_to_timeout(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="t1", request_id="t1")
    _transition(repo, "watch-001", "t1", "asr_ready", user_text="测试")
    s = _transition(repo, "watch-001", "t1", "timeout")
    assert s.state == "timeout"


def test_running_can_transition_to_error(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="re1", request_id="re1")
    _transition(repo, "watch-001", "re1", "asr_ready", user_text="测试")
    _transition(repo, "watch-001", "re1", "running")
    s = _transition(repo, "watch-001", "re1", "error")
    assert s.state == "error"


# ── 终态不可回退 ─────────────────────────────────────────────────────────────

@pytest.mark.parametrize(
    "terminal_state", ["done", "error", "timeout", "canceled", "interrupted"]
)
def test_terminal_state_cannot_transition(tmp_path, terminal_state):
    """终态不可转移到任何其他状态。"""
    repo = SessionRepo(tmp_path / "test.db")
    sid = f"term-{terminal_state}"

    # 手动构造终态：accepted -> asr_ready -> running -> terminal
    _create(repo, session_id=sid, request_id=sid)
    _transition(repo, "watch-001", sid, "asr_ready", user_text="测试")
    _transition(repo, "watch-001", sid, "running")
    _transition(repo, "watch-001", sid, terminal_state)

    for attempt in ("accepted", "asr_ready", "running", "done", "error"):
        with pytest.raises(SessionValidationError, match="illegal transition"):
            _transition(repo, "watch-001", sid, attempt)


def test_unknown_state_rejected(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="u1", request_id="u1")
    with pytest.raises(SessionValidationError, match="unknown state"):
        _transition(repo, "watch-001", "u1", "fantasy")


def test_skip_step_transition_rejected(tmp_path):
    """accepted 不能直接跳到 running。"""
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="skip", request_id="skip")
    with pytest.raises(SessionValidationError, match="illegal transition"):
        _transition(repo, "watch-001", "skip", "running")


def test_accepted_cannot_transition_to_done(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="ad", request_id="ad")
    with pytest.raises(SessionValidationError, match="illegal transition"):
        _transition(repo, "watch-001", "ad", "done")


# ── 读取 ──────────────────────────────────────────────────────────────────────

def test_get_raises_on_missing(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    with pytest.raises(SessionValidationError, match="session not found"):
        repo.get("watch-001", "no_such_session")


def test_get_by_request_id_finds_session(tmp_path):
    """V2.3: session_id == request_id 时 get_by_request_id 可找到。"""
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="req-abc", request_id="req-abc")
    s = repo.get_by_request_id("watch-001", "req-abc")
    assert s.session_id == "req-abc"


def test_get_by_request_id_raises_on_missing(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    with pytest.raises(SessionValidationError, match="session not found by request_id"):
        repo.get_by_request_id("watch-001", "no_such_request")


def test_list_active_only_returns_non_terminal(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    active_ids = {"a1", "a2"}
    for sid in ("a1", "a2", "done1", "err1"):
        _create(repo, session_id=sid, request_id=sid)
    _transition(repo, "watch-001", "a1", "asr_ready", user_text="1")
    _transition(repo, "watch-001", "a2", "asr_ready", user_text="2")
    _transition(repo, "watch-001", "a2", "running")
    _transition(repo, "watch-001", "a2", "done", reply_text="done")

    # 完成 done1
    _create(repo, session_id="done1", request_id="done1")
    _transition(repo, "watch-001", "done1", "asr_ready", user_text="d")
    _transition(repo, "watch-001", "done1", "running")
    _transition(repo, "watch-001", "done1", "done")

    # 完成 err1
    _create(repo, session_id="err1", request_id="err1")
    _transition(repo, "watch-001", "err1", "canceled")

    active = repo.list_active("watch-001")
    active_ids_found = {s.session_id for s in active}
    # a1 还在 asr_ready（非终态），a2 已 done（终态）
    assert "a1" in active_ids_found
    assert "a2" not in active_ids_found  # done
    assert "done1" not in active_ids_found  # done
    assert "err1" not in active_ids_found   # canceled


def test_list_recent_returns_ordered_by_updated_at(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    for i in range(5):
        sid = f"r{i}"
        _create(repo, session_id=sid, request_id=sid)
        time.sleep(1.1)  # 确保 updated_at 跨秒递增

    recent = repo.list_recent("watch-001")
    assert len(recent) == 5
    # 按 updated_at DESC 排列
    assert recent[0].session_id == "r4"
    assert recent[-1].session_id == "r0"


# ── 启动恢复 ──────────────────────────────────────────────────────────────────

def test_startup_recovery_preserves_only_safely_resumable_sessions(tmp_path):
    """重启后只保留可续跑的 asr_ready 和带 run_id 的 running。"""
    repo1 = SessionRepo(tmp_path / "test.db")
    _create(repo1, session_id="s_acc", request_id="s_acc")
    _create(repo1, session_id="s_asr", request_id="s_asr")
    _transition(repo1, "watch-001", "s_asr", "asr_ready", user_text="test")
    _create(repo1, session_id="s_run", request_id="s_run")
    _transition(repo1, "watch-001", "s_run", "asr_ready", user_text="test")
    _transition(repo1, "watch-001", "s_run", "running")
    _create(repo1, session_id="s_run_attached", request_id="s_run_attached")
    _transition(repo1, "watch-001", "s_run_attached", "asr_ready", user_text="test")
    _transition(repo1, "watch-001", "s_run_attached", "running")
    repo1.attach_hermes_run("watch-001", "s_run_attached", "run_abc")
    # 终态保持不变
    _create(repo1, session_id="s_done", request_id="s_done")
    _transition(repo1, "watch-001", "s_done", "asr_ready", user_text="d")
    _transition(repo1, "watch-001", "s_done", "running")
    _transition(repo1, "watch-001", "s_done", "done")

    # 模拟重启：创建新 repo 实例（同一 db 文件）
    repo2 = SessionRepo(tmp_path / "test.db")

    assert repo2.get("watch-001", "s_acc").state == "interrupted"
    assert repo2.get("watch-001", "s_acc").error_code == "server_restarted_before_asr"
    assert repo2.get("watch-001", "s_asr").state == "asr_ready"
    assert repo2.get("watch-001", "s_run").state == "interrupted"
    assert repo2.get("watch-001", "s_run").error_code == "server_restarted_without_run_id"
    assert repo2.get("watch-001", "s_run_attached").state == "running"
    assert repo2.get("watch-001", "s_run_attached").hermes_run_id == "run_abc"
    assert repo2.get("watch-001", "s_done").state == "done"


def test_attach_hermes_run_requires_running_session(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="attach", request_id="attach")

    with pytest.raises(SessionValidationError, match="not running"):
        repo.attach_hermes_run("watch-001", "attach", "run_abc")


def test_attach_hermes_run_persists_absolute_start_time(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="timed", request_id="timed")
    _transition(repo, "watch-001", "timed", "asr_ready", user_text="test")
    _transition(repo, "watch-001", "timed", "running")

    session = repo.attach_hermes_run("watch-001", "timed", "run_timed")

    assert session.hermes_run_id == "run_timed"
    assert session.run_started_at.endswith("Z")


def test_transition_persists_clarification_id_for_restart(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="clarify", request_id="clarify")

    session = repo.transition(
        "watch-001",
        "clarify",
        "asr_ready",
        user_text="补充内容",
        clarification_id="question-123",
    )

    assert session.clarification_id == "question-123"


def test_list_active_all_returns_resumable_sessions(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="ready", request_id="ready")
    _transition(repo, "watch-001", "ready", "asr_ready", user_text="test")
    _create(repo, session_id="running", request_id="running")
    _transition(repo, "watch-001", "running", "asr_ready", user_text="test")
    _transition(repo, "watch-001", "running", "running")
    repo.attach_hermes_run("watch-001", "running", "run_abc")

    assert [item.session_id for item in repo.list_active_all()] == ["ready", "running"]


# ── 淘汰 ──────────────────────────────────────────────────────────────────────

def test_expire_old_removes_old_terminal_sessions(tmp_path):
    """过期终态 session 应被删除。"""
    repo = SessionRepo(tmp_path / "test.db")
    sid = "old_done"
    _create(repo, session_id=sid, request_id=sid)
    _transition(repo, "watch-001", sid, "asr_ready", user_text="old")
    _transition(repo, "watch-001", sid, "running")
    _transition(repo, "watch-001", sid, "done")

    # 手动把 updated_at 改到 25 小时前
    old_time = __import__("datetime").datetime.now(
        __import__("datetime").timezone.utc
    ) - __import__("datetime").timedelta(hours=25)
    old_time_str = old_time.strftime("%Y-%m-%dT%H:%M:%SZ")
    repo._conn.execute(
        "UPDATE watch_session SET updated_at=? WHERE session_id=?",
        (old_time_str, sid),
    )

    deleted = repo.expire_old()
    assert deleted >= 1

    with pytest.raises(SessionValidationError, match="session not found"):
        repo.get("watch-001", sid)


def test_expire_old_keeps_recent_terminal_sessions(tmp_path):
    """近期终态 session 不被淘汰。"""
    repo = SessionRepo(tmp_path / "test.db")
    sid = "recent_done"
    _create(repo, session_id=sid, request_id=sid)
    _transition(repo, "watch-001", sid, "asr_ready", user_text="r")
    _transition(repo, "watch-001", sid, "running")
    _transition(repo, "watch-001", sid, "done")

    deleted = repo.expire_old()
    # 刚创建的不会被淘汰
    s = repo.get("watch-001", sid)
    assert s.state == "done"


def test_expire_old_keeps_active_sessions_even_if_old(tmp_path):
    """活跃 session（非终态）即使超过 24h 也不被时间淘汰。"""
    repo = SessionRepo(tmp_path / "test.db")
    sid = "old_active"
    _create(repo, session_id=sid, request_id=sid)

    old_time = __import__("datetime").datetime.now(
        __import__("datetime").timezone.utc
    ) - __import__("datetime").timedelta(hours=25)
    old_time_str = old_time.strftime("%Y-%m-%dT%H:%M:%SZ")
    repo._conn.execute(
        "UPDATE watch_session SET updated_at=? WHERE session_id=?",
        (old_time_str, sid),
    )

    deleted = repo.expire_old()
    # expire_old 只删终态过期的，不删 accepted
    s = repo.get("watch-001", sid)
    assert s.state == "accepted"


def test_expire_caps_at_max_sessions(tmp_path):
    """超过 MAX_SESSIONS 条时删除最旧的。"""
    repo = SessionRepo(tmp_path / "test.db")
    for i in range(150):
        sid = f"s{i:04d}"
        _create(repo, session_id=sid, request_id=sid)
        # 全部标记为 done 以便淘汰
        _transition(repo, "watch-001", sid, "asr_ready", user_text=str(i))
        _transition(repo, "watch-001", sid, "running")
        _transition(repo, "watch-001", sid, "done")

    repo.expire_old()
    recent = repo.list_recent("watch-001")
    assert len(recent) <= 100  # MAX_SESSIONS


# ── V2.3 约定：session_id == request_id ──────────────────────────────────────

def test_session_id_equals_request_id_v23_convention(tmp_path):
    """V2.3 调用约定：session_id 必须等于 request_id。"""
    repo = SessionRepo(tmp_path / "test.db")
    sid = "watch-001-abc123"
    s = _create(repo, session_id=sid, request_id=sid)
    assert s.session_id == s.request_id
    # get_by_request_id 返回同一 session
    s2 = repo.get_by_request_id("watch-001", sid)
    assert s2.session_id == sid


def test_updated_at_changes_after_transition(tmp_path):
    """每次状态转移应更新 updated_at。"""
    repo = SessionRepo(tmp_path / "test.db")
    s = _create(repo, session_id="time_test", request_id="time_test")
    created = s.updated_at
    time.sleep(1.1)  # 跨秒确保 updated_at 变化
    s = _transition(repo, "watch-001", "time_test", "asr_ready", user_text="t")
    assert s.updated_at != created


def test_last_delivered_message_id_updatable(tmp_path):
    repo = SessionRepo(tmp_path / "test.db")
    _create(repo, session_id="ldm", request_id="ldm")
    _transition(repo, "watch-001", "ldm", "asr_ready", user_text="test")
    s = _transition(repo, "watch-001", "ldm", "running",
                    last_delivered_message_id="msg_abc123")
    assert s.last_delivered_message_id == "msg_abc123"
