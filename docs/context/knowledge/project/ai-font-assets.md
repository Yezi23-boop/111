---
id: ai-font-assets
tags: [project, ui, lvgl, fonts, assets, official-chat]
summary: AI 与 Hermes 动态文本统一使用官方 xiaozhi-fonts 2.0.0 的 Noto common CBin，存放于 assets raw 分区并通过一次 mmap 直读 Flash。
last_reviewed: 2026-08-05
memory_type: semantic
scope: repo
owners: main/ui/custom/ui_font_assets.c, main/ui/custom/cbin_font_bridge.c, scripts/build_ai_font_assets.py
triggers: ai, hermes, font, assets
evidence_level: observed
status: active
---

# AI/Hermes 动态字体资源链

- 只服务 `main/ui/custom` 中 AI 与 Hermes 的动态文本；普通 UI 中文字体仍由 `ui_chinese_fonts.h` 的编译期字体负责。
- `main/idf_component.yml` 固定 `78/xiaozhi-fonts: 2.0.0` 与 `lvgl/lvgl: ~9.5.0`；本机解析结果由 `dependencies.lock` 固定为 `78/xiaozhi-fonts 2.0.0`、`lvgl/lvgl 9.5.0`。
- 官方组件的 `manifest.json` 校验 `Noto / noto-v1 / DeepSeek-V4-Flash / core_vocab_only`，`charsets/common.json` 校验 common 字符集；构建不会从仓库复制第二份字体。
- `scripts/build_ai_font_assets.py` 从已解析的 `managed_components/78__xiaozhi-fonts/cbin/` 读取两个官方 CBin，确定性生成 `assets` raw 镜像：
  - `font_noto_sans_common_20_4.bin`：AI `text_font`，20px/4bpp。
  - `font_noto_sans_common_16_4.bin`：Hermes `hermes_text_font`，16px/4bpp。
- 根 `CMakeLists.txt` 在构建期校验并生成镜像，再通过 `esptool_py_flash_to_partition(flash "assets" ...)` 写入 `assets` 分区；`resources` 只生成普通 LittleFS 资源。
- `ui_font_assets` 读取 raw 表、校验 checksum、`ZZ` CBin 标识和 `index.json` 的 bundle/charset/size/bpp 元数据后，对实际镜像长度执行一次 `esp_partition_mmap()`。CBin 位图留在 Flash，运行时只分配字体描述对象和 cmap 元数据。
- AI 与 Hermes 共享 `noto-v1` 和同一映射，但保留两个独立 `lv_font_t`，因为字号和字形位图不同。
- AI 不使用也不链接 `font_noto_sans_basic_20_4`；动态文本不使用 LittleFS、字体 fallback 或 glyph push。
- assets 缺失、损坏、元数据不匹配或 mmap 失败时，两个页面均停止动态文本渲染，显示固定 ASCII `FONT ASSET ERROR`，不回退到其他中文字体。

# 关键约定

- AI 页面只通过 `ui_font_assets_text()` 取 20px 字体；Hermes 页面只通过 `ui_font_assets_hermes()` 取 16px 字体。
- `ui_font_assets_init()` 是唯一的 raw 分区加载入口；失败返回错误码，`ui_font_assets_ready()` 为 false，字体 getter 返回 `NULL`。
- `cbin_font_bridge_create()` / `cbin_font_bridge_destroy()` 只管理由 CBin 描述数据创建的 LVGL 对象，不复制 Flash 中的点阵位图。
- `ui_font_assets_icon()` 只返回编译期 Montserrat 图标字体，不属于动态中文文本链。

# 分区与验证

- 当前分区固定为 `assets 0x1820000/3M`、`resources 0x1B20000/3M`、`model 0x1E20000/0x1E0000`；双 OTA 槽保持各 `12M`。
- `build-font-migration/flasher_args.json` 已同时包含 `partition_table`、`assets`、`resources` 和 `model` 烧录项；分区变化后必须使用完整 `idf.py flash`，不能只使用 `app-flash`。
- 双字体 raw 镜像在当前官方组件版本下约 `2.17 MiB`，小于 `3 MiB assets` 分区；`resources.bin` 按 `3 MiB` 生成。
- 独立全量构建已通过，应用镜像约 `10.64 MiB`，小于 `12 MiB` OTA 槽；map 未包含 `font_noto_sans_basic_20_4`。
- 2026-08-05 COM7 真机已完成完整分区刷写；`app-flash-monitor` 观察到 `LVGL 9.5`、`Noto font assets ready: bundle=noto-v1 text=common20 hermes=common16 image=2173924 pages=34`、首帧和 `panic_log_seen=0`。剩余验收只包括实际打开 AI/Hermes 页面输入长中文，以及未同步 assets 的专用故障镜像。

# 边界

- `main/ui/custom/fonts/*.c` 面向普通 LVGL UI，不替代 AI/Hermes raw Noto 字体。
- `resources` LittleFS 面向天气图片和普通资源；不保存 AI/Hermes 字体副本。
- `official_chat` 当前激活任务明确跳过旧的 OTA/assets 下载路径；若未来重新启用，必须先改为不会整分区覆盖 Noto raw 镜像的独立协议或分区。
- 不新增字体 fallback 链、LittleFS 动态字体加载或 glyph push，除非产品边界明确改变。
