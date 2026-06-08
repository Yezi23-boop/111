from __future__ import annotations

import inspect
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


def _fastapi_default(callable_obj, parameter_name: str):
    default = inspect.signature(callable_obj).parameters[parameter_name].default
    return getattr(default, "default", default)


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


def test_watch_contract_matches_voice_command_form_fields(watch_app):
    contract_path = Path(__file__).resolve().parents[1] / "watch_contract.v1.json"
    contract = json.loads(contract_path.read_text(encoding="utf-8"))

    public_fields = set(contract["request"]["voice_command_fields"])
    accepted_parameters = set(inspect.signature(watch_app.voice_command).parameters.keys())
    dev_only_fields = {"mock_asr_text"}
    transport_only_fields = {"authorization"}

    assert public_fields <= accepted_parameters
    assert accepted_parameters - public_fields - dev_only_fields - transport_only_fields == set()
    assert _fastapi_default(watch_app.voice_command, "locale") == contract["request"]["locale"]
    assert _fastapi_default(watch_app.voice_command, "timezone") == contract["request"]["timezone"]
    assert _fastapi_default(watch_app.voice_command, "source") == contract["request"]["source"]


def test_release_gate_runs_pytest_and_acceptance_without_expanding_env_file():
    script_path = Path(__file__).resolve().parents[1] / "release_gate.ps1"
    source = script_path.read_text(encoding="utf-8")

    assert "uv run --with-requirements" in source
    assert "python -m pytest" in source
    assert "acceptance_test.ps1" in source
    assert "RebuildContainer" in source
    assert "docker compose -f $composeFile up -d --build" in source
    assert "Wait-ServiceReady" in source
    assert '"$BaseUrl/health"' in source
    assert "service_not_ready_after_rebuild" in source
    assert "[switch]$AssertPrivateNotExposed" in source
    assert "acceptanceArgs.AssertPrivateNotExposed" in source
    assert "reason = $acceptance.reason" in source
    assert "endpoint_errors = $acceptance.endpoint_errors" in source
    assert 'if ($status -ne "passed")' in source
    assert "exit 1" in source
    assert "docker compose config" not in source
    assert "WATCH_DEVICE_TOKENS" not in source
    assert "HERMES_API_KEY" not in source


def test_smoke_test_redacts_transcript_text_by_default():
    script_path = Path(__file__).resolve().parents[1] / "smoke_test.ps1"
    source = script_path.read_text(encoding="utf-8")

    assert "[switch]$IncludeText" in source
    assert "asr_text_present" in source
    assert "reply_text_present" in source
    assert "asr_text_chars" in source
    assert "reply_text_chars" in source
    assert "if ($IncludeText)" in source
    assert "Add-Member -NotePropertyName asr_text" in source
    assert "Add-Member -NotePropertyName reply_text" in source
    assert "asr_text = $voice.asr_text" not in source
    assert "reply_text = $voice.reply_text" not in source


def test_smoke_test_requires_semantic_success_by_default():
    script_path = Path(__file__).resolve().parents[1] / "smoke_test.ps1"
    source = script_path.read_text(encoding="utf-8")

    assert "[switch]$AllowErrorResponse" in source
    assert "Assert-WatchHealthReady" in source
    assert 'Payload.status -ne "ok"' in source
    assert 'Payload.hermes_status -ne "online"' in source
    assert "Assert-VoiceSucceeded" in source
    assert 'Payload.status -ne "done"' in source
    assert 'Payload.action -eq "error"' in source
    assert "Assert-CancelSucceeded" in source
    assert 'Payload.status -ne "canceled"' in source
    assert 'Payload.action -ne "no_action"' in source


def test_smoke_test_cleans_generated_dummy_audio_only():
    script_path = Path(__file__).resolve().parents[1] / "smoke_test.ps1"
    source = script_path.read_text(encoding="utf-8")

    assert "$createdTempAudio = $null" in source
    assert "$createdTempAudio = $tmpAudio" in source
    assert "finally" in source
    assert "Remove-Item -LiteralPath $createdTempAudio" in source
    assert "Remove-Item -LiteralPath $AudioPath" not in source
    assert "watch-smoke-test-" in source


def test_public_domain_gate_checks_private_paths_without_dumping_private_payloads():
    root = Path(__file__).resolve().parents[1]
    runtime_source = (root / "runtime_status.ps1").read_text(encoding="utf-8")
    acceptance_source = (root / "acceptance_test.ps1").read_text(encoding="utf-8")
    private_function = runtime_source.split("function Invoke-PrivateExposureCheck", 1)[1].split(
        "function Get-ContainerStatus", 1
    )[0]

    assert "[switch]$AssertPrivateNotExposed" in runtime_source
    assert "Invoke-PrivateExposureCheck" in runtime_source
    assert "AllowedStatusCodes = @(403, 404, 410)" in runtime_source
    assert '"$privateBaseUrl/health"' in runtime_source
    assert '"$privateBaseUrl/v1/models"' in runtime_source
    assert '"$privateBaseUrl/v1/responses"' in runtime_source
    assert "status_code" in runtime_source
    assert "allowed_status_codes" in runtime_source
    assert "exposed" in runtime_source
    assert "[switch]$AssertPrivateNotExposed" in acceptance_source
    assert "statusArgs.AssertPrivateNotExposed" in acceptance_source
    assert "private_path_unexpected_status" in acceptance_source
    assert "private_exposure" in acceptance_source
    assert "Authorization" not in private_function
    assert "WATCH_DEVICE_TOKENS" not in private_function
    assert "payload" not in private_function
