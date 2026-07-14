---
id: gui-guider-lvgl-host-preview-workflow
tags: project, ui, lvgl, gui-guider, preview, pc-sim, host-runner, skill
summary: 记录 gui-guider-lvgl-preview 的稳定定位：先用 host/pc_sim 预览 LVGL 页面、卡片、弹层和小功能，让用户判断 UI 是否合格，再决定是否接入板端。
last_reviewed: 2026-07-14
memory_type: framework
scope: repo
owners: tools/ui_preview, main/ui/generated, main/ui/custom
triggers: gui-guider-lvgl-preview, agent画ui, agent_preview, pc_sim, host preview, LVGL预览, UI草图, 子页面, 卡片区, 弹层, 状态区, 设置区
evidence_level: observed
status: active
route_area: "GUI Guider / LVGL preview"
---

# GUI Guider / LVGL Host Preview 工作流

## 定位

`gui-guider-lvgl-preview` 不是“Agent 自动完成整套板端 UI”的能力，而是“Agent 把 UI 想法做成可预览的 LVGL 草图和局部实现，让用户先判断再接入”。

适合场景：

- 在 GUI Guider 已有框架旁边补一个独立子页面。
- 为已有入口按钮补页面主体、卡片区、弹层、状态区、设置区或小功能 UI。
- 把用户的文字想法、截图参考或最小代码锚点转成 LVGL C，并生成 host 预览截图。
- 让用户先判断布局、层级、文案、样式和基础交互是否合格。

不适合承诺：

- 不承诺 host 预览通过就等于板端不会花屏、错位或卡顿。
- 不默认直接改正式 `main/CMakeLists.txt`、`sdkconfig`、`lvgl_task.c` 或 GUI Guider 生成文件。
- 不把预览工程当正式固件入口。

## 预览与板端边界

host / `pc_sim` 预览用于快速判断：

- 布局、尺寸、层级、圆角、间距和视觉方向。
- 文案长度、基础状态展示和简单交互手感。
- Agent 生成的 LVGL 对象树是否大体符合预期。

板端验证才裁决：

- CO5300 显示传输、byte swap、DMA/PSRAM/cache 影响。
- FT5x06 触摸坐标映射和输入体验。
- LVGL screen 生命周期、删除动画、cached object 悬空风险。
- 字体资源、中文 glyph、flash/PSRAM 占用和运行时性能。

## 当前仓库默认落点

当前仓库旧 `pc_sim/` 已被清理；如果没有现成 preview runner，默认在隔离目录创建最小 host runner：

- 页面主体：`tools/ui_preview/pages/`
- 最小 runner：`tools/ui_preview/host_runner/`
- 构建/截图脚本：`tools/ui_preview/scripts/`
- 截图产物：`tools/ui_preview/artifacts/`

构建产物和截图默认不要进入版本控制：

- `tools/ui_preview/host_runner/build*/`
- `tools/ui_preview/artifacts/*.png`

## 代码组织约定

预览页面可以是手写 LVGL C，但入口最好兼容 GUI Guider generated 风格：

- 内部用显式对象创建，少封装，方便肉眼审查。
- 提供 `setup_scr_xxx(lv_obj_t **screen)` 风格入口，便于后续理解和接回 GUI Guider 体系。
- 页面/区域实现不要包含硬件驱动调用，不直接启动 Wi-Fi/BLE/音频等 owner。
- 接入正式工程时，优先从 `events_init/controller/custom` 层挂接，不直接大改 `generated/setup_scr_*.c`。

## Windows / MSYS2 经验

当前机器的最小 host runner 应避免依赖隐式环境：

- PowerShell 脚本里显式把 `D:\MSYS2\mingw64\bin` 和 `D:\MSYS2\usr\bin` 放入 `PATH`。
- CMake 显式指定 `D:/MSYS2/mingw64/bin/gcc.exe`，避免默认 `cc.exe` 试编译失败。
- 链接 SDL2 后把 `SDL2.dll` 复制到 exe 所在目录。
- LVGL host runner 默认关闭不需要的 ThorVG internal/external，减少 MinGW C++ 子库构建问题。
- 如需包含 LVGL SDL driver 头文件，runner target 需要能 include 到 LVGL `src`。

## 当前截图脚本机制

当前仓库的稳定截图入口是：

```powershell
& "D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1"
```

Hermes 首屏截图可加 `-OpenHermes`。脚本默认输出：

```text
D:\esp32S3\111\tools\ui_preview\artifacts\wifi-management-image-to-code.png
```

不要把截图链路改回 `PowerShell + C# + EnumWindows + PrintWindow`。该路线在当前 Windows/SDL2 host 下曾出现 `agent_preview_host.exe` 进程存在但拿不到可靠顶层 HWND，导致脚本超时；同时 `System.Drawing` 在 PowerShell/.NET 组合下也有程序集兼容风险。

当前稳定路线是 host 内部截图：

- PowerShell 调用 `agent_preview_host.exe --capture <png>`。
- host runner 跑若干帧完成 LVGL 首屏渲染。
- host runner 通过 `lv_sdl_window_get_renderer()` 获取 SDL renderer。
- host runner 用 `SDL_RenderReadPixels()` 读回像素。
- host runner 用 `lodepng_encode32()` 编码 PNG，再用 C 标准库 `fopen/fwrite` 写入文件。

不要使用 `lodepng_encode32_file()` 写 Windows 文件。LVGL 集成版 lodepng 的 file helper 走 `lv_fs_open()`，host runner 默认没有挂 PC filesystem driver，会返回 `failed to open file for writing`。`lodepng_encode32()` 返回的 buffer 由 LVGL allocator 分配，释放必须用 `lv_free()`。

就近操作说明见 `tools/ui_preview/README.md`。

## 中文字体边界

英文/数字预览可先用 Montserrat 快速验证布局。

只要 UI 文案包含中文，就必须按 `lvgl-chinese-ui-binfonts.md` 和 `lvgl-chinese-ui-fonts` skill 的规则处理：

- 中文 label 不要绑定 `lv_font_montserrat*`。
- 默认引用 `ui_chinese_fonts.h` 声明的编译期中文字体符号。
- 生成或修改后运行中文字体防呆扫描。

## 推荐交付证据

一次合格的 Agent UI 预览交付至少包含：

- LVGL 页面或区域代码。
- 可重复执行的 build/run/capture 命令。
- 截图文件路径或内联预览图。
- 简短说明：哪些可以由 host 预览确认，哪些仍需板端验证。
