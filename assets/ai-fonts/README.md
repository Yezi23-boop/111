# AI/Hermes Noto raw 字体资产

本目录只保存 raw assets 的索引，不保存字体 `.bin` 副本。字体由已解析的
`managed_components/78__xiaozhi-fonts` 提供，构建脚本会校验官方 manifest、
DeepSeek-V4-Flash common 字符集和两个字号 profile 后确定性打包：

- `font_noto_sans_common_20_4.bin`：AI `text_font`，20px / 4bpp。
- `font_noto_sans_common_16_4.bin`：Hermes `hermes_text_font`，16px / 4bpp。

`assets/ai-fonts/index.json` 显式记录 `noto-v1` bundle、`common` charset、
字号和 BPP。根构建流程生成的镜像写入 `assets` raw 分区；固件通过一次
`esp_partition_mmap()` 映射，CBin 位图留在 Flash，两个字体对象只保留解析元数据。

AI/Hermes 不从 LittleFS 加载字体，也不使用字体 fallback、glyph push 或内置
Noto basic20。资源缺失、损坏或元数据不匹配时，页面显示固定 ASCII
`FONT ASSET ERROR`。
