"""
test_inbox.py — 阶段 1 门禁 pytest。

覆盖范围：
  - 契约字段校验（缺失、空白、超字节、kind 非法）→ HTTP 422
  - 首次创建 → 201 + created=true
  - 重复创建（相同 device_id+notification_id）→ 200 + created=false，内容不可变
  - 最近 20 条保留：第 21 条写入后最旧的一条被淘汰
  - 列表接口：空收件箱 → 200 + items=[] + unread_count=0
  - 列表接口：非空，unread_count == 未读条数
  - 标记已读：幂等，首次和重复均 200
  - 标记已读：目标不存在 → 404
  - 鉴权：缺 token → 401，设备不存在 → 403，token 错误 → 403
  - 持久化：关闭再重建 InboxRepo，数据和已读状态保留
  - 日志合规：API 层响应体不包含 Authorization/token/内部 row_id 等敏感字段
"""
from __future__ import annotations

import os
import sys
import tempfile
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

# ── 常量（必须在 env 设置和 import app 之前定义）──────────────────────────────
DEVICE_ID = "watch-test-001"
DEVICE_TOKEN = "test-token-abc"
AUTH_HEADER = {"Authorization": f"Bearer {DEVICE_TOKEN}"}

# 确保 server 目录在 import 路径
_APP_DIR = Path(__file__).resolve().parents[1]
if str(_APP_DIR) not in sys.path:
    sys.path.insert(0, str(_APP_DIR))

# ── 在 import app 之前覆盖路径和 token 环境变量 ────────────────────────────────
# app.py 使用 _get_inbox_repo() 懒初始化；测试通过设置 INBOX_DB_PATH 和直接覆盖
# app._inbox_repo 来实现隔离。_device_tokens() 每次路由调用时动态读 os.getenv，
# 所以 WATCH_DEVICE_TOKENS 在路由被调用前设好即可。
import inbox_repo as _repo_module

_tmp_dir = tempfile.mkdtemp(prefix="test_inbox_")
os.environ["INBOX_DB_PATH"] = str(Path(_tmp_dir) / "test_inbox.db")
os.environ["WATCH_DEVICE_TOKENS"] = f"{DEVICE_ID}={DEVICE_TOKEN}"

import app as _app  # noqa: E402  必须在 INBOX_DB_PATH 设好后 import


# ── Fixtures ───────────────────────────────────────────────────────────────────
@pytest.fixture(autouse=True)
def fresh_repo(tmp_path):
    """每条测试使用独立 SQLite 文件，消除测试间干扰。"""
    db_path = tmp_path / "inbox.db"
    repo = _repo_module.InboxRepo(db_path)
    _app._inbox_repo = repo
    yield repo
    _app._inbox_repo = None


@pytest.fixture(autouse=True)
def set_device_tokens(monkeypatch):
    """强制每次测试函数执行时 env 都是正确值（防止其他测试污染）。"""
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", f"{DEVICE_ID}={DEVICE_TOKEN}")




@pytest.fixture()
def client():
    with TestClient(_app.app, raise_server_exceptions=True) as c:
        yield c


# ── 工具函数 ────────────────────────────────────────────────────────────────
def _create(client, notification_id="msg-001", kind="reminder", title="标题",
            preview="预览文字", body="正文内容", device_id=DEVICE_ID,
            headers=None):
    headers = AUTH_HEADER if headers is None else headers
    payload = {"notification_id": notification_id, "kind": kind,
                "title": title, "preview": preview, "body": body}
    return client.post(
        f"/v1/watch/inbox?device_id={device_id}",
        json=payload,
        headers=headers,
    )


def _list(client, device_id=DEVICE_ID, headers=None):
    headers = AUTH_HEADER if headers is None else headers
    return client.get(f"/v1/watch/inbox?device_id={device_id}", headers=headers)


def _mark_read(client, notification_id, device_id=DEVICE_ID, headers=None):
    headers = AUTH_HEADER if headers is None else headers
    return client.post(
        f"/v1/watch/inbox/{notification_id}/read?device_id={device_id}",
        headers=headers,
    )


# ── 鉴权测试 ────────────────────────────────────────────────────────────────
class TestAuth:
    def test_missing_token_create(self, client):
        r = _create(client, headers={})
        assert r.status_code == 401

    def test_wrong_token_create(self, client):
        r = _create(client, headers={"Authorization": "Bearer wrong-token"})
        assert r.status_code == 403

    def test_unknown_device_create(self, client):
        r = _create(client, device_id="unknown-device",
                    headers={"Authorization": f"Bearer {DEVICE_TOKEN}"})
        assert r.status_code == 403

    def test_missing_token_list(self, client):
        r = _list(client, headers={})
        assert r.status_code == 401

    def test_missing_token_mark_read(self, client):
        r = _mark_read(client, "msg-001", headers={})
        assert r.status_code == 401


# ── 创建校验测试 ────────────────────────────────────────────────────────────
class TestCreateValidation:
    def test_missing_notification_id(self, client):
        r = client.post(
            f"/v1/watch/inbox?device_id={DEVICE_ID}",
            json={"kind": "reminder", "title": "t", "preview": "p", "body": "b"},
            headers=AUTH_HEADER,
        )
        assert r.status_code == 422

    def test_blank_title(self, client):
        r = _create(client, title="   ")
        assert r.status_code == 422

    def test_invalid_kind(self, client):
        r = _create(client, kind="urgent")
        assert r.status_code == 422

    def test_notification_id_too_long(self, client):
        long_id = "a" * 64  # > 63 bytes
        r = _create(client, notification_id=long_id)
        assert r.status_code == 422

    def test_title_exceeds_byte_limit(self, client):
        # 64 bytes UTF-8（> 63）
        long_title = "中" * 22  # 22×3 = 66 bytes > 63
        r = _create(client, title=long_title)
        assert r.status_code == 422

    def test_body_exceeds_byte_limit(self, client):
        long_body = "中" * 129  # 129×3 = 387 bytes > 383
        r = _create(client, body=long_body)
        assert r.status_code == 422

    def test_empty_body_field(self, client):
        r = _create(client, body="")
        assert r.status_code == 422

    def test_notification_id_with_newline(self, client):
        r = _create(client, notification_id="msg\n001")
        assert r.status_code == 422

    def test_kind_reminder_info_warning_all_valid(self, client):
        for i, kind in enumerate(["reminder", "info", "warning"]):
            r = _create(client, notification_id=f"msg-{i}", kind=kind)
            assert r.status_code == 201, f"kind={kind} 应成功"


# ── 创建幂等测试 ────────────────────────────────────────────────────────────
class TestCreateIdempotency:
    def test_first_create_returns_201(self, client):
        r = _create(client)
        assert r.status_code == 201
        data = r.json()
        assert data["created"] is True
        assert data["item"]["source"] == "hermes"
        assert data["item"]["read"] is False
        assert "row_id" not in data["item"]

    def test_duplicate_returns_200_and_false(self, client):
        _create(client)
        r = _create(client)  # 同一 notification_id
        assert r.status_code == 200
        data = r.json()
        assert data["created"] is False

    def test_duplicate_content_not_overwritten(self, client):
        _create(client, title="原始标题")
        r = _create(client, title="修改后标题")
        assert r.status_code == 200
        assert r.json()["item"]["title"] == "原始标题"

    def test_created_at_not_updated_on_retry(self, client):
        r1 = _create(client)
        t1 = r1.json()["item"]["created_at"]
        import time; time.sleep(0.01)
        r2 = _create(client)
        t2 = r2.json()["item"]["created_at"]
        assert t1 == t2

    def test_source_fixed_to_hermes(self, client):
        r = _create(client)
        assert r.json()["item"]["source"] == "hermes"


# ── 列表测试 ────────────────────────────────────────────────────────────────
class TestList:
    def test_empty_inbox_returns_200_not_404(self, client):
        r = _list(client)
        assert r.status_code == 200
        data = r.json()
        assert data["items"] == []
        assert data["unread_count"] == 0

    def test_list_shows_created_items(self, client):
        _create(client, notification_id="msg-1", title="第一条")
        _create(client, notification_id="msg-2", title="第二条")
        r = _list(client)
        assert r.status_code == 200
        data = r.json()
        assert len(data["items"]) == 2
        assert data["unread_count"] == 2

    def test_unread_count_matches_unread_items(self, client):
        _create(client, notification_id="msg-1")
        _create(client, notification_id="msg-2")
        _mark_read(client, "msg-1")
        r = _list(client)
        data = r.json()
        assert data["unread_count"] == 1
        # unread_count 必须等于 items 中 read=false 的数量
        actual_unread = sum(1 for i in data["items"] if not i["read"])
        assert actual_unread == data["unread_count"]

    def test_response_excludes_sensitive_fields(self, client):
        _create(client)
        r = _list(client)
        for item in r.json()["items"]:
            assert "row_id" not in item
            assert "token" not in item


# ── 保留策略测试（最近 20 条）──────────────────────────────────────────────
class TestRetentionPolicy:
    def test_21st_item_evicts_oldest(self, client):
        for i in range(21):
            _create(client, notification_id=f"msg-{i:03d}", title=f"消息{i}")
        r = _list(client)
        items = r.json()["items"]
        assert len(items) == 20
        # 最旧的 msg-000 已被淘汰
        ids = [item["notification_id"] for item in items]
        assert "msg-000" not in ids
        assert "msg-020" in ids

    def test_oldest_evicted_even_if_unread(self, client):
        """旧的未读消息也会被淘汰，不会阻塞新消息。"""
        for i in range(21):
            _create(client, notification_id=f"msg-{i:03d}", title=f"消息{i}")
        items = _list(client).json()["items"]
        ids = {item["notification_id"] for item in items}
        assert "msg-000" not in ids


# ── 标记已读测试 ────────────────────────────────────────────────────────────
class TestMarkRead:
    def test_mark_read_returns_200(self, client):
        _create(client, notification_id="msg-001")
        r = _mark_read(client, "msg-001")
        assert r.status_code == 200
        data = r.json()
        assert data["read"] is True
        assert data["notification_id"] == "msg-001"

    def test_mark_read_idempotent(self, client):
        _create(client, notification_id="msg-001")
        _mark_read(client, "msg-001")
        r = _mark_read(client, "msg-001")  # 第二次
        assert r.status_code == 200
        assert r.json()["read"] is True

    def test_mark_read_not_found_returns_404(self, client):
        r = _mark_read(client, "nonexistent-id")
        assert r.status_code == 404

    def test_read_flag_reflected_in_list(self, client):
        _create(client, notification_id="msg-001")
        _mark_read(client, "msg-001")
        items = _list(client).json()["items"]
        item = next(i for i in items if i["notification_id"] == "msg-001")
        assert item["read"] is True


# ── 持久化测试（关闭后重建 repo 数据保留）──────────────────────────────────
class TestPersistence:
    def test_data_survives_repo_restart(self, tmp_path):
        """关闭 InboxRepo 后用同一 db_path 重建，消息和已读状态必须保留。"""
        db_path = tmp_path / "persist_test.db"

        repo1 = _repo_module.InboxRepo(db_path)
        repo1.create("d1", "msg-001", "info", "标题", "预览", "正文")
        repo1.mark_read("d1", "msg-001")

        # 模拟容器重启：丢弃旧连接，重新连接
        repo1._conn.close()

        repo2 = _repo_module.InboxRepo(db_path)
        items, unread = repo2.list_items("d1")
        assert len(items) == 1
        assert items[0].notification_id == "msg-001"
        assert items[0].read is True
        assert unread == 0
