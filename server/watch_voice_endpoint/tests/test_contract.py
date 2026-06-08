from __future__ import annotations

import json
import importlib
from pathlib import Path
from typing import get_args

import pytest


@pytest.fixture()
def watch_app(monkeypatch):
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", "watch-001=test-token")
    monkeypatch.setenv("HERMES_API_KEY", "test-hermes-key")
    monkeypatch.setenv("WATCH_REPLY_MAX_CHARS", "80")
    import app

    return importlib.reload(app)


def _field_annotation(model, field_name: str):
    if hasattr(model, "model_fields"):
        return model.model_fields[field_name].annotation
    return model.__fields__[field_name].outer_type_


def test_watch_contract_matches_response_model(watch_app):
    contract_path = Path(__file__).resolve().parents[1] / "watch_contract.v1.json"
    contract = json.loads(contract_path.read_text(encoding="utf-8"))

    response_contract = contract["response"]
    assert response_contract["required"] == list(watch_app.WatchResponse.model_fields.keys())
    assert response_contract["additional_properties"] is False
    assert response_contract["status_enum"] == list(
        get_args(_field_annotation(watch_app.WatchResponse, "status"))
    )
    assert response_contract["action_enum"] == list(
        get_args(_field_annotation(watch_app.WatchResponse, "action"))
    )
    assert response_contract["reply_max_chars"] == watch_app.REPLY_MAX_CHARS


def test_watch_contract_matches_request_limits(watch_app):
    contract_path = Path(__file__).resolve().parents[1] / "watch_contract.v1.json"
    contract = json.loads(contract_path.read_text(encoding="utf-8"))

    assert contract["request"]["request_id_pattern"] == watch_app.REQUEST_ID_PATTERN.pattern
    assert contract["request"]["max_audio_bytes"] == watch_app.MAX_AUDIO_BYTES
    assert contract["timeouts"]["server_request_timeout_seconds"] == watch_app.WATCH_REQUEST_TIMEOUT_SECONDS
    assert contract["timeouts"]["watch_wait_seconds"] > contract["timeouts"]["server_request_timeout_seconds"]
    assert contract["security"]["public_proxy_scope"] == "/v1/watch/*"
