# Agent Preview 使用说明

## 目的

`tools/ui_preview` 是当前仓库的 LVGL host 预览面。它用于在 Windows/SDL2 上快速查看 GUI Guider 生成层、`main/ui/custom` 手写页面和全局模拟器遮罩效果。

这个目录只服务 PC 模拟器预览，不是正式板端 UI 入口。

## 构建模拟器

```powershell
& "D:\esp32S3\111\tools\ui_preview\scripts\build_apple_watch_s5_preview.ps1"
```

脚本会：

- 先杀掉已运行的 `agent_preview_host.exe`，避免 exe 被占用导致链接失败。
- 注入 `D:\MSYS2\mingw64\bin` 和 `D:\MSYS2\usr\bin` 到 `PATH`。
- 用 MinGW CMake 构建 `host_runner/build/agent_preview_host.exe`。

## 截图模拟器

```powershell
& "D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1"
```

默认输出：

```text
D:\esp32S3\111\tools\ui_preview\artifacts\wifi-management-image-to-code.png
```

脚本会：

- 确认 exe 存在；不存在时自动调用构建脚本。
- 调用 `agent_preview_host.exe --capture <png>` 生成 PNG。
- 截图完成后重新拉起一个独立常驻的模拟器窗口，方便人工交互查看。

打开 Hermes 页面截图：

```powershell
& "D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1" -OpenHermes
```

打开 Hermes 收件箱或消息详情截图：

```powershell
& "D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1" -OpenHermesInbox
& "D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1" -OpenHermesDetail
```

打开小智 AI 页面截图：

```powershell
& "D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1" -OpenAi
```

打开危险识别页面截图：

```powershell
& "D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1" -OpenDanger
```

自定义输出路径：

```powershell
& "D:\esp32S3\111\tools\ui_preview\scripts\capture_apple_watch_s5_preview.ps1" `
  -OutputPath "D:\esp32S3\111\tools\ui_preview\artifacts\hermes.png" `
  -OpenHermes
```

## 当前截图机制

当前截图不再使用 PowerShell 内嵌 C#、`EnumWindows`、`SetForegroundWindow` 或 `PrintWindow`。

原因：SDL2 窗口在当前 Windows host 下可能出现进程存在但拿不到可靠顶层 HWND 的情况，导致旧脚本卡在 “Preview window did not appear before timeout”。另外 `System.Drawing` 在当前 PowerShell/.NET 环境中也容易出现程序集兼容问题。

现在的稳定路径是：

1. `capture_apple_watch_s5_preview.ps1` 启动 `agent_preview_host.exe --capture <png>`。
2. host runner 创建 LVGL SDL 显示并跑若干帧，确保首屏、字体、遮罩和动画都完成初始渲染。
3. host runner 通过 `lv_sdl_window_get_renderer()` 拿到 SDL renderer。
4. host runner 调用 `SDL_RenderReadPixels()` 直接读回 410x502 像素。
5. host runner 用 `lodepng_encode32()` 编码 PNG，再用 C 标准库 `fopen/fwrite` 写入 Windows 文件。

注意：不要改回 `lodepng_encode32_file()`。LVGL 集成版 lodepng 的 file helper 走 `lv_fs_open()`，host runner 没有挂 PC 文件系统驱动，会报 `failed to open file for writing`。

注意：`lodepng_encode32()` 返回的 buffer 由 LVGL allocator 分配，释放时必须用 `lv_free()`，不能用 `free()`。

## 可点击原型范围

当前 host runner 是多页面可点击原型，不只是单屏截图：

- 主界面保留真实左滑进入功能页、右滑返回主表盘的手势。
- 功能页中的小智 AI 和 Hermes 入口仍走正式 controller 入口。
- `--open-ai` 直接进入小智页；按住/松开语音按钮会驱动 host mock 状态并追加一轮模拟对话。
- `--open-hermes` 直接进入 Hermes 语音页；左滑或点“收件箱”进入收件箱。
- `--open-hermes-inbox` 和 `--open-hermes-detail` 用于直接截取收件箱列表和详情页。
- 全局通知气泡在 host mock 中默认不弹出，避免遮挡多页面 UI 评审。

## 常见问题

### 构建时报 exe 被占用

先关掉桌面上的模拟器窗口，或运行：

```powershell
Get-Process -Name "agent_preview_host" -ErrorAction SilentlyContinue | Stop-Process -Force
```

构建脚本本身也会做这一步。

### 截图是黑屏或首屏没渲染完整

不要在脚本里加 `Start-Sleep` 等待窗口。现在等待逻辑在 host runner 内部，通过固定帧数推进 LVGL tick 和 timer。优先调整 `tools/ui_preview/host_runner/main.c` 中 `--capture` 分支的帧数。

### 截图脚本报找不到 SDL2.dll

确认脚本没有删掉这行 PATH 注入：

```powershell
$env:PATH = "D:\MSYS2\mingw64\bin;D:\MSYS2\usr\bin;" + $env:PATH
```

构建后 CMake 也会把 `SDL2.dll` 复制到 exe 目录。

### 后续 agent 应该怎么交付 UI 预览

如果本轮改了 UI 或 host preview：

1. 跑构建脚本。
2. 跑截图脚本。
3. 在最终回复里附上绝对路径图片：

```markdown
![预览图](D:/esp32S3/111/tools/ui_preview/artifacts/wifi-management-image-to-code.png)
```

不要只说“模拟器已启动”。截图才是可复查的交付证据。
