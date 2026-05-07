# 字体

这里放可选的 LVGL 原生 binfont 文件，会随 `resources` LittleFS 分区打包进
`resources.bin`。当前中文 UI 默认主路径是编译进固件的 C 字体，这里的 `.bin`
保留给后续运行时可替换字体方案。

当前预置《通用规范汉字表》一级 3500 字的 `16 / 22 / 24 / 27` 四个字号：

```c
lv_font_t *font = lv_binfont_create("A:/fonts/lvgl_montserrat_lxgw_tghz_level1_3500_24_4.bin");
```

中文 UI 不要直接给中文 label 使用 `lv_font_montserrat*`；大字号中文标题优先用
页面实际文案生成子集字体，避免 3500 字全量字体过大。
