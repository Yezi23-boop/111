#!/usr/bin/env python3
"""本地 AP 配网页面模拟端。

这个脚本只服务于浏览器交互预览：它托管 `components/ap_portal_adapter/web`
里的静态页面，并模拟官方 SoftAP provisioning 的最小二进制接口。
固件侧仍使用真实 ESP-IDF provisioning，不会引用本脚本。
"""

from __future__ import annotations

import argparse
import functools
import json
import mimetypes
import time
from dataclasses import dataclass
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler
from http.server import ThreadingHTTPServer
from pathlib import Path
from typing import Iterable

WIRE_VARINT = 0
WIRE_LEN = 2

WIFI_NETWORKS = [
    {
        "ssid": "Home-WiFi",
        "channel": 6,
        "rssi": -42,
        "bssid": bytes.fromhex("001122334455"),
        "auth": 3,
    },
    {
        "ssid": "Lab-2.4G",
        "channel": 11,
        "rssi": -58,
        "bssid": bytes.fromhex("66778899aabb"),
        "auth": 4,
    },
    {
        "ssid": "Watch-Test",
        "channel": 1,
        "rssi": -67,
        "bssid": bytes.fromhex("102030405060"),
        "auth": 0,
    },
]


@dataclass
class MockState:
    """保存一次浏览器会话中的最小配网状态。"""

    selected_ssid: str = ""
    selected_password: str = ""
    apply_started_at: float | None = None
    status_poll_count: int = 0


def encode_varint(value: int) -> bytes:
    """编码 protobuf varint；仅用于模拟端固定字段。"""

    if value < 0:
        raise ValueError("varint 只支持非负整数")

    output = bytearray()
    remaining = value
    while True:
        byte = remaining & 0x7F
        remaining >>= 7
        if remaining:
            output.append(byte | 0x80)
            continue
        output.append(byte)
        return bytes(output)


def decode_varint(buffer: bytes, offset: int) -> tuple[int, int]:
    """解码 protobuf varint，用来识别前端发来的请求类型。"""

    value = 0
    shift = 0
    while offset < len(buffer):
        byte = buffer[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if (byte & 0x80) == 0:
            return value, offset
        shift += 7
        if shift > 63:
            raise ValueError("protobuf varint 过大")
    raise ValueError("protobuf varint 被截断")


def field_header(field_number: int, wire_type: int) -> bytes:
    """生成 protobuf 字段头。"""

    return encode_varint((field_number << 3) | wire_type)


def varint_field(field_number: int, value: int, *, emit_default: bool = False) -> bytes:
    """生成 varint 字段。"""

    if value == 0 and not emit_default:
        return b""
    return field_header(field_number, WIRE_VARINT) + encode_varint(value)


def bool_field(field_number: int, value: bool) -> bytes:
    """生成 bool 字段。"""

    return varint_field(field_number, 1 if value else 0)


def bytes_field(field_number: int, value: bytes, *, emit_default: bool = False) -> bytes:
    """生成 length-delimited 字段。"""

    if not value and not emit_default:
        return b""
    return field_header(field_number, WIRE_LEN) + encode_varint(len(value)) + value


def text_field(field_number: int, value: str) -> bytes:
    """生成 UTF-8 文本字段。"""

    return bytes_field(field_number, value.encode("utf-8"))


def decode_fields(buffer: bytes) -> list[tuple[int, int, bytes | int]]:
    """把 protobuf 消息拆成字段列表，够模拟端识别请求即可。"""

    fields: list[tuple[int, int, bytes | int]] = []
    offset = 0
    while offset < len(buffer):
        header, offset = decode_varint(buffer, offset)
        field_number = header >> 3
        wire_type = header & 0x07

        if wire_type == WIRE_VARINT:
            value, offset = decode_varint(buffer, offset)
            fields.append((field_number, wire_type, value))
            continue

        if wire_type == WIRE_LEN:
            length, offset = decode_varint(buffer, offset)
            end = offset + length
            if end > len(buffer):
                raise ValueError("protobuf length-delimited 字段被截断")
            fields.append((field_number, wire_type, buffer[offset:end]))
            offset = end
            continue

        raise ValueError(f"不支持的 protobuf wire type: {wire_type}")
    return fields


def first_varint(fields: Iterable[tuple[int, int, bytes | int]], field_number: int) -> int:
    """读取第一个 varint 字段；没有时返回 0。"""

    for number, wire_type, value in fields:
        if number == field_number and wire_type == WIRE_VARINT:
            return int(value)
    return 0


def first_bytes(fields: Iterable[tuple[int, int, bytes | int]], field_number: int) -> bytes:
    """读取第一个 bytes 字段；没有时返回空 bytes。"""

    for number, wire_type, value in fields:
        if number == field_number and wire_type == WIRE_LEN:
            return bytes(value)
    return b""


def has_field(fields: Iterable[tuple[int, int, bytes | int]], field_number: int, wire_type: int) -> bool:
    """判断字段是否存在，空消息字段也算存在。"""

    return any(number == field_number and kind == wire_type for number, kind, _ in fields)


def signed_int32_value(value: int) -> int:
    """把 Python 负数转成前端 decoder 期望的 32-bit varint 表示。"""

    return value & 0xFFFFFFFF


def encode_session_ok_response() -> bytes:
    """返回 security0 session 建立成功响应。"""

    session_response = varint_field(1, 0, emit_default=True)
    sec0_payload = (
        varint_field(1, 0, emit_default=True)
        + bytes_field(21, session_response, emit_default=True)
    )
    return varint_field(2, 0, emit_default=True) + bytes_field(10, sec0_payload)


def encode_scan_start_response() -> bytes:
    """返回扫描启动成功响应。"""

    return varint_field(1, 0, emit_default=True) + varint_field(2, 0, emit_default=True)


def encode_scan_status_response() -> bytes:
    """返回扫描已完成和结果数量。"""

    status_payload = bool_field(1, True) + varint_field(2, len(WIFI_NETWORKS))
    return (
        varint_field(1, 2, emit_default=True)
        + varint_field(2, 0, emit_default=True)
        + bytes_field(13, status_payload, emit_default=True)
    )


def encode_scan_result_entry(network: dict[str, object]) -> bytes:
    """编码单个 Wi-Fi 扫描结果。"""

    return b"".join(
        [
            text_field(1, str(network["ssid"])),
            varint_field(2, int(network["channel"])),
            varint_field(3, signed_int32_value(int(network["rssi"]))),
            bytes_field(4, bytes(network["bssid"])),
            varint_field(5, int(network["auth"])),
        ]
    )


def encode_scan_result_response() -> bytes:
    """返回固定 Wi-Fi 列表。"""

    result_payload = b"".join(
        bytes_field(1, encode_scan_result_entry(network)) for network in WIFI_NETWORKS
    )
    return (
        varint_field(1, 4, emit_default=True)
        + varint_field(2, 0, emit_default=True)
        + bytes_field(15, result_payload, emit_default=True)
    )


def encode_config_status_response(state: MockState) -> bytes:
    """返回 Wi-Fi 连接状态，模拟提交后先连接中再成功。"""

    if state.apply_started_at is None:
        wifi_state = 2
        connected_payload = b""
    else:
        state.status_poll_count += 1
        if state.status_poll_count < 2 and time.monotonic() - state.apply_started_at < 1.0:
            wifi_state = 1
            connected_payload = b""
        else:
            wifi_state = 0
            connected_payload = (
                text_field(1, "192.168.4.23")
                + varint_field(2, 3)
                + text_field(3, state.selected_ssid or "Home-WiFi")
                + bytes_field(4, bytes.fromhex("001122334455"))
                + varint_field(5, 6)
            )

    status_payload = (
        varint_field(1, 0, emit_default=True)
        + varint_field(2, wifi_state, emit_default=True)
        + bytes_field(11, connected_payload)
    )
    return (
        varint_field(1, 0, emit_default=True)
        + bytes_field(11, status_payload, emit_default=True)
    )


def encode_config_ack_response(payload_field: int) -> bytes:
    """返回 set/apply 配置成功响应。"""

    ok_payload = varint_field(1, 0, emit_default=True)
    return (
        varint_field(1, 0, emit_default=True)
        + bytes_field(payload_field, ok_payload, emit_default=True)
    )


def parse_set_config_request(body: bytes) -> tuple[str, str]:
    """从 SetConfig 请求中提取 SSID 和密码，仅用于模拟状态显示。"""

    fields = decode_fields(body)
    config_payload = first_bytes(fields, 12)
    config_fields = decode_fields(config_payload)
    ssid = first_bytes(config_fields, 1).decode("utf-8", errors="replace")
    password = first_bytes(config_fields, 2).decode("utf-8", errors="replace")
    return ssid, password


class ApPortalMockHandler(SimpleHTTPRequestHandler):
    """同时提供静态资源和最小 provisioning 模拟接口。"""

    server_version = "ApPortalMock/1.0"
    state: MockState

    def log_message(self, format: str, *args: object) -> None:
        """使用更短的日志格式，便于用户观察请求路径。"""

        print(f"{self.address_string()} - {format % args}")

    def end_headers(self) -> None:
        """关闭缓存，避免浏览器预览旧脚本。"""

        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_POST(self) -> None:
        """处理官方 provisioning 风格的 POST endpoint。"""

        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)

        try:
            if self.path == "/proto-ver":
                payload = json.dumps(
                    {"prov": {"ver": "mock-v1", "cap": ["no_sec", "wifi_scan"]}},
                    separators=(",", ":"),
                ).encode("utf-8")
                self.send_binary(payload, "application/json; charset=utf-8")
                return

            if self.path == "/prov-session":
                self.send_binary(encode_session_ok_response())
                return

            if self.path == "/prov-scan":
                self.send_binary(self.handle_scan_request(body))
                return

            if self.path == "/prov-config":
                self.send_binary(self.handle_config_request(body))
                return
        except Exception as error:  # noqa: BLE001 - 模拟端需要把错误直接显示给浏览器。
            self.send_error(HTTPStatus.INTERNAL_SERVER_ERROR, str(error))
            return

        self.send_error(HTTPStatus.NOT_FOUND, "mock endpoint not found")

    def handle_scan_request(self, body: bytes) -> bytes:
        """按请求字段区分 scan start/status/result。"""

        fields = decode_fields(body)
        if has_field(fields, 10, WIRE_LEN):
            return encode_scan_start_response()
        if has_field(fields, 12, WIRE_LEN):
            return encode_scan_status_response()
        if has_field(fields, 14, WIRE_LEN):
            return encode_scan_result_response()
        return encode_scan_start_response()

    def handle_config_request(self, body: bytes) -> bytes:
        """按请求字段区分 get status / set config / apply config。"""

        fields = decode_fields(body)
        command = first_varint(fields, 1)

        if has_field(fields, 10, WIRE_LEN):
            return encode_config_status_response(self.state)

        if command == 2 and has_field(fields, 12, WIRE_LEN):
            self.state.selected_ssid, self.state.selected_password = parse_set_config_request(body)
            self.state.apply_started_at = None
            self.state.status_poll_count = 0
            return encode_config_ack_response(13)

        if command == 4 and has_field(fields, 14, WIRE_LEN):
            self.state.apply_started_at = time.monotonic()
            self.state.status_poll_count = 0
            return encode_config_ack_response(15)

        return encode_config_status_response(self.state)

    def send_binary(self, payload: bytes, content_type: str = "application/octet-stream") -> None:
        """发送二进制响应。"""

        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def parse_args() -> argparse.Namespace:
    """解析命令行参数。"""

    parser = argparse.ArgumentParser(description="AP 配网页面本地模拟端")
    parser.add_argument("--host", default="127.0.0.1", help="监听地址，默认 127.0.0.1")
    parser.add_argument("--port", type=int, default=8768, help="监听端口，默认 8768")
    parser.add_argument(
        "--web-root",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "components" / "ap_portal_adapter" / "web",
        help="静态网页目录，默认指向 components/ap_portal_adapter/web",
    )
    return parser.parse_args()


def main() -> None:
    """启动本地模拟服务器。"""

    args = parse_args()
    web_root = args.web_root.resolve()
    if not web_root.exists():
        raise SystemExit(f"静态网页目录不存在: {web_root}")

    mimetypes.add_type("application/javascript; charset=utf-8", ".js")
    mimetypes.add_type("text/css; charset=utf-8", ".css")

    ApPortalMockHandler.state = MockState()
    handler = functools.partial(ApPortalMockHandler, directory=str(web_root))

    server = ThreadingHTTPServer((args.host, args.port), handler)
    url = f"http://{args.host}:{args.port}/index.html"
    print(f"AP 配网页面模拟端已启动: {url}")
    print("按 Ctrl+C 停止。")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n正在停止模拟端...")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
