from __future__ import annotations

import html
import math
from pathlib import Path

import cairosvg


OUT_DIR = Path(__file__).resolve().parent
W = 2048
H = 1152

COLORS = {
    "navy": "#0f172a",
    "text": "#111827",
    "muted": "#6b7280",
    "line": "#d1d5db",
    "blue": "#2563eb",
    "blue_fill": "#eff6ff",
    "blue_stroke": "#bfdbfe",
    "cyan": "#0891b2",
    "cyan_fill": "#ecfeff",
    "cyan_stroke": "#a5f3fc",
    "green": "#16a34a",
    "green_fill": "#f0fdf4",
    "green_stroke": "#bbf7d0",
    "orange": "#ea580c",
    "orange_fill": "#fff7ed",
    "orange_stroke": "#fed7aa",
    "red": "#dc2626",
    "red_fill": "#fef2f2",
    "red_stroke": "#fecaca",
    "purple": "#7c3aed",
    "purple_fill": "#faf5ff",
    "purple_stroke": "#ddd6fe",
    "gray_fill": "#f9fafb",
    "gray_stroke": "#e5e7eb",
}


def esc(text: str) -> str:
    return html.escape(text, quote=True)


def marker_defs() -> str:
    markers = []
    for name, color in [
        ("blue", COLORS["blue"]),
        ("red", COLORS["red"]),
        ("green", COLORS["green"]),
        ("orange", COLORS["orange"]),
        ("purple", COLORS["purple"]),
        ("cyan", COLORS["cyan"]),
        ("gray", COLORS["muted"]),
    ]:
        markers.append(
            f'<marker id="arrow-{name}" markerWidth="12" markerHeight="9" refX="11" refY="4.5" orient="auto">'
            f'<polygon points="0 0, 12 4.5, 0 9" fill="{color}"/></marker>'
        )
    return "\n".join(markers)


def svg_start(title: str) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">',
        "<style>",
        "text { font-family: 'Microsoft YaHei', 'PingFang SC', 'SimHei', 'Helvetica Neue', Arial, sans-serif; }",
        ".title { font-size: 44px; font-weight: 700; fill: #0f172a; }",
        ".label { font-size: 31px; font-weight: 700; fill: #111827; }",
        ".small { font-size: 25px; font-weight: 600; fill: #374151; }",
        ".hint { font-size: 22px; font-weight: 600; fill: #6b7280; }",
        "</style>",
        "<defs>",
        marker_defs(),
        "</defs>",
        f'<rect width="{W}" height="{H}" fill="#ffffff"/>',
        f'<text x="{W / 2}" y="92" text-anchor="middle" class="title">{esc(title)}</text>',
    ]


def svg_end(lines: list[str]) -> str:
    lines.append("</svg>")
    return "\n".join(lines)


def text_block(x: float, y: float, lines: list[str], cls: str = "label", anchor: str = "middle", gap: int = 38) -> str:
    out = []
    total = (len(lines) - 1) * gap
    start = y - total / 2
    for i, line in enumerate(lines):
        out.append(f'<text x="{x}" y="{start + i * gap}" text-anchor="{anchor}" class="{cls}">{esc(line)}</text>')
    return "\n".join(out)


def node(x: float, y: float, w: float, h: float, label: str | list[str], fill: str, stroke: str, icon: str | None = None, cls: str = "label") -> str:
    labels = [label] if isinstance(label, str) else label
    cx = x + w / 2
    lines = [
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="20" fill="{fill}" stroke="{stroke}" stroke-width="3"/>'
    ]
    if icon:
        # Keep the node content as a clean vertical stack:
        # centered icon on top, centered label below it, both inside the card.
        if h <= 150:
            icon_scale, icon_y, text_y = 0.55, y + 48, y + h - 34
        elif h <= 180:
            icon_scale, icon_y, text_y = 0.62, y + 55, y + h - 38
        elif h <= 220:
            icon_scale, icon_y, text_y = 0.70, y + 66, y + h - 44
        else:
            icon_scale, icon_y, text_y = 0.82, y + 88, y + h - 58
        icon_color = stroke.replace("f", "e") if stroke.startswith("#f") else stroke
        raw_icon = icon_group(icon, cx, icon_y, icon_color)
        lines.append(
            f'<g transform="translate({cx} {icon_y}) scale({icon_scale}) translate({-cx} {-icon_y})">{raw_icon}</g>'
        )
    else:
        text_y = y + h / 2 + 10
    lines.append(text_block(cx, text_y, labels, cls=cls))
    return "\n".join(lines)


def pill(x: float, y: float, w: float, h: float, label: str, fill: str, stroke: str) -> str:
    return (
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{h / 2}" fill="{fill}" stroke="{stroke}" stroke-width="3"/>'
        + text_block(x + w / 2, y + h / 2 + 10, [label], cls="label")
    )


def arrow(x1: float, y1: float, x2: float, y2: float, color: str = "blue", dashed: bool = False, label: str | None = None) -> str:
    dash = ' stroke-dasharray="10,8"' if dashed else ""
    midx = (x1 + x2) / 2
    midy = (y1 + y2) / 2
    out = [
        f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{COLORS[color]}" stroke-width="4" fill="none"{dash} marker-end="url(#arrow-{color})"/>'
    ]
    if label:
        tw = max(96, len(label) * 24 + 24)
        out.append(f'<rect x="{midx - tw / 2}" y="{midy - 44}" width="{tw}" height="34" rx="9" fill="#ffffff" opacity="0.96"/>')
        out.append(f'<text x="{midx}" y="{midy - 19}" text-anchor="middle" class="hint">{esc(label)}</text>')
    return "\n".join(out)


def path_arrow(d: str, color: str = "blue", dashed: bool = False, label: tuple[float, float, str] | None = None) -> str:
    dash = ' stroke-dasharray="10,8"' if dashed else ""
    out = [f'<path d="{d}" stroke="{COLORS[color]}" stroke-width="4" fill="none"{dash} marker-end="url(#arrow-{color})"/>']
    if label:
        x, y, text = label
        tw = max(96, len(text) * 24 + 24)
        out.append(f'<rect x="{x - tw / 2}" y="{y - 34}" width="{tw}" height="34" rx="9" fill="#ffffff" opacity="0.96"/>')
        out.append(f'<text x="{x}" y="{y - 9}" text-anchor="middle" class="hint">{esc(text)}</text>')
    return "\n".join(out)


def icon_group(kind: str, cx: float, cy: float, color: str) -> str:
    if kind == "watch":
        return (
            f'<rect x="{cx-34}" y="{cy-44}" width="68" height="88" rx="18" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<rect x="{cx-18}" y="{cy-64}" width="36" height="22" rx="8" fill="{color}" opacity="0.22"/>'
            f'<rect x="{cx-18}" y="{cy+42}" width="36" height="22" rx="8" fill="{color}" opacity="0.22"/>'
            f'<circle cx="{cx}" cy="{cy}" r="12" fill="{color}"/>'
        )
    if kind == "mic":
        return (
            f'<rect x="{cx-16}" y="{cy-42}" width="32" height="58" rx="16" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<path d="M {cx-34},{cy-10} Q {cx},{cy+40} {cx+34},{cy-10}" stroke="{color}" stroke-width="4" fill="none"/>'
            f'<line x1="{cx}" y1="{cy+40}" x2="{cx}" y2="{cy+64}" stroke="{color}" stroke-width="4"/>'
            f'<line x1="{cx-26}" y1="{cy+64}" x2="{cx+26}" y2="{cy+64}" stroke="{color}" stroke-width="4"/>'
        )
    if kind == "phone":
        return (
            f'<rect x="{cx-30}" y="{cy-50}" width="60" height="100" rx="14" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<rect x="{cx-22}" y="{cy-18}" width="44" height="28" rx="7" fill="{color}" opacity="0.20"/>'
            f'<circle cx="{cx}" cy="{cy+35}" r="5" fill="{color}"/>'
        )
    if kind == "cloud":
        return (
            f'<path d="M {cx-48},{cy+18} C {cx-58},{cy-18} {cx-18},{cy-28} {cx-4},{cy-12} C {cx+14},{cy-42} {cx+62},{cy-24} {cx+54},{cy+16} C {cx+70},{cy+20} {cx+58},{cy+46} {cx+38},{cy+44} L {cx-36},{cy+44} C {cx-62},{cy+44} {cx-68},{cy+22} {cx-48},{cy+18} Z" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
        )
    if kind == "sd":
        return (
            f'<path d="M {cx-38},{cy-50} L {cx+18},{cy-50} L {cx+38},{cy-30} L {cx+38},{cy+50} L {cx-38},{cy+50} Z" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<rect x="{cx-22}" y="{cy-38}" width="12" height="24" fill="{color}" opacity="0.55"/>'
            f'<rect x="{cx-4}" y="{cy-38}" width="12" height="24" fill="{color}" opacity="0.55"/>'
            f'<rect x="{cx+14}" y="{cy-38}" width="12" height="24" fill="{color}" opacity="0.55"/>'
        )
    if kind == "brain":
        return (
            f'<path d="M {cx-45},{cy} C {cx-50},{cy-32} {cx-12},{cy-48} {cx},{cy-24} C {cx+18},{cy-50} {cx+56},{cy-26} {cx+42},{cy+8} C {cx+54},{cy+36} {cx+14},{cy+52} {cx},{cy+28} C {cx-18},{cy+54} {cx-58},{cy+30} {cx-45},{cy} Z" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<path d="M {cx-22},{cy-16} C {cx-5},{cy-4} {cx-16},{cy+12} {cx},{cy+24} M {cx+20},{cy-18} C {cx+4},{cy-4} {cx+20},{cy+10} {cx+2},{cy+28}" stroke="{color}" stroke-width="3" fill="none" opacity="0.65"/>'
        )
    if kind == "laptop":
        return (
            f'<rect x="{cx-54}" y="{cy-42}" width="108" height="68" rx="8" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<path d="M {cx-74},{cy+38} L {cx+74},{cy+38} L {cx+56},{cy+56} L {cx-56},{cy+56} Z" fill="{color}" opacity="0.18" stroke="{color}" stroke-width="3"/>'
        )
    if kind == "wave":
        return f'<path d="M {cx-64},{cy} C {cx-44},{cy-44} {cx-24},{cy+44} {cx-4},{cy} C {cx+16},{cy-44} {cx+36},{cy+44} {cx+64},{cy}" stroke="{color}" stroke-width="5" fill="none"/>'
    if kind == "car":
        return (
            f'<path d="M {cx-58},{cy+16} L {cx-42},{cy-18} L {cx+34},{cy-18} L {cx+58},{cy+16} Z" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<rect x="{cx-66}" y="{cy+8}" width="132" height="42" rx="12" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<circle cx="{cx-38}" cy="{cy+52}" r="11" fill="{color}"/><circle cx="{cx+38}" cy="{cy+52}" r="11" fill="{color}"/>'
        )
    if kind == "alarm":
        return (
            f'<path d="M {cx-44},{cy+36} L {cx-34},{cy-8} Q {cx},{cy-56} {cx+34},{cy-8} L {cx+44},{cy+36} Z" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<circle cx="{cx}" cy="{cy+8}" r="13" fill="{color}" opacity="0.7"/>'
            f'<path d="M {cx-56},{cy-30} L {cx-76},{cy-50} M {cx+56},{cy-30} L {cx+76},{cy-50}" stroke="{color}" stroke-width="5"/>'
        )
    if kind == "user":
        return (
            f'<circle cx="{cx}" cy="{cy-24}" r="18" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            f'<path d="M {cx-36},{cy+48} Q {cx-34},{cy+4} {cx},{cy+4} Q {cx+34},{cy+4} {cx+36},{cy+48}" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
        )
    if kind == "chip":
        pins = []
        for dx in [-48, -24, 0, 24, 48]:
            pins.append(f'<line x1="{cx+dx}" y1="{cy-52}" x2="{cx+dx}" y2="{cy-68}" stroke="{color}" stroke-width="4"/>')
            pins.append(f'<line x1="{cx+dx}" y1="{cy+52}" x2="{cx+dx}" y2="{cy+68}" stroke="{color}" stroke-width="4"/>')
        return (
            "".join(pins)
            + f'<rect x="{cx-62}" y="{cy-52}" width="124" height="104" rx="14" fill="#ffffff" stroke="{color}" stroke-width="4"/>'
            + f'<rect x="{cx-28}" y="{cy-22}" width="56" height="44" rx="8" fill="{color}" opacity="0.2"/>'
        )
    return f'<circle cx="{cx}" cy="{cy}" r="36" fill="#ffffff" stroke="{color}" stroke-width="4"/>'


def band(x: float, y: float, w: float, h: float, title: str, stroke: str, fill: str) -> str:
    return (
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="24" fill="{fill}" stroke="{stroke}" stroke-width="3" stroke-dasharray="12,10" opacity="0.95"/>'
        f'<text x="{x + 30}" y="{y + 48}" class="small" fill="{stroke}">{esc(title)}</text>'
    )


def write_svg_png(filename: str, title: str, body: list[str]) -> None:
    svg = svg_start(title)
    svg.extend(body)
    svg_text = svg_end(svg)
    svg_path = OUT_DIR / filename.replace(".png", ".svg")
    png_path = OUT_DIR / filename
    svg_path.write_text(svg_text, encoding="utf-8")
    cairosvg.svg2png(bytestring=svg_text.encode("utf-8"), write_to=str(png_path), output_width=W, output_height=H)
    print(f"generated {png_path.name}")


def f00():
    body = []
    cx, cy = W / 2, 580
    body.append(node(cx - 200, cy - 140, 400, 280, ["ESP32-S3 手表"], COLORS["cyan_fill"], COLORS["cyan_stroke"], "watch"))
    specs = [
        (300, 255, "危险声识别", COLORS["red_fill"], COLORS["red_stroke"], "mic"),
        (300, 760, "本地提醒", COLORS["orange_fill"], COLORS["orange_stroke"], "phone"),
        (1378, 255, "任务辅助", COLORS["purple_fill"], COLORS["purple_stroke"], "brain"),
        (1378, 760, "样本闭环", COLORS["green_fill"], COLORS["green_stroke"], "sd"),
    ]
    for x, y, label, fill, stroke, ic in specs:
        body.append(node(x, y, 370, 210, label, fill, stroke, ic))
    body.extend([
        arrow(670, 360, 824, 465, "red"),
        arrow(824, 700, 670, 865, "orange"),
        arrow(1224, 465, 1378, 360, "purple"),
        arrow(1224, 700, 1378, 865, "green", dashed=True),
        path_arrow("M 1565,760 C 1470,610 1290,525 1225,515", "green", dashed=True),
    ])
    write_svg_png("f00_function_overview.png", "聆安 Watch 功能总览图", body)


def f00b():
    body = []
    blocks = [
        (205, 330, "日常出行", COLORS["blue_fill"], COLORS["blue_stroke"], "car"),
        (740, 330, "公共场所", COLORS["red_fill"], COLORS["red_stroke"], "alarm"),
        (1275, 330, "任务辅助", COLORS["purple_fill"], COLORS["purple_stroke"], "watch"),
    ]
    for x, y, label, fill, stroke, ic in blocks:
        body.append(node(x, y, 360, 230, label, fill, stroke, ic))
        body.append(arrow(x + 180, y + 230, 1024, 745, "blue" if label != "公共场所" else "red"))
    body.append(node(754, 755, 540, 230, "听障安全辅助", COLORS["green_fill"], COLORS["green_stroke"], "user"))
    write_svg_png("f00b_application_scenario.png", "典型应用场景图", body)


def f00c():
    body = []
    labels = ["需求分析", "系统架构", "端侧识别", "状态机提醒", "任务辅助", "闭环优化"]
    icons = ["user", "cloud", "mic", "alarm", "brain", "chip"]
    fills = [COLORS["blue_fill"], COLORS["cyan_fill"], COLORS["blue_fill"], COLORS["red_fill"], COLORS["purple_fill"], COLORS["orange_fill"]]
    strokes = [COLORS["blue_stroke"], COLORS["cyan_stroke"], COLORS["blue_stroke"], COLORS["red_stroke"], COLORS["purple_stroke"], COLORS["orange_stroke"]]
    xs = [115, 445, 775, 1105, 1435, 1765]
    for i, (x, label) in enumerate(zip(xs, labels)):
        body.append(node(x, 460, 250, 210, label, fills[i], strokes[i], icons[i]))
        if i < len(xs) - 1:
            body.append(arrow(x + 250, 565, xs[i + 1], 565, "blue" if i < 4 else "orange"))
    write_svg_png("f00c_design_process.png", "设计流程图", body)


def f01():
    body = []
    y = 445
    specs = [
        (70, "环境危险声", COLORS["red_fill"], COLORS["red_stroke"], "wave"),
        (410, "ESP32-S3 手表", COLORS["cyan_fill"], COLORS["cyan_stroke"], "watch"),
        (750, "云服务端", COLORS["green_fill"], COLORS["green_stroke"], "cloud"),
        (1090, "Hermes", COLORS["purple_fill"], COLORS["purple_stroke"], "brain"),
        (1430, "电脑端协同", COLORS["blue_fill"], COLORS["blue_stroke"], "laptop"),
    ]
    for x, label, fill, stroke, ic in specs:
        body.append(node(x, y, 280, 210, label, fill, stroke, ic))
    for x1, x2, c in [(350, 410, "blue"), (690, 750, "green"), (1030, 1090, "purple"), (1370, 1430, "blue")]:
        body.append(arrow(x1, y + 105, x2, y + 105, c))
    body.append(node(410, 790, 280, 170, "本地提醒", COLORS["red_fill"], COLORS["red_stroke"], "phone", cls="small"))
    body.append(node(855, 790, 420, 170, "通知/样本/元数据", COLORS["orange_fill"], COLORS["orange_stroke"], "sd", cls="small"))
    body.append(arrow(550, y + 210, 550, 790, "red"))
    body.append(path_arrow("M 640,655 L 640,720 L 920,720 L 920,790", "orange", dashed=True))
    body.append(path_arrow("M 1065,790 L 1065,720 L 890,720 L 890,655", "orange", dashed=True))
    write_svg_png("f01_system_architecture.png", "系统整体框图", body)


def f02():
    body = [
        band(90, 220, 1868, 330, "手表端本地安全链路", COLORS["blue"], "#eff6ff"),
        band(90, 640, 1868, 330, "Hermes 任务辅助链路", COLORS["purple"], "#faf5ff"),
    ]
    top = [("音频采集", "mic"), ("ESP-DL 推理", "chip"), ("风险融合", "wave"), ("提醒管理", "alarm"), ("LVGL UI", "phone")]
    bottom = [("语音/文本", "user"), ("云服务端", "cloud"), ("Hermes", "brain"), ("JSON 回执", "sd"), ("手表显示", "watch")]
    xs = [160, 520, 880, 1240, 1600]
    for i, (label, ic) in enumerate(top):
        body.append(node(xs[i], 335, 280, 150, label, COLORS["blue_fill"] if i < 2 else COLORS["green_fill"] if i == 2 else COLORS["red_fill"] if i == 3 else COLORS["cyan_fill"], COLORS["blue_stroke"] if i < 2 else COLORS["green_stroke"] if i == 2 else COLORS["red_stroke"] if i == 3 else COLORS["cyan_stroke"], ic, cls="small"))
        if i < 4:
            body.append(arrow(xs[i] + 280, 410, xs[i + 1], 410, "blue"))
    for i, (label, ic) in enumerate(bottom):
        body.append(node(xs[i], 755, 280, 150, label, COLORS["purple_fill"] if i in (2, 3) else COLORS["green_fill"] if i == 1 else COLORS["cyan_fill"], COLORS["purple_stroke"] if i in (2, 3) else COLORS["green_stroke"] if i == 1 else COLORS["cyan_stroke"], ic, cls="small"))
        if i < 4:
            body.append(arrow(xs[i] + 280, 830, xs[i + 1], 830, "purple"))
    body.append(node(170, 1000, 320, 100, "样本记录", COLORS["orange_fill"], COLORS["orange_stroke"], None, cls="small"))
    body.append(node(1558, 1000, 320, 100, "告警投递", COLORS["red_fill"], COLORS["red_stroke"], None, cls="small"))
    body.append(path_arrow("M 880,485 L 880,1035 L 490,1035", "orange", dashed=True))
    body.append(path_arrow("M 1380,485 L 1718,1000", "red", dashed=True))
    write_svg_png("f02_software_architecture.png", "软件整体架构图", body)


def f03():
    body = []
    top = [("麦克风采集", "mic"), ("滑窗重采样", "wave"), ("Fbank 特征", "chip"), ("ESP-DL 推理", "brain")]
    bottom = [("单窗输出", "wave"), ("证据融合", "chip"), ("本地提醒", "phone")]
    xs_top = [180, 600, 1020, 1440]
    for i, (label, ic) in enumerate(top):
        body.append(node(xs_top[i], 330, 300, 170, label, COLORS["blue_fill"], COLORS["blue_stroke"], ic, cls="small"))
        if i < 3:
            body.append(arrow(xs_top[i] + 300, 415, xs_top[i + 1], 415, "blue"))
    xs_bot = [1440, 1020, 600]
    fills = [COLORS["blue_fill"], COLORS["green_fill"], COLORS["red_fill"]]
    strokes = [COLORS["blue_stroke"], COLORS["green_stroke"], COLORS["red_stroke"]]
    for i, ((label, ic), x) in enumerate(zip(bottom, xs_bot)):
        body.append(node(x, 710, 300, 170, label, fills[i], strokes[i], ic, cls="small"))
    body.append(arrow(1590, 500, 1590, 710, "blue"))
    body.append(arrow(1440, 795, 1320, 795, "green"))
    body.append(arrow(1020, 795, 900, 795, "red"))
    write_svg_png("f03_edge_ai_flow.png", "端侧危险声识别流程图", body)


def f04():
    body = []
    states = {
        "Off": (160, 455, COLORS["gray_fill"], COLORS["gray_stroke"]),
        "Monitoring": (500, 455, COLORS["green_fill"], COLORS["green_stroke"]),
        "Suspicious": (880, 455, COLORS["orange_fill"], COLORS["orange_stroke"]),
        "Alerting": (1260, 455, COLORS["red_fill"], COLORS["red_stroke"]),
        "Cooldown": (880, 780, COLORS["blue_fill"], COLORS["blue_stroke"]),
    }
    for label, (x, y, fill, stroke) in states.items():
        body.append(pill(x, y, 270, 120, label, fill, stroke))
    body.append(arrow(430, 515, 500, 515, "green", label="开启"))
    body.append(arrow(770, 515, 880, 515, "orange", label="单窗危险"))
    body.append(arrow(1150, 515, 1260, 515, "red", label="连续确认"))
    body.append(path_arrow("M 1395,575 L 1395,840 L 1150,840", "red", label=(1280, 840, "保持结束")))
    body.append(path_arrow("M 880,840 L 635,840 L 635,575", "blue", label=(730, 840, "冷却结束")))
    write_svg_png("f04_risk_state_machine.png", "风险状态机图", body)


def f05():
    body = []
    specs = [
        (80, "用户", COLORS["blue_fill"], COLORS["blue_stroke"], "user"),
        (350, "手表端", COLORS["cyan_fill"], COLORS["cyan_stroke"], "watch"),
        (650, "云服务端", COLORS["green_fill"], COLORS["green_stroke"], "cloud"),
        (985, "ASR", COLORS["blue_fill"], COLORS["blue_stroke"], "wave"),
        (1235, "Hermes", COLORS["purple_fill"], COLORS["purple_stroke"], "brain"),
        (1515, "电脑端工具", COLORS["orange_fill"], COLORS["orange_stroke"], "laptop"),
    ]
    for x, label, fill, stroke, ic in specs:
        body.append(node(x, 410, 230, 170, label, fill, stroke, ic, cls="small"))
    for x1, x2, c in [(310, 350, "blue"), (580, 650, "green"), (880, 985, "blue"), (1215, 1235, "purple"), (1465, 1515, "orange")]:
        body.append(arrow(x1, 495, x2, 495, c))
    body.append(node(735, 770, 360, 150, "JSON 回执", COLORS["orange_fill"], COLORS["orange_stroke"], "sd", cls="small"))
    body.append(path_arrow("M 1350,580 L 1350,845 L 1095,845", "orange", dashed=True))
    body.append(path_arrow("M 735,845 L 465,845 L 465,580", "orange"))
    write_svg_png("f05_hermes_flow.png", "Hermes 任务辅助流程图", body)


def f06():
    body = []
    top = [
        (165, 305, "危险告警", COLORS["red_fill"], COLORS["red_stroke"], "alarm"),
        (570, 305, "连续 PCM", COLORS["blue_fill"], COLORS["blue_stroke"], "wave"),
        (975, 305, "SD 本地缓存", COLORS["orange_fill"], COLORS["orange_stroke"], "sd"),
        (1380, 305, "人工导出", COLORS["green_fill"], COLORS["green_stroke"], "cloud"),
    ]
    bottom = [
        (1380, 715, "样本积累", COLORS["orange_fill"], COLORS["orange_stroke"], "sd"),
        (975, 715, "误报分析", COLORS["blue_fill"], COLORS["blue_stroke"], "wave"),
        (570, 715, "训练优化", COLORS["purple_fill"], COLORS["purple_stroke"], "brain"),
        (165, 715, "量化部署", COLORS["cyan_fill"], COLORS["cyan_stroke"], "chip"),
    ]
    all_nodes = top + bottom
    for x, y, label, fill, stroke, ic in all_nodes:
        body.append(node(x, y, 290, 180, label, fill, stroke, ic, cls="small"))
    body.extend([
        arrow(455, 395, 570, 395, "blue"),
        arrow(860, 395, 975, 395, "orange"),
        arrow(1265, 395, 1380, 395, "green"),
        path_arrow("M 1525,485 L 1525,620 L 1525,715", "green"),
        arrow(1380, 805, 1265, 805, "orange"),
        arrow(975, 805, 860, 805, "blue"),
        arrow(570, 805, 455, 805, "purple"),
        path_arrow("M 165,805 L 95,805 L 95,395 L 165,395", "cyan", dashed=True),
    ])
    body.append(text_block(1024, 610, ["端侧识别", "本地缓存", "离线优化", "量化回部署"], cls="label", gap=42))
    write_svg_png("f06_data_loop.png", "数据闭环与模型优化流程图", body)


def f12():
    body = []
    specs = [
        (105, "ESP32-S3 手表", COLORS["cyan_fill"], COLORS["cyan_stroke"], "watch"),
        (465, "危险告警", COLORS["red_fill"], COLORS["red_stroke"], "alarm"),
        (825, "后台发送", COLORS["green_fill"], COLORS["green_stroke"], "cloud"),
        (1185, "云服务端", COLORS["purple_fill"], COLORS["purple_stroke"], "cloud"),
        (1545, "手机通知栏", COLORS["orange_fill"], COLORS["orange_stroke"], "phone"),
    ]
    for x, label, fill, stroke, ic in specs:
        body.append(node(x, 470, 280, 180, label, fill, stroke, ic, cls="small"))
    for x1, x2, color in [(385, 465, "red"), (745, 825, "green"), (1105, 1185, "purple"), (1465, 1545, "orange")]:
        body.append(arrow(x1, 560, x2, 560, color))
    write_svg_png("f12_phone_alert_chain.png", "危险告警手机通知栏推送链路", body)


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for fn in [f00, f00b, f00c, f01, f02, f03, f04, f05, f06, f12]:
        fn()


if __name__ == "__main__":
    main()
