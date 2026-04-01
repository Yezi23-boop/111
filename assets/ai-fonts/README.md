# AI Fonts Assets

- 本目录用于生成与 `xiaozhi-esp32` 兼容的 `assets` 分区字体资源。
- 第一版只先内置 AI 页面需要的中文文本字体，`index.json` 里保留 `version` 与 `text_font`。
- `icon_font` 目前仍允许走运行时回退，不强制要求当前资源包提供。
- 字体二进制文件直接复用 `xiaozhi-esp32` 的 `cbin` 资源格式，不再经过 GUI Guider 生成链。
- `build_ai_font_assets.py` 会按 `esp_mmap_assets` 的 header/table/data 格式生成 `assets.bin`。
