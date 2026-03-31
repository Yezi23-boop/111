---
id: ai-font-assets
tags: [project, ui, lvgl, fonts, assets, official-chat]
summary: 记录 AI 页面 hand-written 字体资源链，当前已接好接缝与回退层，但 assets 真正字体解析尚未完成。
last_reviewed: 2026-03-31
---

# AI 页面字体资源链

- 仅服务 `main/ui/custom` 下的 hand-written AI 页面。
- 当前实现是“接缝与回退层已落地，assets 真正字体解析尚未完成”。
- `ui_font_assets` 已接入 AI 页面调用点并探测 `assets` 分区，但目前仍回退到编译字体。
- 现阶段可稳定使用的回退字体：
  - `lv_font_SourceHanSerifSC_Regular_22`
  - `lv_font_montserratMedium_16`
- GUI Guider 生成页面的字体链保持原样，不在本轮重构范围内。

# 关键约定

- AI 页面只通过 `ui_font_assets_*()` 取字体，不直接绑定具体字体对象。
- `ui_font_assets_init()` 目前只做分区探测与状态占位，不代表 assets 字体已可解析。
- 后续若接入真正的 assets 字体解析，应先保持同一组调用点不变，再替换底层实现。
