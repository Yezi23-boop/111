---
id: ai-font-assets
tags: [project, ui, lvgl, fonts, assets, official-chat]
summary: 记录 AI 页面 hand-written 字体资源链，当前已通过 mmap + index.json 解析 assets.bin，并保留编译字体回退。
last_reviewed: 2026-04-01
---

# AI 页面字体资源链

- 仅服务 `main/ui/custom` 下的 hand-written AI 页面。
- 当前实现已打通 `assets` 分区 `mmap`、`index.json` 解析和运行时字体缓存。
- 当前构建链已接通本地 `assets/ai-fonts` 目录、`scripts/build_ai_font_assets.py` 打包脚本，以及根 `CMakeLists.txt` 中的 `esptool_py_flash_to_partition(flash "assets" ...)`。
- `ui_font_assets` 成功时返回 `ESP_OK`，失败时明确记录日志并回退到编译字体。
- `assets` 与 `model` 分区都依赖运行时 `esp_partition_mmap()`，因此需要保持在 16MB 以下；`audio` 可放到更高地址。
- 当前仓库运行时 `lvgl/lvgl` 版本为 `9.2.2`，而复用的 `xiaozhi-esp32` `cbin` 字体资产链面向 `LVGL >= 9.3.0`。因此在当前版本下 `ui_font_assets` 会主动短路回退到编译中文字库，避免“资产可读但中文仍显示方框”。
- 现阶段可稳定使用的回退字体：
  - `lv_font_SourceHanSerifSC_Regular_22`
  - `lv_font_montserratMedium_16`
- GUI Guider 生成页面的字体链保持原样，不在本轮重构范围内。

# 关键约定

- AI 页面只通过 `ui_font_assets_*()` 取字体，不直接绑定具体字体对象。
- `ui_font_assets_init()` 会尝试解析 `assets.bin`，并从 `index.json` 读取 `text_font` 与 `icon_font`。
- 当前 `text_font` 是标题、正文和元信息的主字体来源，`icon_font` 作为独立缓存保留，缺失时继续用编译字体兜底。
- 运行时字体桥接通过本地 vendored 的 `78__xiaozhi_fonts` 组件提供的 `cbin_font_create` 完成。
- 第一版资产内容直接复用 `xiaozhi-esp32` 的 `font_puhui_common_20_4.bin`，并沿用其 `index.json` 约定。
- 若未来要真正启用运行时 `cbin` 字体，优先方案是升级仓库 LVGL 到 `9.3+` 或重新生成兼容 `9.2.2` 结构布局的字体资产，而不是继续在 `9.2.2` 上强行使用现成 `cbin` 包。

# 当前状态

- `build/flasher_args.json` 已包含 `assets` 分区烧录项，说明字体资产镜像会随 `idf.py flash` 一起写入。
- 当前分区布局已调整为 `factory 8M -> assets 2M -> model 4M -> audio 7M`，用于避免 `assets/model` 被放到 16MB 以上后出现 `mmap` 读取异常。
- 正式 AI 页面 `main/ui/custom/ai_ui_controller.c` 与实验页 `main/ai_experiment_ui.c` 都已切到统一的 `ui_font_assets` 入口。
- 当前页面已统一为“顶部状态条 + 静态 AI 图标卡片 + 最近一轮对话卡片”的 hand-written 骨架。
- `official_chat_service` 现已缓存最近一轮 `stt` 用户文本和 `tts sentence_start` 助手文本，两个 AI 页面会优先显示真实最近一轮内容，取不到时才回退到状态提示文案。
