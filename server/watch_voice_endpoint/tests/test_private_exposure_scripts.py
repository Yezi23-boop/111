from __future__ import annotations

import json
import os
import shutil
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

import pytest


ROOT = Path(__file__).resolve().parents[1]
PRIVATE_BODY_SENTINEL = "SECRET_PRIVATE_BODY_SHOULD_NOT_LEAK"


def _powershell_executable() -> str:
    executable = shutil.which("pwsh")
    if executable is None:
        pytest.skip("PowerShell 7 executable is not available")
    return executable


def _run_script(
    script_name: str,
    *args: str,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    run_env = os.environ.copy()
    if env is not None:
        run_env.update(env)
    return subprocess.run(
        [
            _powershell_executable(),
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(ROOT / script_name),
            *args,
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
        env=run_env,
        timeout=90,
    )


def _json_stdout(result: subprocess.CompletedProcess[str]) -> dict[str, Any]:
    output = result.stdout.strip()
    assert output, result.stderr
    return json.loads(output)


def _write_fake_env_files(tmp_path: Path) -> tuple[Path, Path]:
    watch_env = tmp_path / "watch.env"
    hermes_env = tmp_path / "hermes.env"
    watch_env.write_text(
        "\n".join(
            [
                "WATCH_DEVICE_TOKENS=watch-001=test-token",
                "WATCH_ASR_PROVIDER=mock",
                "WATCH_REQUEST_TIMEOUT_SECONDS=115",
            ]
        ),
        encoding="utf-8",
    )
    hermes_env.write_text("API_SERVER_KEY=fake-hermes-api-key\n", encoding="utf-8")
    return watch_env, hermes_env


class _StubServer:
    def __init__(self, private_statuses: dict[str, int]) -> None:
        self._private_statuses = private_statuses
        self._server: ThreadingHTTPServer | None = None
        self._thread: threading.Thread | None = None

    @property
    def base_url(self) -> str:
        assert self._server is not None
        host, port = self._server.server_address
        return f"http://{host}:{port}"

    def __enter__(self) -> "_StubServer":
        private_statuses = self._private_statuses

        class Handler(BaseHTTPRequestHandler):
            def _send_json(self, status: int, payload: dict[str, Any]) -> None:
                body = json.dumps(payload).encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def _send_private_status(self, status: int) -> None:
                body = PRIVATE_BODY_SENTINEL.encode("utf-8")
                self.send_response(status)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self) -> None:  # noqa: N802
                path = urlparse(self.path).path
                if path == "/v1/watch/health":
                    if self.headers.get("Authorization") == "Bearer invalid-smoke-token":
                        self._send_json(403, {"detail": "invalid_device_token"})
                        return
                    self._send_json(
                        200,
                        {
                            "status": "ok",
                            "hermes_status": "online",
                            "device_id": "watch-001",
                        },
                    )
                    return
                if path in private_statuses:
                    self._send_private_status(private_statuses[path])
                    return
                self._send_json(404, {"detail": "not found"})

            def do_POST(self) -> None:  # noqa: N802
                content_length = int(self.headers.get("Content-Length", "0") or 0)
                if content_length:
                    self.rfile.read(content_length)
                path = urlparse(self.path).path
                if path == "/internal/watch/inbox" and path in private_statuses:
                    self._send_private_status(private_statuses[path])
                    return
                if path == "/v1/watch/voice-command":
                    self._send_json(
                        200,
                        {
                            "request_id": "stub-request",
                            "status": "done",
                            "action": "memory_saved",
                            "asr_text": "stub asr",
                            "reply_text": "stub reply",
                            "clarification_id": None,
                            "error_code": None,
                        },
                    )
                    return
                if path == "/v1/watch/text-command":
                    self._send_json(
                        200,
                        {
                            "request_id": "stub-text",
                            "status": "done",
                            "action": "memory_saved",
                            "asr_text": "stub text",
                            "reply_text": "stub text reply",
                            "clarification_id": None,
                            "error_code": None,
                        },
                    )
                    return
                if path.startswith("/v1/watch/request/") and path.endswith("/cancel"):
                    self._send_json(
                        200,
                        {
                            "request_id": "stub-cancel",
                            "status": "canceled",
                            "action": "no_action",
                            "asr_text": "",
                            "reply_text": "已取消等待",
                            "clarification_id": None,
                            "error_code": None,
                        },
                    )
                    return
                self._send_json(404, {"detail": "not found"})

            def log_message(self, format: str, *args: object) -> None:
                return

        self._server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()
        return self

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
        assert self._server is not None
        self._server.shutdown()
        self._server.server_close()
        if self._thread is not None:
            self._thread.join(timeout=5)


def test_runtime_status_flags_public_private_paths_with_unexpected_status(tmp_path: Path) -> None:
    watch_env, hermes_env = _write_fake_env_files(tmp_path)
    with _StubServer(
        {
            "/health": 200,
            "/v1/models": 404,
            "/v1/responses": 404,
            "/internal/watch/inbox": 404,
        }
    ) as server:
        result = _run_script(
            "runtime_status.ps1",
            "-BaseUrl",
            server.base_url,
            "-WatchEnvFile",
            str(watch_env),
            "-HermesEnvFile",
            str(hermes_env),
            "-SkipDocker",
            "-SkipHermesApi",
            "-SkipServiceHealth",
            "-AssertPrivateNotExposed",
        )

    assert result.returncode == 0, result.stderr
    assert PRIVATE_BODY_SENTINEL not in result.stdout
    assert PRIVATE_BODY_SENTINEL not in result.stderr
    payload = _json_stdout(result)
    private_exposure = payload["endpoints"]["private_exposure"]
    assert private_exposure["ok"] is False
    checks = {item["path"]: item for item in private_exposure["checks"]}
    assert checks["/health"]["exposed"] is True
    assert checks["/health"]["status_code"] == 200
    assert checks["/v1/models"]["ok"] is True
    assert checks["/v1/responses"]["ok"] is True
    assert checks["/internal/watch/inbox"]["ok"] is True


def test_runtime_status_rejects_reachable_internal_post_route(tmp_path: Path) -> None:
    watch_env, hermes_env = _write_fake_env_files(tmp_path)
    with _StubServer(
        {
            "/health": 404,
            "/v1/models": 404,
            "/v1/responses": 404,
            "/internal/watch/inbox": 405,
        }
    ) as server:
        result = _run_script(
            "runtime_status.ps1",
            "-BaseUrl",
            server.base_url,
            "-WatchEnvFile",
            str(watch_env),
            "-HermesEnvFile",
            str(hermes_env),
            "-SkipDocker",
            "-SkipHermesApi",
            "-SkipServiceHealth",
            "-AssertPrivateNotExposed",
        )

    payload = _json_stdout(result)
    checks = {
        item["path"]: item
        for item in payload["endpoints"]["private_exposure"]["checks"]
    }
    assert checks["/internal/watch/inbox"]["status_code"] == 405
    assert checks["/internal/watch/inbox"]["ok"] is False


def test_acceptance_fails_when_public_private_path_has_unexpected_status(tmp_path: Path) -> None:
    watch_env, hermes_env = _write_fake_env_files(tmp_path)
    with _StubServer(
        {
            "/health": 200,
            "/v1/models": 404,
            "/v1/responses": 404,
            "/internal/watch/inbox": 404,
        }
    ) as server:
        result = _run_script(
            "acceptance_test.ps1",
            "-BaseUrl",
            server.base_url,
            "-EnvFile",
            str(watch_env),
            "-HermesEnvFile",
            str(hermes_env),
            "-SkipDocker",
            "-SkipHermesApi",
            "-SkipServiceHealth",
            "-SkipRealAsr",
            "-AssertPrivateNotExposed",
        )

    assert result.returncode == 1
    assert PRIVATE_BODY_SENTINEL not in result.stdout
    assert PRIVATE_BODY_SENTINEL not in result.stderr
    payload = _json_stdout(result)
    assert payload["status"] == "failed"
    assert payload["reason"] == "private_path_unexpected_status"
    assert payload["endpoint_errors"]["private_exposure"]["ok"] is False


def test_acceptance_passes_when_public_private_paths_are_rejected(tmp_path: Path) -> None:
    watch_env, hermes_env = _write_fake_env_files(tmp_path)
    with _StubServer(
        {
            "/health": 403,
            "/v1/models": 404,
            "/v1/responses": 410,
            "/internal/watch/inbox": 404,
        }
    ) as server:
        result = _run_script(
            "acceptance_test.ps1",
            "-BaseUrl",
            server.base_url,
            "-EnvFile",
            str(watch_env),
            "-HermesEnvFile",
            str(hermes_env),
            "-SkipDocker",
            "-SkipHermesApi",
            "-SkipServiceHealth",
            "-SkipRealAsr",
            "-AssertPrivateNotExposed",
        )

    assert result.returncode == 0, result.stderr
    assert PRIVATE_BODY_SENTINEL not in result.stdout
    assert PRIVATE_BODY_SENTINEL not in result.stderr
    payload = _json_stdout(result)
    assert payload["status"] == "passed"
    private_exposure = payload["runtime_before"]["private_exposure"]
    assert private_exposure["ok"] is True
    assert {item["status_code"] for item in private_exposure["checks"]} == {403, 404, 410}


def test_smoke_test_removes_generated_dummy_audio(tmp_path: Path) -> None:
    watch_env, _ = _write_fake_env_files(tmp_path)
    temp_dir = tmp_path / "temp"
    temp_dir.mkdir()

    with _StubServer({}) as server:
        result = _run_script(
            "smoke_test.ps1",
            "-BaseUrl",
            server.base_url,
            "-EnvFile",
            str(watch_env),
            "-SkipServiceHealth",
            env={"TEMP": str(temp_dir), "TMP": str(temp_dir)},
        )

    assert result.returncode == 0, result.stderr
    payload = _json_stdout(result)
    assert payload["voice_status"] == "done"
    assert list(temp_dir.glob("watch-smoke-test-*.opus")) == []


def test_smoke_test_keeps_explicit_audio_path(tmp_path: Path) -> None:
    watch_env, _ = _write_fake_env_files(tmp_path)
    temp_dir = tmp_path / "temp"
    temp_dir.mkdir()
    audio_path = tmp_path / "provided.opus"
    audio_path.write_bytes(b"OggS\x00\x01\x02\x03")

    with _StubServer({}) as server:
        result = _run_script(
            "smoke_test.ps1",
            "-BaseUrl",
            server.base_url,
            "-EnvFile",
            str(watch_env),
            "-AudioPath",
            str(audio_path),
            "-SkipServiceHealth",
            env={"TEMP": str(temp_dir), "TMP": str(temp_dir)},
        )

    assert result.returncode == 0, result.stderr
    payload = _json_stdout(result)
    assert payload["voice_status"] == "done"
    assert audio_path.exists()
    assert list(temp_dir.glob("watch-smoke-test-*.opus")) == []
