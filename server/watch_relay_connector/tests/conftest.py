from __future__ import annotations

import importlib
import sys
from pathlib import Path

import pytest


SERVICE_DIR = Path(__file__).resolve().parents[1]
if str(SERVICE_DIR) not in sys.path:
    sys.path.insert(0, str(SERVICE_DIR))


@pytest.fixture()
def relay_app(monkeypatch, tmp_path):
    monkeypatch.setenv("GATEWAY_RELAY_ID", "gateway-poc")
    monkeypatch.setenv("GATEWAY_RELAY_SECRET", "relay-test-secret")
    monkeypatch.setenv("WATCH_RELAY_INTERNAL_TOKEN", "relay-internal-token")
    monkeypatch.setenv("WATCH_RELAY_DB_PATH", str(tmp_path / "relay.db"))
    monkeypatch.setenv("WATCH_RELAY_TEST_MODE", "true")
    monkeypatch.setenv("WATCH_RELAY_TEST_TOKEN", "test-route-token")
    import app

    return importlib.reload(app)
