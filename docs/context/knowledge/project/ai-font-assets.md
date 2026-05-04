---
id: ai-font-assets
tags: [project, ui, lvgl, fonts, assets, official-chat]
summary: 记录 AI 页面 hand-written 字体资源链，当前已在 LVGL 9.3.0 下重新回到运行时资产主路径，并保留编译字体回退。
last_reviewed: 2026-04-17
memory_type: semantic
scope: repo
owners: main/ui/custom/ui_font_assets.c, main/ui/custom/cbin_font_bridge.c, main/ui/generated
triggers: ai, font, assets
evidence_level: observed
---

# AI 页面字体资源链

- 仅服务 `main/ui/custom` 下的 hand-written AI 页面。
- 当前实现已打通 `assets` 分区 `mmap`、`index.json` 解析和运行时字体缓存。
- 当前构建链已接通本地 `assets/ai-fonts` 目录、`scripts/build_ai_font_assets.py` 打包脚本，以及根 `CMakeLists.txt` 中的 `esptool_py_flash_to_partition(flash "assets" ...)`。
- `ui_font_assets` 成功时返回 `ESP_OK`，失败时明确记录日志并回退到编译字体。
- `assets` 与 `model` 分区都依赖运行时 `esp_partition_mmap()`，因此需要保持在 16MB 以下；`audio` 可放到更高地址。
- 当前仓库运行时 `lvgl/lvgl` 已升级到 `9.3.0`，并与 `xiaozhi-esp32` 风格的 `cbin` 字体资产链回到同一版本方向。
- 之前为 `LVGL 9.2.2` 临时加的“主动短路回退”保护已移除；现在 `ui_font_assets` 会优先尝试运行时资产字体，失败时再回退到编译字体。
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
- 当前主路径已经是：
  - `assets` 分区
  - `mmap + index.json`
  - `cbin_font_bridge_create()`
  - AI 页面通过 `ui_font_assets_*()` 取字体
- 若未来再次更换 GUI Guider 导出层或升级 LVGL 小版本，优先复核 `gui_guider.h` 中的字体声明是否完整，避免 generated 字体文件已存在但头文件未暴露给 hand-written 层。

# 当前状态

- `build/flasher_args.json` 已包含 `assets` 分区烧录项，说明字体资产镜像会随 `idf.py flash` 一起写入。
- 当前分区布局已调整为 `factory 8M -> assets 2M -> model 4M -> audio 7M`，用于避免 `assets/model` 被放到 16MB 以上后出现 `mmap` 读取异常。
- 当前 `dependencies.lock` 已锁到 `lvgl/lvgl 9.3.0`。
- 当前正式 AI 页面 `main/ui/custom/ai_ui_controller.c` 已切到统一的 `ui_font_assets` 入口。
- 历史上的独立实验页 `main/ai_experiment_ui.c` 已从仓库删除，不应再作为当前代码入口理解。
- 当前页面已统一为“顶部状态条 + 静态 AI 图标卡片 + 最近一轮对话卡片”的 hand-written 骨架。
- `ui_font_assets` 当前已移除 `LVGL < 9.3.0` 的强制短路分支，源码测试改为检查“不再保留这条短路文案”。
- `official_chat_service` 现已缓存最近一轮 `stt` 用户文本和 `tts sentence_start` 助手文本，两个 AI 页面会优先显示真实最近一轮内容，取不到时才回退到状态提示文案。
