# LVGL 中文字体源

这里保存生成中文 LVGL 字体所需的源字体和字符集，目的是让字体产物可复现。

- 拉丁字体：`fonts/montserratMedium.ttf`
- 中文字体：`fonts/LXGWWenKai-Regular.ttf`
- 默认字符集：`charsets/charset_tghz_level1_3500.txt`
- 字号预置：`16 / 22 / 24 / 27`
- 默认编译期字体脚本：`scripts/lvgl_fonts/ensure_lvgl_compiled_fonts.py`
- 可选 binfont 脚本：`scripts/lvgl_fonts/ensure_lvgl_chinese_fonts.py`

字体源只用于生成，不会随 `resources.bin` 烧录。当前默认中文 UI 主路径消费
`main/ui/custom/fonts/` 下已提交的 LVGL C 字体；`resources/fonts/` 下的 `.bin`
保留给后续运行时可替换字体方案。
