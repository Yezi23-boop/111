from __future__ import annotations

import importlib

import httpx
import pytest


@pytest.mark.anyio
async def test_remote_ota_admin_publishes_manifest_and_artifact(tmp_path, monkeypatch):
    monkeypatch.setenv("WATCH_DEVICE_TOKENS", "watch-001=test-token")
    monkeypatch.setenv("HERMES_API_KEY", "test-hermes-key")
    monkeypatch.setenv("WATCH_INTERNAL_API_KEY", "test-internal-key")
    monkeypatch.setenv("WATCH_OTA_ADMIN_TOKEN", "test-ota-admin")
    monkeypatch.setenv("WATCH_OTA_RELEASE_DIR", str(tmp_path))
    monkeypatch.setenv("WATCH_OTA_PUBLIC_BASE_URL", "https://watch.934000.xyz")
    monkeypatch.setenv("INBOX_DB_PATH", str(tmp_path / "inbox.db"))
    monkeypatch.setenv("CONVERSATION_DB_PATH", str(tmp_path / "conversation.db"))
    monkeypatch.setenv("SESSION_DB_PATH", str(tmp_path / "session.db"))

    import app

    module = importlib.reload(app)
    transport = httpx.ASGITransport(app=module.app)
    async with httpx.AsyncClient(transport=transport, base_url="https://test") as client:
        denied = await client.post(
            "/v1/watch/ota/admin/releases",
            files={"file": ("111.bin", b"firmware-v1", "application/octet-stream")},
            data={"version": "0.2.0", "channel": "stable"},
        )
        published = await client.post(
            "/v1/watch/ota/admin/releases",
            headers={"X-OTA-Admin-Token": "test-ota-admin"},
            files={"file": ("111.bin", b"firmware-v1", "application/octet-stream")},
            data={"version": "0.2.0", "channel": "stable"},
        )
        manifest = await client.get("/v1/watch/ota/manifest?channel=stable")
        artifact = await client.get("/v1/watch/ota/artifacts/stable/0.2.0/firmware.bin")

    assert denied.status_code == 403
    assert published.status_code == 200
    payload = published.json()
    assert payload["version"] == "0.2.0"
    assert payload["size"] == len(b"firmware-v1")
    assert payload["url"].endswith("/stable/0.2.0/firmware.bin")
    assert manifest.status_code == 200
    assert manifest.json() == payload
    assert artifact.status_code == 200
    assert artifact.content == b"firmware-v1"


def test_release_store_rejects_non_semver_and_oversized_image(tmp_path):
    from ota_release import OtaReleaseError, OtaReleaseStore

    store = OtaReleaseStore(tmp_path, "https://watch.example", max_image_bytes=4)
    with pytest.raises(OtaReleaseError, match="MAJOR.MINOR.PATCH"):
        store.publish(__import__("io").BytesIO(b"1234"), "latest", "stable")
    with pytest.raises(OtaReleaseError, match="exceeds"):
        store.publish(__import__("io").BytesIO(b"12345"), "0.1.0", "stable")
