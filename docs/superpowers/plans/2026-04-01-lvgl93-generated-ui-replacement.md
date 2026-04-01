# LVGL 9.3 Generated UI Replacement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 `C:\Users\ye\Desktop\src` 替换当前仓库的 GUI Guider generated 层和配套 custom 文件，升级 `lvgl/lvgl` 到 `9.3.x`，同时保持主菜单 AI 入口继续跳转到现有 hand-written 聊天气泡页。

**Architecture:** generated 层和 GUI Guider 配套 custom 文件整体替换，hand-written AI/official_chat 桥接层保持不动，只在桥接点做最小补丁。LVGL 版本升级与 UI 替换一起推进，但先确保 generated 层编译通过，再验证 AI 入口桥接和字体链恢复。

**Tech Stack:** ESP-IDF 5.5.3、LVGL 9.3.x、GUI Guider 导出 C 源、ESP32-S3、official_chat、PowerShell、uv。

---

## File Map

- `C:\Users\ye\Desktop\src\generated\**`
  - 新导出的 GUI Guider generated 层源。
- `C:\Users\ye\Desktop\src\custom\custom.c`
  - GUI Guider 配套 custom 实现。
- `C:\Users\ye\Desktop\src\custom\custom.h`
  - GUI Guider 配套 custom 头；替换后需要补回 hand-written AI 桥接 include。
- `C:\Users\ye\Desktop\src\custom\lv_conf_ext.h`
  - GUI Guider 自定义 LVGL 扩展头。
- `C:\Users\ye\Desktop\src\custom\clock_functions.*`
  - GUI Guider 页面时钟辅助。
- `C:\Users\ye\Desktop\src\custom\scroll_functions.*`
  - GUI Guider 页面滚动辅助。
- `D:\esp32S3\111\main\ui\generated\**`
  - 仓库中的 generated 层，整体替换目标。
- `D:\esp32S3\111\main\ui\custom\custom.c`
  - 仓库中的 GUI Guider 配套 custom 实现，替换目标。
- `D:\esp32S3\111\main\ui\custom\custom.h`
  - 仓库中的 GUI Guider 配套 custom 头，替换后需补回 AI 入口桥接 include。
- `D:\esp32S3\111\main\ui\custom\lv_conf_ext.h`
  - 仓库中的 GUI Guider LVGL 扩展头，替换目标。
- `D:\esp32S3\111\main\ui\custom\clock_functions.*`
  - 仓库中的 GUI Guider 时钟辅助，替换目标。
- `D:\esp32S3\111\main\ui\custom\scroll_functions.*`
  - 仓库中的 GUI Guider 滚动辅助，替换目标。
- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
  - hand-written AI 页面控制器，必须保留，且 AI 入口继续通过 `ai_ui_open()` 到这里。
- `D:\esp32S3\111\main\ui\custom\ai_chat_view.c`
  - hand-written 聊天气泡视图，必须保留。
- `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`
  - AI 字体资产链，升级后需要重新验证 `9.3+` 兼容。
- `D:\esp32S3\111\main\lvgl_task.c`
  - LVGL 启动链，升级后需要确认 `setup_ui -> ai_ui_controller_init -> events_init` 顺序仍成立。
- `D:\esp32S3\111\main\idf_component.yml`
  - 当前锁定 `lvgl/lvgl: 9.2.2`，需要升级到 `9.3.x`。
- `D:\esp32S3\111\dependencies.lock`
  - 升级 LVGL 后会变化，用于确认组件版本。
- `D:\esp32S3\111\tests\test_ai_ui_entry_source.py`
  - 需要补充/调整源码级断言，确保 AI 入口桥接仍保留。
- `D:\esp32S3\111\tests\test_ui_font_assets_source.py`
  - 需要调整运行时字体链相关断言以适配 `9.3+`。
- `D:\esp32S3\111\docs\context\knowledge\project\ai-ui-entry-network-guidance.md`
  - 需要补充 UI generated 层替换与 AI 入口桥接现状。
- `D:\esp32S3\111\docs\context\knowledge\project\ai-font-assets.md`
  - 需要补充 LVGL 9.3 升级后字体资产链的状态。
- `D:\esp32S3\111\docs\context\CHANGELOG.md`
  - 记录本轮替换与升级。

### Task 1: 预检新导出源与现有桥接点

**Files:**
- Modify: `D:\esp32S3\111\tests\test_ai_ui_entry_source.py`
- Test: `D:\esp32S3\111\tests\test_ai_ui_entry_source.py`

- [ ] **Step 1: 先补一个会失败的源码级断言，锁定 AI 入口必须继续调用 `ai_ui_open()`**

```python
def test_generated_ai_entry_still_routes_to_handwritten_ai_page(self) -> None:
    source = GENERATED_EVENTS_SOURCE.read_text(encoding="utf-8")
    self.assertIn("ai_ui_open();", source)
```

- [ ] **Step 2: 运行测试，确认在替换前它基线通过**

Run: `uv run python -m unittest tests.test_ai_ui_entry_source -v`
Expected: PASS，说明当前仓库 AI 入口桥接还在，后续替换时能及时发现回归。

- [ ] **Step 3: 记录新导出源与仓库 generated/custom 的映射关系**

```text
generated/** -> main/ui/generated/**
custom/custom.c -> main/ui/custom/custom.c
custom/custom.h -> main/ui/custom/custom.h
custom/lv_conf_ext.h -> main/ui/custom/lv_conf_ext.h
custom/clock_functions.* -> main/ui/custom/clock_functions.*
custom/scroll_functions.* -> main/ui/custom/scroll_functions.*
```

- [ ] **Step 4: 提交预检与测试固化**

```bash
git add tests/test_ai_ui_entry_source.py
git commit -m "测试：固化AI入口桥接约束"
```

### Task 2: 替换 generated 层和 GUI Guider 配套 custom 文件

**Files:**
- Modify: `D:\esp32S3\111\main\ui\generated\**`
- Modify: `D:\esp32S3\111\main\ui\custom\custom.c`
- Modify: `D:\esp32S3\111\main\ui\custom\custom.h`
- Modify: `D:\esp32S3\111\main\ui\custom\lv_conf_ext.h`
- Modify: `D:\esp32S3\111\main\ui\custom\clock_functions.c`
- Modify: `D:\esp32S3\111\main\ui\custom\clock_functions.h`
- Modify: `D:\esp32S3\111\main\ui\custom\scroll_functions.c`
- Modify: `D:\esp32S3\111\main\ui\custom\scroll_functions.h`
- Test: `D:\esp32S3\111\tests\test_ai_ui_entry_source.py`

- [ ] **Step 1: 用桌面导出源覆盖 generated 目录**

Run:

```powershell
Copy-Item -Path C:\Users\ye\Desktop\src\generated\* -Destination D:\esp32S3\111\main\ui\generated -Recurse -Force
```

Expected: `main/ui/generated` 下所有 generated 文件被新导出源替换。

- [ ] **Step 2: 用桌面导出源覆盖 GUI Guider 配套 custom 文件**

Run:

```powershell
Copy-Item C:\Users\ye\Desktop\src\custom\custom.c D:\esp32S3\111\main\ui\custom\custom.c -Force
Copy-Item C:\Users\ye\Desktop\src\custom\custom.h D:\esp32S3\111\main\ui\custom\custom.h -Force
Copy-Item C:\Users\ye\Desktop\src\custom\lv_conf_ext.h D:\esp32S3\111\main\ui\custom\lv_conf_ext.h -Force
Copy-Item C:\Users\ye\Desktop\src\custom\clock_functions.c D:\esp32S3\111\main\ui\custom\clock_functions.c -Force
Copy-Item C:\Users\ye\Desktop\src\custom\clock_functions.h D:\esp32S3\111\main\ui\custom\clock_functions.h -Force
Copy-Item C:\Users\ye\Desktop\src\custom\scroll_functions.c D:\esp32S3\111\main\ui\custom\scroll_functions.c -Force
Copy-Item C:\Users\ye\Desktop\src\custom\scroll_functions.h D:\esp32S3\111\main\ui\custom\scroll_functions.h -Force
```

- [ ] **Step 3: 把 hand-written AI 桥接 include 补回新的 `custom.h`**

```c
#include "gui_guider.h"
#include "ai_ui_controller.h"
#include "ui_font_assets.h"
#include "clock_functions.h"
#include "scroll_functions.h"
```

Expected: 新 `custom.h` 仍能为 hand-written AI 层暴露桥接依赖。

- [ ] **Step 4: 运行 AI 入口源码测试，检查新 generated 层是否仍保留 `ai_ui_open()`**

Run: `uv run python -m unittest tests.test_ai_ui_entry_source -v`
Expected: 若 FAIL，说明新 generated 的 `events_init.c` 已改名或断桥，需要在下一任务修补。

- [ ] **Step 5: 提交 generated/custom 替换**

```bash
git add main/ui/generated main/ui/custom/custom.c main/ui/custom/custom.h main/ui/custom/lv_conf_ext.h main/ui/custom/clock_functions.c main/ui/custom/clock_functions.h main/ui/custom/scroll_functions.c main/ui/custom/scroll_functions.h tests/test_ai_ui_entry_source.py
git commit -m "UI：替换GUI Guider导出层"
```

### Task 3: 修复 AI 入口桥接与 hand-written 页面接缝

**Files:**
- Modify: `D:\esp32S3\111\main\ui\generated\events_init.c`
- Modify: `D:\esp32S3\111\main\ui\generated\events_init.h`
- Modify: `D:\esp32S3\111\main\ui\custom\custom.h`
- Test: `D:\esp32S3\111\tests\test_ai_ui_entry_source.py`

- [ ] **Step 1: 如果新 generated 事件层不再调用 `ai_ui_open()`，先补 failing test 锁定桥接点**

```python
def test_generated_ai_entry_still_routes_to_handwritten_ai_page(self) -> None:
    source = GENERATED_EVENTS_SOURCE.read_text(encoding="utf-8")
    self.assertIn("ai_ui_open();", source)
```

- [ ] **Step 2: 运行测试，确认桥接点当前确实丢失**

Run: `uv run python -m unittest tests.test_ai_ui_entry_source.AiUiEntrySourceTests.test_generated_ai_entry_still_routes_to_handwritten_ai_page -v`
Expected: FAIL，若新 generated 事件层未保留旧桥接。

- [ ] **Step 3: 在新 `events_init.c` 中找到主菜单 AI 图标点击回调，恢复 `ai_ui_open()`**

```c
static void screen_main_option_2_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    switch (code) {
    case LV_EVENT_CLICKED:
    {
        ai_ui_open();
        break;
    }
    default:
        break;
    }
}
```

如果新对象名不是 `screen_main_option_2`，按新 generated 实际对象名修改，但行为必须保持一致。

- [ ] **Step 4: 重新运行 AI 入口源码测试**

Run: `uv run python -m unittest tests.test_ai_ui_entry_source -v`
Expected: PASS，说明主菜单 AI 图标仍然通往 hand-written AI 页面。

- [ ] **Step 5: 提交 AI 桥接修补**

```bash
git add main/ui/generated/events_init.c main/ui/generated/events_init.h main/ui/custom/custom.h tests/test_ai_ui_entry_source.py
git commit -m "UI：恢复AI入口桥接到手写页面"
```

### Task 4: 升级 LVGL 到 9.3 并收敛编译

**Files:**
- Modify: `D:\esp32S3\111\main\idf_component.yml`
- Modify: `D:\esp32S3\111\dependencies.lock`
- Modify: `D:\esp32S3\111\main\lvgl_task.c`
- Modify: `D:\esp32S3\111\components\lvgl_port\**`
- Test: `D:\esp32S3\111\tests\test_ai_ui_entry_source.py`

- [ ] **Step 1: 先修改 `main/idf_component.yml` 中的 LVGL 版本**

```yaml
dependencies:
  idf:
    version: '>=5.5'
  espressif/esp_lcd_co5300: ^2.0.3
  lvgl/lvgl: 9.3.0
```

- [ ] **Step 2: 重新解析依赖并更新锁文件**

Run: `cmd /c "call D:\esp-idf\v5.5.3\esp-idf\export.bat >nul && idf.py reconfigure"`
Expected: `dependencies.lock` 更新，LVGL 版本变为 `9.3.x`。

- [ ] **Step 3: 先做一次完整编译，收集第一轮 API/链接错误**

Run: `cmd /c "call D:\esp-idf\v5.5.3\esp-idf\export.bat >nul && idf.py build"`
Expected: 可能 FAIL；记录首个编译错误文件与符号。

- [ ] **Step 4: 只修首轮构建失败涉及的最小文件**

优先修：

```text
main/lvgl_task.c
components/lvgl_port/**
main/ui/generated/**
main/ui/custom/ai_chat_view.c
```

规则：

- 一次只修一类 API 兼容问题
- 不顺手重构 unrelated 代码
- 修完立刻重新 build

- [ ] **Step 5: 重新编译直到 `idf.py build` 通过**

Run: `cmd /c "call D:\esp-idf\v5.5.3\esp-idf\export.bat >nul && idf.py build"`
Expected: PASS。

- [ ] **Step 6: 提交 LVGL 升级与最小兼容修补**

```bash
git add main/idf_component.yml dependencies.lock main/lvgl_task.c components/lvgl_port main/ui/generated main/ui/custom/ai_chat_view.c
git commit -m "升级：切换LVGL到9.3并修复基础兼容"
```

### Task 5: 验证运行时字体链与 AI 页面中文

**Files:**
- Modify: `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`
- Modify: `D:\esp32S3\111\tests\test_ui_font_assets_source.py`
- Test: `D:\esp32S3\111\tests\test_ui_font_assets_source.py`

- [ ] **Step 1: 先补一个源码级断言，要求 `9.3+` 下不再强制短路回退**

```python
def test_font_assets_source_no_longer_short_circuits_lvgl_93(self) -> None:
    source = FONT_ASSETS_SOURCE.read_text(encoding="utf-8")
    self.assertNotIn("runtime cbin fonts require LVGL >= 9.3.0", source)
```

- [ ] **Step 2: 运行测试，确认当前保护逻辑还在**

Run: `uv run python -m unittest tests.test_ui_font_assets_source -v`
Expected: FAIL，因为当前 `ui_font_assets.c` 还保留 `< 9.3.0` 的短路保护。

- [ ] **Step 3: 在 `ui_font_assets.c` 中移除仅针对 `< 9.3.0` 的短路保护，保留其他回退链**

```c
static bool ui_font_assets_runtime_font_supported(void) {
    return true;
}
```

或者直接删除该判断分支，但必须保留：

```c
if (s_runtime.init_error != ESP_OK) {
    ESP_LOGW(TAG, "font assets fallback to compiled fonts: %s",
             esp_err_to_name(s_runtime.init_error));
    ui_font_assets_reset_runtime();
}
```

- [ ] **Step 4: 重新运行字体源码测试**

Run: `uv run python -m unittest tests.test_ui_font_assets_source -v`
Expected: PASS。

- [ ] **Step 5: 提交字体链切换**

```bash
git add main/ui/custom/ui_font_assets.c tests/test_ui_font_assets_source.py
git commit -m "字体：恢复9.3运行时资产链"
```

### Task 6: 真机回归、文档与上下文更新

**Files:**
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\ai-ui-entry-network-guidance.md`
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\ai-font-assets.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] **Step 1: 完整刷机**

Run: `cmd /c "call D:\esp-idf\v5.5.3\esp-idf\export.bat >nul && idf.py -p COM3 flash"`
Expected: app、`assets`、`model` 一起刷入。

- [ ] **Step 2: 真机检查主菜单和普通页面**

检查项：

```text
屏幕正常点亮
主菜单正常显示
时间页可打开
壁纸页可打开
无明显触摸失效
```

- [ ] **Step 3: 真机检查 AI 入口和 AI 页**

检查项：

```text
主菜单 AI 图标点击后进入 hand-written 聊天气泡页
未进入 AI 页前不自动 foreground
进入 AI 页后 official_chat 才 foreground
AI 页面中文显示正常
聊天区、顶部状态、底部按钮正常
```

- [ ] **Step 4: 更新上下文知识卡和变更记录**

在文档中补充：

```text
generated/custom 替换来源
LVGL 9.3 升级结果
AI 入口桥接是否保留
运行时字体链是否恢复
```

- [ ] **Step 5: 提交文档与上下文**

```bash
git add docs/context/knowledge/project/ai-ui-entry-network-guidance.md docs/context/knowledge/project/ai-font-assets.md docs/context/CHANGELOG.md
git commit -m "文档：补充LVGL9.3与新UI替换上下文"
```

## Self-Review

- Spec coverage:
  - generated/custom 替换：Task 2
  - AI 入口继续跳 hand-written 页面：Task 1 + Task 3
  - LVGL 升级到 9.3：Task 4
  - 运行时字体链恢复验证：Task 5
  - 真机回归与上下文更新：Task 6
- Placeholder scan:
  - 已避免 `TODO/TBD/类似任务N`
  - 每个任务均给出具体文件、命令和关键代码片段
- Type consistency:
  - 统一使用 `ai_ui_open()` 作为 AI 入口桥接
  - 统一使用 `ui_font_assets.c` 作为运行时字体链文件
  - 统一使用 `lvgl/lvgl: 9.3.0` 作为升级目标示例
