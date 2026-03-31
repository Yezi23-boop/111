---
id: ai-font-assets
tags: [project, ui, lvgl, fonts, assets, official-chat]
summary: 记录 AI 页面 hand-written 字体资源链，当前已通过 mmap + index.json 解析 assets.bin，并保留编译字体回退。
last_reviewed: 2026-04-01
---

# AI 页面字体资源链

- 仅服务 `main/ui/custom` 下的 hand-written AI 页面。
- 当前实现已打通 `assets` 分区 `mmap`、`index.json` 解析和运行时字体缓存。
- `ui_font_assets` 成功时返回 `ESP_OK`，失败时明确记录日志并回退到编译字体。
- 现阶段可稳定使用的回退字体：
  - `lv_font_SourceHanSerifSC_Regular_22`
  - `lv_font_montserratMedium_16`
- GUI Guider 生成页面的字体链保持原样，不在本轮重构范围内。

# 关键约定

- AI 页面只通过 `ui_font_assets_*()` 取字体，不直接绑定具体字体对象。
- `ui_font_assets_init()` 会尝试解析 `assets.bin`，并从 `index.json` 读取 `text_font` 与 `icon_font`。
- 当前 `text_font` 是标题、正文和元信息的主字体来源，`icon_font` 作为独立缓存保留，缺失时继续用编译字体兜底。
- 运行时字体桥接通过本地 vendored 的 `78__xiaozhi_fonts` 组件提供的 `cbin_font_create` 完成。
