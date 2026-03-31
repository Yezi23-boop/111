# AI Font Assets Design

## 问题重述

当前仓库 `D:\esp32S3\111` 的中文显示能力主要来自 GUI Guider 导出的编译期字体：

- `D:\esp32S3\111\main\ui\generated\guider_fonts\lv_font_SourceHanSerifSC_Regular_22.c`
- `D:\esp32S3\111\main\ui\generated\gui_guider.h`

这条路径能支撑现有生成页面，但不适合后续 AI 页面和 hand-written 页面长期演进，原因有三点：

1. hand-written 页面目前直接引用具体字体对象，页面代码和字体来源耦合
2. GUI Guider 生成层不是稳定的长期资源层，后续重导页面有覆盖风险
3. 用户明确要求 AI 页面字体链路尽量贴近 `D:\xiaozhiai\xiaozhi-esp32`，采用“显示层统一字体资源，从资产加载”的做法

同时，用户已确认：

- 当前项目与 `D:\xiaozhiai\xiaozhi-esp32\main\boards\waveshare\esp32-s3-touch-amoled-2.06` 硬件一致
- 这次只先服务 AI 页面和后续 hand-written 页面
- 不要求一步改动 GUI Guider 生成页面
- 字体资源希望放在 `assets` 分区里，尽量贴近 `xiaozhi-esp32`

因此，这一轮需要设计一条新的字体资源链：

- hand-written 页面不再直接依赖 GUI Guider 编译字体
- 字体优先从 `assets` 分区加载
- AI 页面通过统一字体资源接口取字体
- 加载失败时可安全回退到当前仓库现有编译字体

## 目标

本轮目标是为 AI 页面和后续 hand-written 页面建立“字体资产化基础设施”：

1. 引入一层 hand-written 页面专用的字体资源层
2. 字体资源优先从 `assets` 分区加载
3. AI 页面只依赖统一字体资源接口，不直接写具体 `lv_font_t`
4. 失败时自动回退到当前仓库现有中文编译字体
5. 不破坏现有 GUI Guider 页面

验收目标：

- `assets` 分区中的字体资源可被发现和加载
- AI 页面中文文本通过统一字体资源层渲染
- 字体加载失败时，AI 页面仍能用现有编译字体显示中文
- GUI Guider 生成页面继续按原路径工作

## 非目标

本轮明确不做：

- 重写整个显示系统为 `xiaozhi-esp32` 的 `Display` / `LvglDisplay` 架构
- 改造 `main/ui/generated` 下的 GUI Guider 页面
- 全量替换当前仓库所有页面字体来源
- 一次引入完整主题系统、换肤系统或多语言系统
- 一次做完整字号家族和复杂动态排版

## 现状与证据

### 当前仓库中文显示路径

来自：

- `D:\esp32S3\111\main\ui\generated\gui_guider.h`
- `D:\esp32S3\111\main\ui\generated\guider_fonts\lv_font_SourceHanSerifSC_Regular_22.c`
- `D:\esp32S3\111\main\ui\generated\setup_scr_screen_wallpaper.c`

已知事实：

- `gui_guider.h` 中声明了 `LV_FONT_DECLARE(lv_font_SourceHanSerifSC_Regular_22)`
- `setup_scr_screen_wallpaper.c` 用 `lv_font_SourceHanSerifSC_Regular_22` 渲染中文 `"小祁满"`
- `lv_font_SourceHanSerifSC_Regular_22.c` 的字符映射包含中文范围，而不仅是 ASCII

这说明：

- 当前仓库能显示中文，不是 LVGL 默认支持，而是依赖预编译字库
- 这套字库可以作为回退路径，但不应继续承担 hand-written 页面长期资源层角色

### 当前 hand-written AI 页面的问题

来自：

- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`

已知事实：

- 页面中已有中文字符串，例如：
  - `"小智"`
  - `"未联网"`
  - `"进入配网"`
  - `"返回主页"`
- 但部分中文文本被挂在 `montserrat` 字体上，而不是中文字体

这会带来两个风险：

1. 中文显示依赖当前资源偶然性，不是清晰的字体策略
2. 页面布局调整后，字体来源问题会成为新的不稳定因素

### 参考仓库字体链路

来自：

- `D:\xiaozhiai\xiaozhi-esp32\main\assets.cc`
- `D:\xiaozhiai\xiaozhi-esp32\main\CMakeLists.txt`
- `D:\xiaozhiai\xiaozhi-esp32\main\display\display.h`

参考仓库的核心思路不是“每页手动指定字体”，而是：

1. 字体作为资源资产存在
2. 由资源层加载
3. 显示层统一拿字体对象
4. 页面和应用层只表达“需要哪类文本样式”

用户希望本仓库的 AI 页面尽量贴近这条路线。

## 方案比较

### 方案 A：继续复用 GUI Guider 编译字体，只加一个 helper

优点：

- 改动小
- 上手快

缺点：

- 不是资产化字体链
- 不能真正贴近 `xiaozhi-esp32`
- 仍然延续“页面直接碰字体对象”的思路

结论：不采用

### 方案 B：完整迁入 `xiaozhi-esp32` 的显示系统

优点：

- 与参考仓库最像

缺点：

- 范围远超 AI 页面字体问题
- 会撞上当前 GUI Guider + hand-written bridge 结构
- 难以最小可运行、最小回退

结论：不采用

### 方案 C：仅为 hand-written 页面引入资产化字体资源层

优点：

- 贴近 `xiaozhi-esp32` 的资源理念
- 只影响 AI 页面和后续 hand-written 页面
- GUI Guider 页面可保持稳定
- 支持逐步演进到更完整的显示资源层

缺点：

- 需要补一层字体资产加载和失败回退逻辑

结论：采用

## 选定方案

采用方案 C：

- 为 hand-written 页面新增字体资源层
- 字体优先从 `assets` 分区加载
- AI 页面使用统一字体资源接口
- GUI Guider 页面不改
- 失败时回退到现有编译字体

这是一条“先对齐资源层理念，再决定是否继续对齐显示层结构”的路线。

## 设计

### 1. 新增 hand-written 页面字体资源层

建议新增：

- `D:\esp32S3\111\main\ui\custom\ui_font_assets.h`
- `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`

职责：

1. 初始化字体资产系统
2. 从 `assets` 分区查找字体资源
3. 解析并保存 hand-written 页面使用的字体句柄
4. 暴露稳定接口给 AI 页面
5. 在失败时提供回退字体

建议对外暴露的最小接口：

- `ui_font_assets_init()`
- `ui_font_assets_ready()`
- `ui_font_assets_title()`
- `ui_font_assets_body()`
- `ui_font_assets_meta()`
- `ui_font_assets_icon()`

第一版只要求支持 AI 页面需要的 3 类文本：

- 标题字体
- 正文字体
- 元信息/小字字体

### 2. 字体资源来源：`assets` 分区

字体资产第一版放在：

- `assets` 分区

理由：

- 用户已明确要求尽量贴近 `xiaozhi-esp32`
- 当前仓库已有 `assets` 分区，可作为运行时资产容器
- 这能把 hand-written 页面和 GUI Guider 生成字体彻底分层

这轮不建议：

- 先放 SPIFFS
- 先放 SD 卡
- 先放源码目录再伪装成资源层

因为这些做法都会偏离用户已确认的方向。

### 3. AI 页面改为依赖字体资源层

第一轮只改：

- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`

改动原则：

- 页面不再直接引用具体字体对象
- 页面只通过 `ui_font_assets_*()` 取字体

例如，当前页面中这类直接引用都应逐步消失：

- `&lv_font_SourceHanSerifSC_Regular_22`
- `&lv_font_montserratMedium_16`
- `&lv_font_montserratMedium_27`

替换为：

- `ui_font_assets_title()`
- `ui_font_assets_body()`
- `ui_font_assets_meta()`

这样后面即使资产字体升级、替换或扩展，页面代码也不需要跟着改。

### 4. GUI Guider 页面保持原状

本轮明确不动：

- `D:\esp32S3\111\main\ui\generated\gui_guider.h`
- `D:\esp32S3\111\main\ui\generated\setup_scr_screen_main.c`
- `D:\esp32S3\111\main\ui\generated\setup_scr_screen_time.c`
- `D:\esp32S3\111\main\ui\generated\setup_scr_screen_wallpaper.c`
- `D:\esp32S3\111\main\ui\generated\guider_fonts\*`

这些文件继续走当前 GUI Guider 生成字体路径。

理由：

- 它们属于生成层
- 修改后续难维护
- 与这轮“只服务 AI 页面和 hand-written 页面”的目标不一致

### 5. 失败回退设计

必须做两级保护：

#### 一级回退：资产字体失败时回退到现有编译字体

建议回退资源：

- 中文标题/正文：
  - `D:\esp32S3\111\main\ui\generated\guider_fonts\lv_font_SourceHanSerifSC_Regular_22.c`
- 小型元信息/纯数字：
  - `D:\esp32S3\111\main\ui\generated\guider_fonts\lv_font_montserratMedium_16.c`

#### 二级保护：若资源层完全不可用，不进入新字体路径

要求：

- 打明确错误日志
- 页面仍可继续初始化
- 不因字体失败导致 AI 页面崩溃或整机不可用

### 6. 资源层与页面边界

资源层负责：

- 找资源
- 加资源
- 选回退
- 给出最终 `lv_font_t*`

页面层负责：

- 取字体
- 赋给控件
- 不关心字体是资产来的还是回退来的

这种边界与 `xiaozhi-esp32` 的方向一致，但不要求当前仓库一次性抄整套显示抽象。

## 验证计划

第一轮验收只看以下 5 件事：

1. 构建通过
   - `idf.py build` 正常

2. 资产字体能被发现
   - 启动日志明确显示：
     - 找到 `assets` 分区
     - 找到字体资源
     - 字体解析成功

3. AI 页面中文正常显示
   - 关键文本：
     - `小智`
     - `待唤醒`
     - `你刚刚说`
     - `小智回答`

4. 人为制造失败后能回退
   - 资源不存在或加载失败时，页面仍能用现有编译字体工作

5. 不影响 GUI Guider 页面
   - 主菜单、时间页、壁纸页不因为新字体链路出问题

## 风险与缓解

### 风险 1：资产字体格式与当前仓库实际可用解析链不完全匹配

缓解：

- 第一轮先限制在 AI 页面
- 必须提供编译字体回退

### 风险 2：一次做太多字号和样式，导致调试复杂度上升

缓解：

- 第一轮只做 `title/body/meta`
- 不一次做完整主题体系

### 风险 3：误碰 GUI Guider 生成层，导致后续重导覆盖

缓解：

- 明确不改 `main/ui/generated`
- 所有新逻辑留在 `main/ui/custom`

### 风险 4：字体资源链成功但页面排版仍不适合中文

缓解：

- 先把字体来源统一
- 再单独做 AI 页面中文布局优化

## 回滚策略

若本轮失败，可按以下顺序回退：

1. AI 页面恢复直接使用当前编译字体
2. 停用 `ui_font_assets`
3. 保留 `assets` 分区不动
4. GUI Guider 页面始终不受影响

这保证了：

- 回滚只影响 hand-written AI 页面
- 不会破坏正式主菜单和已有生成页面

## 下一步建议

规格确认后，实施计划应按这个顺序拆解：

1. 梳理参考仓库字体资源链里真正需要迁的最小文件/概念
2. 在当前仓库中引入 `ui_font_assets`
3. 打通 `assets` 分区字体加载
4. 给 AI 页面切统一字体资源接口
5. 做失败回退测试和真机显示验证
