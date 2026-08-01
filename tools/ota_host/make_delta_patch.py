#!/usr/bin/env python3
"""生成 esp_delta_ota 差分 patch，并输出服务器 manifest 的 delta 字段。

用法示例（需在 ESP-IDF 环境或已安装 detools 的 Python 环境执行）：

    python tools/ota_host/make_delta_patch.py \
        --base build_1.0.7/111.bin \
        --new build/111.bin \
        --baseline-version 1.0.7 \
        --version 1.0.8 \
        --channel stable

产物：
    1) 生成 patch 文件（64 字节头 + detools/heatshrink body），命名
       <baseline>_to_<version>.patch；
    2) 自动用 detools 在本地把 patch 套到 base 上做一次全量还原校验；
    3) 打印服务器 manifest.json 待填写的 delta 字段（含 patch_url、
       patch_sha256、target_sha256），并给出目标全量字段供保留。
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

# 与设备端 ota_transport.c 中 kOtaDeltaPatchMagic/kOtaDeltaPatchHeaderBytes 一致
ESP_DELTA_OTA_MAGIC = 0xFCCDDE10
MAGIC_SIZE = 4
DIGEST_SIZE = 32
HEADER_SIZE = 64
RESERVED_HEADER = HEADER_SIZE - (MAGIC_SIZE + DIGEST_SIZE)


def sha256_file(path: str) -> str:
    """计算文件 SHA-256（小写 hex）。"""
    digest = hashlib.sha256()
    with open(path, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def base_image_sha256(base_binary: str) -> str:
    """用 esptool image_info 提取 'Validation Hash'。

    设备端 baseline 校验用 esp_partition_get_sha256(running) 与 patch 头
    比对，而该值正是 esptool image_info 的 Validation Hash（镜像级哈希，
    不是普通文件 sha256），必须从这里取。
    """
    try:
        result = subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", "esp32s3",
             "image_info", base_binary],
            capture_output=True, text=True, check=True, timeout=120)
    except subprocess.CalledProcessError as exc:
        print(f"esptool image_info 执行失败: {exc}\n{exc.stderr}")
        raise
    # esptool v5.x 输出 "Validation hash:"（小写 hash），旧版本为 "Validation Hash:"
    match = re.search(r"Validation [Hh]ash: ([0-9a-fA-F]+)", result.stdout)
    if match is None:
        raise SystemExit("base binary 中未找到 Validation Hash，确认它是 esp32 app 镜像")
    return match.group(1).lower()


def create_patch(base_binary: str, new_binary: str, patch_file: str) -> None:
    """生成带 64 字节头的 patch 文件。"""
    try:
        import detools
    except ImportError:
        raise SystemExit("缺少 detools，请先执行: pip install detools==0.49.0")

    with tempfile.NamedTemporaryFile(suffix=".body", delete=False) as tmp:
        body_path = tmp.name
    try:
        with open(base_binary, "rb") as base_fh, \
                open(new_binary, "rb") as new_fh, \
                open(body_path, "wb") as body_fh:
            detools.create_patch(base_fh, new_fh, body_fh,
                                 compression="heatshrink")
        with open(patch_file, "wb") as out_fh:
            out_fh.write(ESP_DELTA_OTA_MAGIC.to_bytes(MAGIC_SIZE, "little"))
            out_fh.write(bytes.fromhex(base_image_sha256(base_binary)))
            out_fh.write(bytearray(RESERVED_HEADER))
            with open(body_path, "rb") as body_fh:
                while True:
                    chunk = body_fh.read(1 << 16)
                    if not chunk:
                        break
                    out_fh.write(chunk)
    finally:
        if os.path.exists(body_path):
            os.remove(body_path)


def verify_patch(base_binary: str, patch_file: str, new_binary: str) -> None:
    """本地还原校验：patch 套 base 后与 new binary 字节一致才放行发布。"""
    try:
        import detools
    except ImportError:
        print("跳过本地还原校验（缺 detools）")
        return
    with tempfile.NamedTemporaryFile(suffix=".body", delete=False) as tmp_fh:
        body_path = tmp_fh.name
        with open(patch_file, "rb") as src_fh:
            src_fh.seek(HEADER_SIZE)
            tmp_fh.write(src_fh.read())
    applied_path = None
    try:
        with tempfile.NamedTemporaryFile(suffix=".new", delete=False) as out_fh:
            applied_path = out_fh.name
        detools.apply_patch_filenames(base_binary, body_path, applied_path)
        ok = sha256_file(applied_path) == sha256_file(new_binary)
        if not ok:
            raise SystemExit("本地还原校验失败：还原结果与 new binary 不一致，中止")
        print(f"本地还原校验 OK（还原 sha256 == new binary sha256）")
    finally:
        for path in (body_path, applied_path):
            if path is not None and os.path.exists(path):
                os.remove(path)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="生成 esp_delta_ota 差分 patch 并输出 manifest delta 字段")
    parser.add_argument("--base", required=True, help="旧版本固件 bin（baseline）")
    parser.add_argument("--new", required=True, help="新版本固件 bin（目标）")
    parser.add_argument("--baseline-version", required=True,
                        help="旧版本号，如 1.0.7")
    parser.add_argument("--version", required=True, help="新版本号，如 1.0.8")
    parser.add_argument("--channel", default="stable")
    parser.add_argument("--base-url", default="https://watch.934000.xyz",
                        help="设备端 base_url，用于拼 patch_url/url")
    parser.add_argument("--outdir", default=".",
                        help="patch 输出目录（默认当前目录）")
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    patch_name = f"{args.baseline_version}_to_{args.version}.patch"
    patch_path = os.path.join(args.outdir, patch_name)

    create_patch(args.base, args.new, patch_path)
    verify_patch(args.base, patch_path, args.new)

    patch_sha256 = sha256_file(patch_path)
    target_sha256 = sha256_file(args.new)  # 全量镜像文件 sha256 == 分区前 size 字节 sha256
    patch_url = (f"{args.base_url}/v1/watch/ota/patches/{args.channel}/"
                 f"{patch_name}")
    full_url = (f"{args.base_url}/v1/watch/ota/artifacts/{args.channel}/"
                f"{args.version}/firmware.bin")

    size = os.path.getsize(args.new)
    manifest = {
        "version": args.version,
        "url": full_url,
        "size": size,
        "sha256": target_sha256,
        "channel": args.channel,
        # --- delta 字段（板测时手动写入服务器 manifest，测试完删除） ---
        "baseline_version": args.baseline_version,
        "patch_url": patch_url,
        "patch_size": os.path.getsize(patch_path),
        "patch_sha256": patch_sha256,
        "target_sha256": target_sha256,
    }
    print(f"\npatch 已生成: {patch_path}  ({os.path.getsize(patch_path)} bytes)")
    print(f"patch_sha256: {patch_sha256}")
    print(f"target_sha256: {target_sha256}")
    print("\n服务器 manifest.json 请填写（delta 字段）：")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
