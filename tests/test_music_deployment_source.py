from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPOSE = ROOT / "server" / "watch_voice_endpoint" / "compose.hk.yml"
OPENRESTY = ROOT / "server" / "deploy" / "openresty" / "ai-memory-watch.conf"
PANEL_PROXY = ROOT / "server" / "deploy" / "1panel" / "watch-endpoint" / "proxy" / "root.conf"


def _music_service_block() -> str:
    text = COMPOSE.read_text(encoding="utf-8")
    return text.split("  ai-memory-watch-music-service:\n", 1)[1].split(
        "\nnetworks:\n", 1
    )[0]


def test_music_service_is_loopback_only_and_data_is_host_persisted() -> None:
    compose = COMPOSE.read_text(encoding="utf-8")
    block = _music_service_block()
    assert "127.0.0.1:18788:8788" in block
    assert "/opt/ai-memory-watch/music-data:/data" in block
    assert "read_only: true" in block
    assert "pids_limit: 64" in block
    assert "mem_limit:" not in block
    assert "music-service-egress" in block
    assert "name: ai-memory-watch-music-egress" in compose


def test_openresty_only_adds_authenticated_music_namespace() -> None:
    text = OPENRESTY.read_text(encoding="utf-8")
    mcp = text.split("location = /v1/music/mcp", 1)[1].split(
        "\n    location ^~ /v1/music/", 1
    )[0]
    assert "limit_except POST { deny all; }" in mcp
    assert "client_max_body_size 64k;" in mcp
    assert "proxy_set_header Authorization $http_authorization;" in mcp
    assert "proxy_read_timeout 15s;" in mcp
    music = text.split("location ^~ /v1/music/", 1)[1].split(
        "\n    location /", 1
    )[0]
    assert "proxy_pass http://127.0.0.1:18788;" in music
    assert "proxy_buffering off;" in music
    assert "proxy_read_timeout 3600s;" in music
    assert "location = /health" in text
    assert "return 404;" in text
    assert "location ^~ /internal/" in text
    assert "api-enhanced" not in music


def test_1panel_watch_proxy_has_the_same_exact_mcp_gate() -> None:
    text = PANEL_PROXY.read_text(encoding="utf-8")
    mcp = text.split("location = /v1/music/mcp", 1)[1].split(
        "\nlocation ^~ /v1/music/", 1
    )[0]
    assert "limit_except POST { deny all; }" in mcp
    assert "client_max_body_size 64k;" in mcp
    assert "proxy_set_header Authorization $http_authorization;" in mcp
    assert "proxy_read_timeout 15s;" in mcp
