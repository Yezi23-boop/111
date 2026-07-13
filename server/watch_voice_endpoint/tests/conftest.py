from __future__ import annotations

import sys
from pathlib import Path

import pytest


APP_DIR = Path(__file__).resolve().parents[1]
if str(APP_DIR) not in sys.path:
    sys.path.insert(0, str(APP_DIR))


@pytest.fixture(autouse=True)
def _default_internal_server_key(monkeypatch):
    """所有 endpoint 测试默认满足 V2.5 readiness 的 internal credential。"""
    monkeypatch.setenv("WATCH_INTERNAL_API_KEY", "test-internal-server-key")
