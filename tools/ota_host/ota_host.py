#!/usr/bin/env python3
"""本地 HTTPS OTA 发布与故障注入命令行工具。"""

from __future__ import annotations

import argparse
import hashlib
import http.server
import json
import shutil
import ssl
import sys
from pathlib import Path
from typing import Literal
from urllib.parse import urlparse


FaultMode = Literal["none", "bad-sha", "truncated", "disconnect"]
MANIFEST_NAME = "manifest.json"
CHUNK_SIZE = 64 * 1024


def sha256_file(path: Path) -> str:
    """流式计算文件 SHA-256，避免将固件镜像整体放入主机内存。"""
    digest = hashlib.sha256()
    with path.open("rb") as firmware:
        while chunk := firmware.read(CHUNK_SIZE):
            digest.update(chunk)
    return digest.hexdigest()


def normalize_base_url(value: str) -> str:
    """验证发布地址为无查询参数的 HTTPS 基址。"""
    parsed = urlparse(value)
    if parsed.scheme != "https" or not parsed.netloc or parsed.query or parsed.fragment:
        raise ValueError("--base-url must be an HTTPS URL without query or fragment")
    return value.rstrip("/")


def build_manifest(version: str, image_name: str, image_size: int,
                   image_sha256: str, base_url: str) -> dict[str, object]:
    """构造设备端独立 OTA 协议使用的最小 manifest。"""
    if not version or version.strip() != version:
        raise ValueError("--version must be non-empty and contain no surrounding whitespace")
    if Path(image_name).name != image_name:
        raise ValueError("--image-name must not contain a directory")
    if image_size <= 0:
        raise ValueError("firmware image must not be empty")
    if len(image_sha256) != 64 or any(c not in "0123456789abcdef" for c in image_sha256):
        raise ValueError("image SHA-256 must be lowercase hexadecimal")

    return {
        "version": version,
        "url": f"{normalize_base_url(base_url)}/{image_name}",
        "size": image_size,
        "sha256": image_sha256,
    }


def prepare_release(bin_path: Path, version: str, base_url: str,
                    output_dir: Path, image_name: str | None = None) -> dict[str, object]:
    """复制镜像并生成 manifest，返回写入磁盘的 manifest 内容。"""
    if not bin_path.is_file():
        raise ValueError(f"firmware image not found: {bin_path}")

    resolved_name = image_name or bin_path.name
    if Path(resolved_name).name != resolved_name:
        raise ValueError("--image-name must not contain a directory")

    output_dir.mkdir(parents=True, exist_ok=True)
    destination = output_dir / resolved_name
    if bin_path.resolve() != destination.resolve():
        shutil.copyfile(bin_path, destination)

    manifest = build_manifest(
        version=version,
        image_name=resolved_name,
        image_size=destination.stat().st_size,
        image_sha256=sha256_file(destination),
        base_url=base_url,
    )
    (output_dir / MANIFEST_NAME).write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest


def faulted_manifest_bytes(release_dir: Path, fault: FaultMode) -> bytes:
    """返回正常或 SHA 故障版本的 manifest 字节。"""
    content = (release_dir / MANIFEST_NAME).read_text(encoding="utf-8")
    if fault != "bad-sha":
        return content.encode("utf-8")

    manifest = json.loads(content)
    expected = manifest["sha256"]
    manifest["sha256"] = "0" * 64 if expected != "0" * 64 else "1" * 64
    return (json.dumps(manifest, ensure_ascii=False) + "\n").encode("utf-8")


def image_response(path: Path, fault: FaultMode) -> tuple[bytes, int, bool]:
    """返回镜像响应体、Content-Length 和是否在发送后主动断开连接。"""
    body = path.read_bytes()
    if fault == "truncated":
        partial = body[: max(1, len(body) // 2)]
        return partial, len(partial), False
    if fault == "disconnect":
        partial = body[: max(1, len(body) // 2)]
        return partial, len(body), True
    return body, len(body), False


class OtaRequestHandler(http.server.SimpleHTTPRequestHandler):
    """静态发布 OTA 工件，并在指定模式下制造可重复下载故障。"""

    protocol_version = "HTTP/1.1"

    def __init__(self, *args: object, directory: str | None = None, **kwargs: object) -> None:
        super().__init__(*args, directory=directory, **kwargs)

    @property
    def fault(self) -> FaultMode:
        return self.server.fault  # type: ignore[attr-defined]

    def do_GET(self) -> None:
        request_path = urlparse(self.path).path
        if request_path == f"/{MANIFEST_NAME}":
            payload = faulted_manifest_bytes(Path(self.directory), self.fault)
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return

        local_path = Path(self.translate_path(request_path))
        if self.fault in ("truncated", "disconnect") and local_path.is_file() and local_path.suffix == ".bin":
            payload, content_length, close_after_write = image_response(local_path, self.fault)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(content_length))
            if close_after_write:
                self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(payload)
            if close_after_write:
                self.close_connection = True
            return

        super().do_GET()

    def log_message(self, format_text: str, *args: object) -> None:
        sys.stderr.write("[ota-host] %s - %s\n" % (self.address_string(), format_text % args))


def serve_release(release_dir: Path, bind: str, port: int,
                  certificate: Path, private_key: Path, fault: FaultMode) -> None:
    """以 TLS 包装静态服务器，直到用户发送 Ctrl+C。"""
    if not (release_dir / MANIFEST_NAME).is_file():
        raise ValueError(f"manifest not found in release directory: {release_dir}")
    if not certificate.is_file() or not private_key.is_file():
        raise ValueError("--cert and --key must name existing TLS files")

    handler = lambda *args, **kwargs: OtaRequestHandler(
        *args, directory=str(release_dir), **kwargs)
    server = http.server.ThreadingHTTPServer((bind, port), handler)
    server.fault = fault  # type: ignore[attr-defined]
    tls_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls_context.load_cert_chain(certfile=certificate, keyfile=private_key)
    server.socket = tls_context.wrap_socket(server.socket, server_side=True)

    print(f"Serving https://{bind}:{port}/{MANIFEST_NAME} from {release_dir}")
    print(f"Fault mode: {fault}; press Ctrl+C to stop")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping OTA host")
    finally:
        server.server_close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    prepare = commands.add_parser("prepare", help="copy firmware and create manifest.json")
    prepare.add_argument("--bin", type=Path, required=True, help="input ESP-IDF app .bin")
    prepare.add_argument("--version", required=True, help="release version")
    prepare.add_argument("--base-url", required=True, help="HTTPS release base URL")
    prepare.add_argument("--output-dir", type=Path, required=True, help="release directory")
    prepare.add_argument("--image-name", help="published image filename; defaults to input name")

    serve = commands.add_parser("serve", help="serve an existing release over HTTPS")
    serve.add_argument("--directory", type=Path, required=True, help="prepared release directory")
    serve.add_argument("--bind", default="0.0.0.0", help="listen address")
    serve.add_argument("--port", type=int, default=8443, help="TLS listen port")
    serve.add_argument("--cert", type=Path, required=True, help="PEM certificate chain")
    serve.add_argument("--key", type=Path, required=True, help="PEM private key")
    serve.add_argument("--fault", choices=("none", "bad-sha", "truncated", "disconnect"),
                       default="none", help="development fault injection mode")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "prepare":
            manifest = prepare_release(args.bin, args.version, args.base_url,
                                       args.output_dir, args.image_name)
            print(json.dumps(manifest, ensure_ascii=False, indent=2))
            return 0
        serve_release(args.directory, args.bind, args.port, args.cert, args.key, args.fault)
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"ota-host: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
