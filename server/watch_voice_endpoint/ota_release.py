"""Remote OTA release storage used by the watch endpoint.

The module deliberately keeps release publication separate from the ESP32 OTA
transport.  The server owns an immutable artifact and one small manifest per
channel; the device still performs the existing HTTPS full-image OTA flow.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
from pathlib import Path
from typing import BinaryIO


CHUNK_SIZE = 64 * 1024
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
CHANNEL_PATTERN = re.compile(r"^[a-z0-9][a-z0-9-]{0,31}$")


class OtaReleaseError(ValueError):
    """发布参数或当前发布状态无效。"""


class OtaReleaseStore:
    """将 OTA 固件和 manifest 原子发布到持久化目录。"""

    def __init__(self, root: Path, public_base_url: str, max_image_bytes: int) -> None:
        self.root = root
        self.public_base_url = public_base_url.rstrip("/")
        self.max_image_bytes = max_image_bytes

    @staticmethod
    def validate_version(version: str) -> str:
        if not VERSION_PATTERN.fullmatch(version):
            raise OtaReleaseError("version must use MAJOR.MINOR.PATCH")
        return version

    @staticmethod
    def validate_channel(channel: str) -> str:
        if not CHANNEL_PATTERN.fullmatch(channel):
            raise OtaReleaseError("channel contains invalid characters")
        return channel

    def _channel_root(self, channel: str) -> Path:
        return self.root / "releases" / self.validate_channel(channel)

    def manifest_path(self, channel: str) -> Path:
        return self._channel_root(channel) / "manifest.json"

    def artifact_path(self, channel: str, version: str) -> Path:
        return self._channel_root(channel) / self.validate_version(version) / "firmware.bin"

    def current_manifest(self, channel: str) -> dict[str, object] | None:
        path = self.manifest_path(channel)
        if not path.is_file():
            return None
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise OtaReleaseError("published manifest is unreadable") from exc
        if not isinstance(payload, dict):
            raise OtaReleaseError("published manifest is invalid")
        return payload

    def publish(
        self,
        stream: BinaryIO,
        version: str,
        channel: str,
        image_name: str = "firmware.bin",
    ) -> dict[str, object]:
        """流式写入固件，校验完成后再原子切换当前 manifest。"""
        version = self.validate_version(version)
        channel = self.validate_channel(channel)
        if not self.public_base_url:
            raise OtaReleaseError("public OTA base URL is not configured")

        channel_root = self._channel_root(channel)
        release_root = channel_root / version
        destination = release_root / image_name
        if destination.exists() or self.manifest_path(channel).exists() and (
            self.current_manifest(channel) or {}
        ).get("version") == version:
            raise OtaReleaseError("version already published")

        temp_root = channel_root / f".upload-{version}"
        temp_root.mkdir(parents=True, exist_ok=False)
        temp_path = temp_root / image_name
        digest = hashlib.sha256()
        size = 0
        try:
            with temp_path.open("wb") as output:
                while True:
                    chunk = stream.read(CHUNK_SIZE)
                    if not chunk:
                        break
                    size += len(chunk)
                    if size > self.max_image_bytes:
                        raise OtaReleaseError("firmware image exceeds configured limit")
                    digest.update(chunk)
                    output.write(chunk)
            if size == 0:
                raise OtaReleaseError("firmware image must not be empty")

            release_root.mkdir(parents=True, exist_ok=False)
            temp_path.replace(destination)
            temp_root.rmdir()
            manifest = {
                "version": version,
                "url": f"{self.public_base_url}/v1/watch/ota/artifacts/"
                f"{channel}/{version}/{image_name}",
                "size": size,
                "sha256": digest.hexdigest(),
                "channel": channel,
            }
            manifest_tmp = channel_root / ".manifest.tmp"
            manifest_tmp.write_text(
                json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8",
            )
            manifest_tmp.replace(self.manifest_path(channel))
            return manifest
        except Exception:
            if temp_path.exists():
                temp_path.unlink()
            if temp_root.exists():
                temp_root.rmdir()
            raise


def default_store() -> OtaReleaseStore:
    """读取服务进程配置，供 API 路由和测试覆盖使用。"""
    return OtaReleaseStore(
        root=Path(os.getenv("WATCH_OTA_RELEASE_DIR", "/data/ota")),
        public_base_url=os.getenv("WATCH_OTA_PUBLIC_BASE_URL", "").strip(),
        max_image_bytes=int(os.getenv("WATCH_OTA_MAX_IMAGE_BYTES", str(12 * 1024 * 1024))),
    )
