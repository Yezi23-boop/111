# 上下文包

- 生成时间(UTC): 2026-04-02T12:02:28.036645+00:00
- 查询: BLE 配网 network_service AP fallback
- 范围: mixed
- 包含导航文档: False

## 命中文档

1. `docs/context/knowledge/project/ble-provisioning-wechat-feasibility.md` (score=76)
   - 标题: BLE 配网与微信小程序可行性
   - 标签: project, wifi, provisioning, ble, wechat, esp32-s3
   - 摘要: 当前仓库新增 BLE 配网时，推荐先走“自定义 BLE GATT 主路径 + 现有 AP 网页兜底 + 先固件后小程序”的最小闭环方案。
2. `docs/context/knowledge/project/storage-and-provisioning-paths.md` (score=39)
   - 标题: 存储与配网路径
   - 标签: project, storage, sd, spiffs, wifi, provisioning, html
   - 摘要: 当前仓库的存储路径、SD 总线选择和 AP 配网页面嵌入方式摘要。
3. `docs/context/knowledge/project/display-render-touch-transfer-pipeline.md` (score=31)
   - 标题: 显示渲染、传输与触摸输入链路
   - 标签: project, lvgl, display, touch, co5300, ft5x06, qspi, dma, rendering
   - 摘要: 记录当前仓库从 LVGL 渲染、CO5300 QSPI 传输到 FT5x06 触摸输入的完整链路，以及与 DMA 内存压力相关的典型故障模式。

## 可直接粘贴给 Codex 的上下文

### 来源: docs/context/knowledge/project/ble-provisioning-wechat-feasibility.md
- 相关分数: 76
- 关键片段(L66): 3. 调整 `network_service`，无凭据时默认启动 BLE 配网
- 摘要: 当前仓库新增 BLE 配网时，推荐先走“自定义 BLE GATT 主路径 + 现有 AP 网页兜底 + 先固件后小程序”的最小闭环方案。

### 来源: docs/context/knowledge/project/storage-and-provisioning-paths.md
- 相关分数: 39
- 关键片段(L34): - `wifi_provision_start_apcfg()` 会启动 AP 和 WebSocket 服务器，把这份 HTML 作为配网页面提供给用户
- 摘要: 当前仓库的存储路径、SD 总线选择和 AP 配网页面嵌入方式摘要。

### 来源: docs/context/knowledge/project/display-render-touch-transfer-pipeline.md
- 相关分数: 31
- 关键片段(L290): - `LVGL` 文档在 `lv_display_set_color_format()` 说明里明确提到：若屏幕是 `RGB565` 且需要交换高低字节，应在 `flush_cb` 中调用 `lv_draw_sw_rgb565_swap()`。
- 摘要: 记录当前仓库从 LVGL 渲染、CO5300 QSPI 传输到 FT5x06 触摸输入的完整链路，以及与 DMA 内存压力相关的典型故障模式。


> 已打包片段数: 3/3，片段字符预算: 4000