#!/usr/bin/env python3
"""把 LVGL 生成的图标 C 数组(RGB565 / RGB565A8)解码成 PNG,供 Vue 原型使用。

输入: main/ui/generated/images/*.c
输出: watch-vue/src/assets/img/<name>_<w>x<h>_<CF>.png

零依赖:PNG 用 zlib + struct 手写,解析用正则。
用法: uv run python scripts/convert_lvgl_icons.py [--src <images目录>] [--out <输出目录>]
"""
import os
import re
import struct
import sys
import zlib

# 仓库根(脚本位于 tools/ui_prototypes/watch-vue/scripts/)
DEFAULT_SRC = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '..', '..', '..', '..', 'main', 'ui', 'generated', 'images'))
DEFAULT_OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'src', 'assets', 'img'))


def parse_c_file(path):
    """从 LVGL 生成的 .c 文件提取 (w, h, color_format, 字节数组)。"""
    txt = open(path, 'r', encoding='utf-8', errors='replace').read()
    w = int(re.search(r'\.w\s*=\s*(\d+),', txt).group(1))
    h = int(re.search(r'\.h\s*=\s*(\d+),', txt).group(1))
    cf = re.search(r'\.cf\s*=\s*LV_COLOR_FORMAT_(\w+)', txt)
    cf = cf.group(1) if cf else 'UNKNOWN'
    m = re.search(r'\[\]\s*=\s*\{(.*?)\};', txt, re.S)
    if not m:
        m = re.search(r'=\s*\{(.*?)\};', txt, re.S)
    body = m.group(1)
    vals = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body)]
    return w, h, cf, vals


def rgb565_to_rgba(w, h, data, alpha=None):
    """RGB565 小端 + 可选 A8 alpha -> RGBA 字节流。"""
    px = bytearray()
    n = w * h
    for i in range(n):
        lo = data[2 * i]
        hi = data[2 * i + 1]
        v = (hi << 8) | lo
        r5 = (v >> 11) & 0x1F
        g6 = (v >> 5) & 0x3F
        b5 = v & 0x1F
        r = (r5 << 3) | (r5 >> 2)
        g = (g6 << 2) | (g6 >> 4)
        b = (b5 << 3) | (b5 >> 2)
        a = alpha[i] if alpha is not None else 255
        px += bytes((r, g, b, a))
    return bytes(px)


def write_png(path, w, h, rgba):
    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)

    raw = b''.join(b'\x00' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def main():
    src = DEFAULT_SRC
    out = DEFAULT_OUT
    args = sys.argv[1:]
    for i, a in enumerate(args):
        if a == '--src' and i + 1 < len(args):
            src = args[i + 1]
        elif a == '--out' and i + 1 < len(args):
            out = args[i + 1]

    os.makedirs(out, exist_ok=True)
    n_ok = n_skip = n_err = 0
    for name in sorted(os.listdir(src)):
        if not name.endswith('.c'):
            continue
        path = os.path.join(src, name)
        try:
            w, h, cf, data = parse_c_file(path)
        except Exception as e:
            print(f'[ERR ] {name}: {e}')
            n_err += 1
            continue

        if cf not in ('RGB565A8', 'RGB565'):
            print(f'[SKIP] {name}: 不支持的格式 {cf}')
            n_skip += 1
            continue

        # 需要的数据量 = RGB565(w*h*2) + 可选 A8(w*h)
        need = w * h * 2
        if cf == 'RGB565A8':
            need += w * h
        if len(data) < need:
            print(f'[ERR ] {name}: 数据不足 {len(data)} < {need}')
            n_err += 1
            continue

        rgb = data[:w * h * 2]
        alpha = data[w * h * 2:w * h * 3] if cf == 'RGB565A8' else None
        rgba = rgb565_to_rgba(w, h, rgb, alpha)

        stem = re.sub(r'^_', '', name[:-2])  # _heart_RGB565A8_70x70.c -> heart_RGB565A8_70x70
        png_name = f'{stem}.png'
        write_png(os.path.join(out, png_name), w, h, rgba)
        n_ok += 1

    print(f'完成:成功 {n_ok},跳过 {n_skip},失败 {n_err} -> {out}')


if __name__ == '__main__':
    main()
