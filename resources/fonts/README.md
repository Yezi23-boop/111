# 字体

这里放可选的 LVGL 原生 binfont 文件，会随 `resources` LittleFS 分区打包进
`resources.bin`。当前中文 UI 默认主路径是编译进固件的 C 字体，因此目录默认
不预置 `.bin` 文件，避免无使用价值的字体占用资源分区。

需要验证运行时可替换字体方案时，再手动运行
`uv run python scripts/lvgl_fonts/ensure_lvgl_chinese_fonts.py` 生成预置字体。

AI 和 Hermes 动态中文不再使用本目录字体。两套 Noto common CBin 字体由
`78/xiaozhi-fonts 2.0.0` 提供，构建时打包到 `assets` raw 分区并通过 mmap 读取。
本目录不保留 Hermes 字体副本，避免 LittleFS 与 assets 出现两份来源。

生成后的通用字体使用 `charset_tghz_common_5500.txt` 的 `16 / 22` 两个字号：

```c
lv_font_t *font = lv_binfont_create("A:/fonts/lvgl_montserrat_lxgw_common_5500_22_4.bin");
```

中文 UI 不要直接给中文 label 使用 `lv_font_montserrat*`；大字号中文标题优先用
页面实际文案生成子集字体，避免 3500 字全量字体过大。
