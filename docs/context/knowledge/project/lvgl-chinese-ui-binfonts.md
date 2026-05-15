---
id: lvgl-chinese-ui-binfonts
tags: [project, ui, lvgl, fonts, resources, littlefs]
summary: 记录通用中文 LVGL UI 默认使用编译进固件的 LVGL C 字体，不要把中文 label 绑定到 Montserrat，并保持压缩字体运行时开关开启。
last_reviewed: 2026-05-12
memory_type: semantic
scope: repo
owners: main/ui/custom/ui_chinese_fonts.h, main/ui/custom/fonts, scripts/lvgl_fonts, tools/lvgl_fonts, resources/fonts
triggers: lvgl, chinese, fonts, ui_chinese_fonts, binfont, resources
evidence_level: observed
---

# LVGL 中文 UI 字体链路

- 通用中文 LVGL UI 默认使用编译进固件的 LVGL C 字体。
- 页面侧直接引用 `ui_chinese_fonts.h` 声明的字体符号，典型用法是把 `&lv_font_montserrat_lxgw_tghz_level1_3500_24_4` 传给 `lv_obj_set_style_text_font()`。
- 中文 label 不应直接使用 `lv_font_montserrat*`，否则 Montserrat 缺少中文字形时会出现方框、缺字或乱码。
- 当前预置字体是《通用规范汉字表》一级 3500 字的 `16 / 22 / 24 / 27` 四个字号，`bpp=4`。
- `main/ui/custom/fonts/*.c` 当前使用 LVGL 压缩字体（`.bitmap_format = 1`）；必须保持 `CONFIG_LV_USE_FONT_COMPRESSED=y`（`sdkconfig` 和 `sdkconfig.defaults`），否则中文 label 绘制时 `lv_font_get_glyph_bitmap()` 会返回 `NULL` 并在 `draw_letter_cb` 触发 `LoadProhibited`。
- 字体源、字符集和生成脚本都保存在仓库内：`tools/lvgl_fonts/` 与 `scripts/lvgl_fonts/`。
- 编译期 C 字体生成不接入 `idf.py build`；普通构建只消费已提交到 `main/ui/custom/fonts/` 的 `.c` 产物，避免 Node/npx 影响固件构建稳定性。
- `resources/fonts/*.bin` 仍保留为后续升级到 LittleFS 可替换字体的资源方案，运行时可通过 `lv_binfont_create("A:/fonts/...")` 加载，但不是当前默认主路径。
- 手写中文 UI 默认直接使用 `16 / 22 / 24 / 27` 四个编译期字体对象；不再额外包一层 runtime helper。
- 大字号中文字体，例如 `46 / 58`，默认按页面实际中文文案生成子集字体，不提交 3500 字全量。
- 未预置字号应 fail-visible：打印明确错误日志，不静默回退到 Montserrat，也不回退到 AI 页面 `ui_font_assets` / xiaozhi cbin 字体链。

# Agent 生成中文 UI 默认流程

- 只要新增或修改含中文文案的 LVGL UI，先确认页面能访问 `ui_chinese_fonts.h`；在 `main/ui/custom` 下通常可通过 `custom.h` 间接获得声明。
- 中文 label 设置字体时，默认直接使用 `&lv_font_montserrat_lxgw_tghz_level1_3500_16_4`、`22_4`、`24_4` 或 `27_4`。
- 纯英文、数字和 ASCII 符号 label 可以继续使用 Montserrat；不要把“所有 label 都换中文字体”当成硬规则。
- 若页面需要 `46 / 58` 这类大字号中文字体，先基于该页面实际中文文案生成子集 `.c` 字体，再提交到 `main/ui/custom/fonts/` 并补 `LV_FONT_DECLARE(...)`。
- 生成或修改后运行 `uv run python scripts/lvgl_fonts/scan_lvgl_chinese_text.py` 做防呆扫描，确认没有中文 label 直接使用 `lv_font_montserrat*`。
- 生成或修改中文字体源、`sdkconfig` 或 `sdkconfig.defaults` 后，运行 `uv run python -m pytest tests/test_ui_chinese_fonts_source.py`，确认压缩字体运行时配置没有被关掉。

# 与 AI 页面字体链的边界

- `assets` SPIFFS 分区仍是 AI 页面 xiaozhi cbin 字体运行时覆盖路径。
- `ui_font_assets_*()` 只服务 hand-written AI 页面，保留 `78/xiaozhi-fonts` cbin 内置字体 + assets 覆盖的链路。
- `main/ui/custom/fonts/*.c` 面向通用 LVGL UI 和未来 agent 新增中文页面，不替代 `ui_font_assets`。
- `resources/fonts/*.bin` 只作为未来可替换运行时字体方案保留。
