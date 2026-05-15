---
id: attempt-lvgl-compressed-font-draw-crash-20260512
tags: context, run, attempt-log, lvgl, fonts, crash, compressed-font
summary: 诊断 LVGL draw_letter_cb LoadProhibited：自定义中文 C 字体为压缩格式但 sdkconfig 未启用 LV_USE_FONT_COMPRESSED，导致 get_glyph_bitmap 返回 NULL。
status: active
result: success
last_reviewed: 2026-05-12
memory_type: episodic
scope: task
owners: sdkconfig, sdkconfig.defaults, main/ui/custom/fonts, tests/test_ui_chinese_fonts_source.py, docs/context/knowledge/project/lvgl-chinese-ui-binfonts.md
triggers: draw_letter_cb, LoadProhibited, LV_USE_FONT_COMPRESSED, lvgl font, 中文字体, EXCVADDR 0x10
evidence_level: observed
---

# LVGL 中文压缩字体绘制崩溃诊断记录

## 背景

- 板端日志在启动后进入 LVGL 绘制线程 panic：`Guru Meditation Error: Core 0 panic'ed (LoadProhibited)`。
- 回溯落在 `managed_components/lvgl__lvgl/src/draw/sw/lv_draw_sw_letter.c:179 draw_letter_cb`，`EXCVADDR=0x00000010`。
- 同轮日志没有显示 Safety Monitor 后台推理启动，因此先按 LVGL 字体绘制链路排查。

## 环境

- 工作区：`D:\esp32S3\111`
- ESP-IDF：`D:\esp-idf\v5.5.3\esp-idf\export.ps1`
- 相关页面：`main/ui/custom/danger_detection_view.c` 使用中文 label 和 `ui_chinese_fonts.h` 编译期中文字体。

## 操作

- 查阅 `lv_draw_sw_letter.c`、`lv_draw_label.c` 和 `lv_font_fmt_txt.c`，确认 `draw_letter_cb` 会直接解引用 `lv_font_get_glyph_bitmap()` 返回的 `glyph_data`。
- 检查 `main/ui/custom/fonts/lv_font_montserrat_lxgw_tghz_level1_3500_*_4.c`，确认字体源使用 `.bitmap_format = 1` 压缩格式。
- 检查 `sdkconfig`，发现 `# CONFIG_LV_USE_FONT_COMPRESSED is not set`。
- 在 `sdkconfig` 和 `sdkconfig.defaults` 中启用 `CONFIG_LV_USE_FONT_COMPRESSED=y`。
- 在 `tests/test_ui_chinese_fonts_source.py` 增加回归测试，确保压缩中文字体源存在时运行时配置保持开启。

## 观测

- `uv run python -m pytest tests/test_ui_chinese_fonts_source.py` 通过，结果为 `5 passed`。
- `uv run python scripts/lvgl_fonts/scan_lvgl_chinese_text.py` 通过，没有发现中文 label 直接绑定 Montserrat。
- `. 'D:\esp-idf\v5.5.3\esp-idf\export.ps1'; idf.py fullclean build` 通过。
- 构建产物：`111.bin` 大小 `0x8d4c50`，`factory` 分区剩余 `0x12b3b0`，约 `12%`。

## 结论

- 本次 `draw_letter_cb` / `EXCVADDR=0x10` 的根因是：中文 C 字体使用 LVGL 压缩格式，但运行时未启用 `LV_USE_FONT_COMPRESSED`，导致字形 bitmap 获取返回 `NULL`，随后 LVGL 软件绘制线程解引用空指针。
- 该崩溃与网络连接失败日志无直接关系，也不是危险识别后台推理日志本身触发。
- 以后新增或修改中文 LVGL 字体时，必须同时保留 `CONFIG_LV_USE_FONT_COMPRESSED=y` 并跑字体 source test。

## 未验证风险

- 当前已完成构建级验证，仍建议烧录后打开危险识别页，重点确认 `安全监听`、`未开启` 等中文 label 不再触发 panic。
