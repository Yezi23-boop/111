#!/usr/bin/env python3
"""OneNET Fuse OTA 鉴权与 SOTA report/check 预检工具。

默认从仓库内的 ``tools/ota_host/onenet.txt`` 读取产品级 AccessKey；也可
通过 ``--access-key-file`` 指定其他本地文件。Authorization 和密钥不会写入输出。
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import ssl
import sys
import time
from dataclasses import dataclass
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen


AUTH_VERSION = "2022-05-01"
DEFAULT_API_BASE = "https://iot-api.heclouds.com/fuse-ota"
DEFAULT_MODULE_VERSION = "1.0.0"
DEFAULT_ACCESS_KEY_FILE = Path(__file__).with_name("onenet.txt")


@dataclass(frozen=True)
class CheckResult:
    """OneNET check 接口中设备端需要消费的最小字段。"""

    code: int
    message: str
    target: str | None
    task_id: int | None
    size: int | None
    md5: str | None
    status: int | None
    package_type: int | None


def _require_https_base(value: str) -> str:
    if not value.startswith("https://") or "?" in value or "#" in value:
        raise ValueError("ONENET_API_BASE must be an HTTPS base URL without query or fragment")
    return value.rstrip("/")


def load_access_key_file(path: str) -> str:
    """读取本机 AccessKey 文件；只接受 `AccessKey:<value>` 一行格式。"""
    content = Path(path).read_text(encoding="utf-8-sig").strip()
    if ":" not in content and "=" not in content and "：" not in content:
        raise ValueError("access key file must contain AccessKey:<value>")
    _, value = content.split(":", 1) if ":" in content else (
        content.split("=", 1) if "=" in content else content.split("：", 1)
    )
    access_key = value.strip()
    if not access_key:
        raise ValueError("access key file contains an empty key")
    return access_key


def build_authorization(
    access_key: str,
    resource: str,
    *,
    now: int | None = None,
    ttl_seconds: int = 3600,
    method: str = "sha256",
) -> str:
    """按 OneNET 2022-05-01 规则生成短期 Authorization。"""
    if not access_key:
        raise ValueError("ONENET_ACCESS_KEY is empty")
    if not resource:
        raise ValueError("authorization resource is empty")
    if ttl_seconds <= 0:
        raise ValueError("token TTL must be positive")
    normalized_method = method.lower()
    digest = getattr(hashlib, normalized_method, None)
    if digest is None or normalized_method not in {"md5", "sha1", "sha256"}:
        raise ValueError("authorization method must be md5, sha1, or sha256")
    try:
        secret = base64.b64decode(access_key, validate=True)
    except (ValueError, base64.binascii.Error) as error:
        raise ValueError("ONENET_ACCESS_KEY must be base64 encoded") from error

    expire_at = int(time.time() if now is None else now) + ttl_seconds
    signature_source = f"{expire_at}\n{normalized_method}\n{resource}\n{AUTH_VERSION}"
    signature = base64.b64encode(
        hmac.new(secret, signature_source.encode("utf-8"), digest).digest()
    ).decode("ascii")
    return (
        f"version={AUTH_VERSION}"
        f"&res={quote(resource, safe='')}"
        f"&et={expire_at}"
        f"&method={normalized_method}"
        f"&sign={quote(signature, safe='')}"
    )


def build_version_payload(app_version: str, module_version: str = DEFAULT_MODULE_VERSION) -> dict[str, str]:
    """构造 Fuse OTA 版本上报体；SOTA 选择 s_version。"""
    if not app_version or app_version.strip() != app_version:
        raise ValueError("app version must be non-empty and trimmed")
    if not module_version or module_version.strip() != module_version:
        raise ValueError("module version must be non-empty and trimmed")
    return {"s_version": app_version, "f_version": module_version}


def build_endpoint(api_base: str, product_id: str, device_name: str, suffix: str) -> str:
    """构造单个 Fuse OTA API 地址，路径段全部进行 URL 编码。"""
    base = _require_https_base(api_base)
    product = quote(product_id, safe="")
    device = quote(device_name, safe="")
    return f"{base}/{product}/{device}/{suffix.lstrip('/')}"


def parse_check_response(payload: object) -> CheckResult:
    """解析 check 响应，拒绝缺字段或错误类型，避免误进入下载。"""
    if not isinstance(payload, dict):
        raise ValueError("OneNET check response must be a JSON object")
    code = payload.get("code")
    message = payload.get("msg", "")
    if not isinstance(code, int) or not isinstance(message, str):
        raise ValueError("OneNET check response has invalid code/msg")
    data = payload.get("data")
    if code != 0:
        return CheckResult(code, message, None, None, None, None, None, None)
    if not isinstance(data, dict):
        raise ValueError("successful OneNET check response has no data object")
    target = data.get("target")
    task_id = data.get("tid")
    size = data.get("size")
    md5 = data.get("md5")
    status = data.get("status")
    package_type = data.get("type")
    if not isinstance(target, str) or not isinstance(task_id, int):
        raise ValueError("OneNET check response is missing target/tid")
    if not isinstance(size, int) or size <= 0:
        raise ValueError("OneNET check response has invalid size")
    if not isinstance(md5, str) or len(md5) != 32 or any(c not in "0123456789abcdefABCDEF" for c in md5):
        raise ValueError("OneNET check response has invalid md5")
    if not isinstance(status, int) or not isinstance(package_type, int):
        raise ValueError("OneNET check response is missing status/type")
    return CheckResult(code, message, target, task_id, size, md5.lower(), status, package_type)


def _request_json(url: str, authorization: str, *, method: str, body: bytes | None = None) -> object:
    request = Request(
        url,
        data=body,
        method=method,
        headers={
            "Authorization": authorization,
            "Content-Type": "application/json",
            "Accept": "application/json",
        },
    )
    try:
        with urlopen(request, context=ssl.create_default_context(), timeout=30) as response:
            return json.loads(response.read().decode("utf-8"))
    except HTTPError as error:
        raise RuntimeError(f"OneNET HTTP error: {error.code}") from error
    except URLError as error:
        raise RuntimeError(f"OneNET network error: {error.reason}") from error


def run_probe(
    *,
    product_id: str,
    device_name: str,
    access_key: str,
    current_version: str,
    resource: str | None = None,
    api_base: str = DEFAULT_API_BASE,
    module_version: str = DEFAULT_MODULE_VERSION,
    ttl_seconds: int = 3600,
    auth_method: str = "sha256",
) -> tuple[object, CheckResult]:
    """先上报版本，再检查 SOTA 任务；返回脱敏前的内存对象，不打印密钥。"""
    auth_resource = resource or f"products/{product_id}"
    authorization = build_authorization(
        access_key,
        auth_resource,
        ttl_seconds=ttl_seconds,
        method=auth_method,
    )
    version_url = build_endpoint(api_base, product_id, device_name, "version")
    version_response = _request_json(
        version_url,
        authorization,
        method="POST",
        body=json.dumps(build_version_payload(current_version, module_version)).encode("utf-8"),
    )
    check_query = urlencode({"type": "2", "version": current_version})
    check_url = f"{build_endpoint(api_base, product_id, device_name, 'check')}?{check_query}"
    check_response = _request_json(check_url, authorization, method="GET")
    return version_response, parse_check_response(check_response)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product-id", default=os.getenv("ONENET_PRODUCT_ID", "w23kT21Z3x"))
    parser.add_argument("--device-name", default=os.getenv("ONENET_DEVICE_NAME", "watch-001"))
    parser.add_argument("--current-version", required=True)
    parser.add_argument("--api-base", default=os.getenv("ONENET_API_BASE", DEFAULT_API_BASE))
    parser.add_argument("--auth-res", default=os.getenv("ONENET_AUTH_RES", ""))
    parser.add_argument(
        "--access-key-file",
        default=str(DEFAULT_ACCESS_KEY_FILE),
        help="AccessKey file; value is never printed",
    )
    parser.add_argument("--module-version", default=os.getenv("ONENET_MODULE_VERSION", DEFAULT_MODULE_VERSION))
    parser.add_argument("--ttl-seconds", type=int, default=3600)
    parser.add_argument("--auth-method", choices=("sha256", "sha1", "md5"),
                        default=os.getenv("ONENET_AUTH_METHOD", "sha256"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    access_key = os.getenv("ONENET_ACCESS_KEY", "")
    try:
        access_key = load_access_key_file(args.access_key_file)
    except (OSError, ValueError) as error:
        print(f"onenet-probe: {error}", file=sys.stderr)
        return 2
    if not access_key:
        print("onenet-probe: set ONENET_ACCESS_KEY in the local environment", file=sys.stderr)
        return 2
    try:
        version_response, result = run_probe(
            product_id=args.product_id,
            device_name=args.device_name,
            access_key=access_key,
            current_version=args.current_version,
            resource=args.auth_res or None,
            api_base=args.api_base,
            module_version=args.module_version,
            ttl_seconds=args.ttl_seconds,
            auth_method=args.auth_method,
        )
    except (OSError, ValueError, RuntimeError) as error:
        print(f"onenet-probe: {error}", file=sys.stderr)
        return 2

    if not isinstance(version_response, dict):
        print("onenet-probe: version response is not a JSON object", file=sys.stderr)
        return 2
    print(json.dumps({
        "version_code": version_response.get("code"),
        "version_msg": version_response.get("msg"),
        "check_code": result.code,
        "check_msg": result.message,
        "target": result.target,
        "tid": result.task_id,
        "size": result.size,
        "md5": result.md5,
        "status": result.status,
        "type": result.package_type,
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
