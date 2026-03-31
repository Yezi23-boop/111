# AI 字体资产链与 AI 页面重构设计

## 问题重述

当前仓库 `D:\esp32S3\111` 已经有两条 hand-written AI 页面路径：

- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
- `D:\esp32S3\111\main\ai_experiment_ui.c`

它们都已经能显示中文和驱动 `official_chat_service`，但仍存在两个明显问题：

1. 字体链路还只是“接缝 + 回退”状态  
   `D:\esp32S3\111\main\ui\custom\ui_font_assets.c` 当前只探测 `assets` 分区，并继续回退到编译字体，尚未真正按 `xiaozhi-esp32` 的资产格式解析运行时字体。

2. 两套 AI 页的视觉层级仍偏工程验证页，不像 `xiaozhi-esp32` 的 AI-first 页面  
   当前页面更偏“状态说明 + 大按钮”，而不是“状态主卡片 + 最近一轮对话反馈”的结构。

用户已明确约束：

- 保留“手表主菜单 + 多应用”结构
- AI 页面要更像 `D:\xiaozhiai\xiaozhi-esp32`
- 主视觉使用静态 AI 图标卡片
- 字体资产链直接复用 `xiaozhi-esp32` 的实际格式与 `index.json` 约定
- 字体资产放在 `assets` 分区
- 不只改 `ai_ui_controller`，还要顺手把 `ai_experiment_ui` 一起切过去

因此，这一轮要同时解决两件事：

- 把 `xiaozhi-esp32` 的实际字体资产读取/解析链迁进 `ui_font_assets`
- 把正式 AI 页面和独立实验页统一改成 `B. 对话优先页 + 静态 AI 图标卡片`

## 目标

本轮目标：

1. 让 `ui_font_assets` 真正按 `xiaozhi-esp32` 方式从 `assets` 分区读取字体资产
2. 迁入 `xiaozhi-esp32` 的字体资产文件格式与 `index.json` 约定
3. 让 `ai_ui_controller` 和 `ai_experiment_ui` 都改为通过 `ui_font_assets_*()` 使用运行时字体
4. 让两套 AI 页统一为“对话优先 + 静态 AI 图标卡片”的页面骨架
5. 继续保留失败回退链，确保字体资产加载失败时页面仍可显示

验收目标：

- 启动日志能明确看到 `assets` 分区、资源校验和字体解析结果
- `ui_font_assets_ready()` 仅在字体资产真正可用时返回 `true`
- `ai_ui_controller` 与 `ai_experiment_ui` 的中文文本统一走字体资源层
- 两套 AI 页视觉结构接近 `xiaozhi-esp32` 的 AI-first 页面
- 字体资产缺失或解析失败时，页面仍可用编译字体显示中文

## 非目标

本轮不做：

- 改造 `main/ui/generated` 下的 GUI Guider 生成页面
- 一次性迁入 `xiaozhi-esp32` 的完整 `Display` / `LvglDisplay` 架构
- 长聊天历史、复杂消息气泡流、键盘输入
- 动态波纹球或复杂动画系统
- 一次性重做整套主题系统

## 现状与证据

### 当前字体资源层状态

来自：

- `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`
- `D:\esp32S3\111\main\ui\custom\ui_font_assets.h`

已知事实：

- 当前只做 `assets` 分区探测
- `ui_font_assets_init()` 返回 `ESP_ERR_NOT_SUPPORTED`
- `ui_font_assets_ready()` 返回 `false`
- `title/body/meta` 仍回退到：
  - `lv_font_SourceHanSerifSC_Regular_22`
  - `lv_font_montserratMedium_16`

这说明：

- 字体调用接缝已经稳定
- 但真正的运行时字体资产链尚未落地

### `xiaozhi-esp32` 的参考链路

来自：

- `D:\xiaozhiai\xiaozhi-esp32\main\assets.cc`
- `D:\xiaozhiai\xiaozhi-esp32\main\CMakeLists.txt`

已知关键行为：

- 通过 `assets` 分区 mmap 整个资源区
- 读取头部文件表与校验信息
- 从 `index.json` 读取 `text_font` 等资源键
- 调用 `LvglCBinFont` 从资源里构造 LVGL 可用字体对象

这不是“页面自己找字体”，而是“资源层统一读取、页面只拿句柄”。

### 当前两套 AI 页的结构问题

来自：

- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
- `D:\esp32S3\111\main\ai_experiment_ui.c`

当前问题：

- 以状态说明和按钮为主，不是 AI-first 页面
- 最近一轮用户话 / 助手话没有视觉中心位置
- 正式页与实验页布局分叉明显
- `ai_experiment_ui` 仍直接绑定具体编译字体对象

## 方案比较

### 方案 A：只迁字体资产链，不改页面布局

优点：

- 范围小
- 容易验证

缺点：

- 页面仍然不像 `xiaozhi-esp32`
- 两套 AI 页的视觉差异继续存在

结论：不采用

### 方案 B：只重做页面布局，字体资产链继续先回退

优点：

- 页面效果更快接近目标

缺点：

- 字体链路仍是半成品
- 后面页面排版和字体适配还要再返工一次

结论：不采用

### 方案 C：先把字体资产链落地，再同时统一两套 AI 页布局

优点：

- 先把文本资源底座做稳
- 正式页和实验页一起对齐设计语言
- 最接近用户已确认的路线

缺点：

- 这轮改动会同时覆盖资源层和页面层

结论：采用

## 选定方案

采用方案 C：

- 直接迁入 `xiaozhi-esp32` 的字体资产格式与 `index.json` 约定
- `ui_font_assets` 成为 hand-written 页面统一字体资源层
- `ai_ui_controller` 与 `ai_experiment_ui` 同时切到这条字体链
- 两套 AI 页同时统一成“对话优先 + 静态 AI 图标卡片”

## 设计

### 1. 字体资产链设计

继续使用：

- `D:\esp32S3\111\main\ui\custom\ui_font_assets.h`
- `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`

但实现从“只探测分区”升级为：

1. 查找 `assets` 分区
2. mmap 整个 `assets` 分区
3. 解析 `xiaozhi-esp32` 的资源表格式
4. 校验资源表长度与 checksum
5. 读取 `index.json`
6. 从 `index.json` 中获取：
   - `text_font`
   - `icon_font`（如果资源中存在）
7. 构造运行时字体对象
8. 缓存 `title/body/meta/icon` 四类字体句柄

接口保持不变：

- `ui_font_assets_init()`
- `ui_font_assets_ready()`
- `ui_font_assets_title()`
- `ui_font_assets_body()`
- `ui_font_assets_meta()`
- `ui_font_assets_icon()`

这样页面调用点不需要再改第二次。

### 2. 资源格式与索引约定

本轮不发明新格式，直接对齐 `xiaozhi-esp32`：

- 继续使用 `assets` 分区
- 继续使用资源表 + `index.json`
- `index.json` 里至少约定：
  - `version`
  - `text_font`
  - `icon_font`（若当前资源包具备）

本仓库后续 hand-written 页面若要继续资产化，也优先沿用这套约定。

### 3. 失败回退设计

必须保留两级回退：

一级回退：

- `assets` 分区不存在
- 资源表校验失败
- `index.json` 缺失或非法
- 字体解析失败

此时 `ui_font_assets_ready()` 返回 `false`，并回退到编译字体。

二级回退字体：

- 中文标题/正文：
  - `lv_font_SourceHanSerifSC_Regular_22`
- 元信息小字：
  - `lv_font_montserratMedium_16`

这样即使资产字体链失败，AI 页面和实验页也不会白屏或乱码。

### 4. 正式 AI 页面布局

文件：

- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`

页面改成 4 层：

1. 顶部轻状态栏
   - 左：`小智`
   - 右：联网状态短词

2. 中部主卡片
   - 静态 AI 图标卡片
   - 大状态字：`待唤醒 / 聆听中 / 回答中 / 未联网`
   - 一句简短提示

3. 下部双卡片
   - `你刚刚说`
   - `小智回答`

4. 底部操作区
   - `进入配网` / `网络设置`
   - `返回主页`

设计重点：

- 页面主视觉从“说明文字”切换为“AI 当前状态 + 最近一轮对话”
- 所有中文文本统一从 `ui_font_assets` 取字体

### 5. 独立实验页布局

文件：

- `D:\esp32S3\111\main\ai_experiment_ui.c`

与正式页统一同一套视觉骨架：

- 顶部状态栏
- 中部静态 AI 图标卡片
- 最近一轮对话反馈区
- 底部操作按钮

差异只保留在行为边界：

- 独立实验页不依赖主菜单返回链
- 正式页保留返回主页按钮

这样后续你调视觉、状态映射、字体和间距时，不需要维护两套风格。

### 6. 最近一轮对话反馈

两套页面都补这两块状态：

- `最近一句用户话`
- `最近一句助手话`

数据来源继续依赖 `official_chat_service`，由服务层缓存最近一轮文本，再提供只读 getter 给页面。

本轮只要求：

- 显示最后一轮
- 文本可换行
- 超长文本做截断或限高

不做长历史滚动区。

## 文件边界

### 重点修改

- `D:\esp32S3\111\main\ui\custom\ui_font_assets.h`
- `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`
- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
- `D:\esp32S3\111\main\ai_experiment_ui.c`
- `D:\esp32S3\111\main\official_chat_service.h`
- `D:\esp32S3\111\main\official_chat_service.c`
- `D:\esp32S3\111\main\CMakeLists.txt`

### 可能新增

- hand-written 页共用的小型 view helper
- AI 图标卡片布局辅助函数

### 明确不改

- `D:\esp32S3\111\main\ui\generated\*`
- `D:\esp32S3\111\main\ui\generated\guider_fonts\*`
- `D:\esp32S3\111\main\ui\generated\gui_guider.h`

## 验证计划

### 源码级验证

- `ui_font_assets` 不再只返回 `ESP_ERR_NOT_SUPPORTED`
- `ui_font_assets_ready()` 仅在真实字体可用时返回 `true`
- `ai_ui_controller` 和 `ai_experiment_ui` 都改为只通过 `ui_font_assets_*()` 取字体
- 两套页面都出现“你刚刚说 / 小智回答”布局节点

### 构建验证

- `uv run python -m unittest ...` 相关源码测试通过
- `. "$env:IDF_PATH\\export.ps1"; idf.py build` 通过

### 真机验证

1. `assets` 分区可被发现
2. 启动日志可看到字体资源初始化成功或明确回退原因
3. 正式 AI 页中文显示正常
4. 实验页中文显示正常
5. 两套页面状态变化时不乱码、不缺字、不崩溃

## 风险与回滚

### 风险

- `xiaozhi-esp32` 的字体资产依赖当前仓库尚未引入的运行时对象
- 字体资源体积可能增加 `assets` 分区占用
- 小屏中文在换成新字体后，行高和留白可能需要再调
- 如果最近一轮对话文本直接塞进页面，超长文本会挤压布局

### 回滚

- 保留 `ui_font_assets` 的编译字体回退路径
- 即使运行时字体解析失败，页面仍走当前可用字体
- 页面布局改造限定在 hand-written 页面，不影响 GUI Guider 页面

## 推荐实施顺序

1. 先迁字体资产读取/解析链
2. 再给 `official_chat_service` 补最近一轮文本缓存
3. 然后重做 `ai_ui_controller`
4. 最后同步 `ai_experiment_ui`

这样能先把底座做稳，再做页面层。
