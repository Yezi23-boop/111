# AI Font Assets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 AI 页面和后续 hand-written 页面建立从 `assets` 分区加载字体资源的最小可运行链路，并在失败时回退到当前仓库编译字体。

**Architecture:** 新增 `ui_font_assets` 作为 hand-written 页面专用字体资源层，优先从 `assets` 分区加载字体，AI 页面只通过该资源层取字体，不直接依赖 GUI Guider 生成字体对象。GUI Guider 页面保持原样，字体资产加载失败时回退到现有 `lv_font_SourceHanSerifSC_Regular_22` / `lv_font_montserratMedium_16`。

**Tech Stack:** ESP-IDF, LVGL 9.2, C, assets 分区, 现有 GUI Guider 字体资源, Python `unittest`

---

## 文件边界

### 新增文件

- `D:\esp32S3\111\main\ui\custom\ui_font_assets.h`
  - hand-written 页面字体资源层对外头文件
- `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`
  - `assets` 分区字体加载、状态管理、回退策略实现
- `D:\esp32S3\111\tests\test_ui_font_assets_source.py`
  - 源码级回归测试，确保字体资源层存在、AI 页面通过它取字体、保留回退路径
- `D:\esp32S3\111\docs\context\knowledge\project\ai-font-assets.md`
  - 记录 AI 页面字体资源层、加载链与回退规则

### 修改文件

- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
  - 去掉对具体字体对象的直接依赖，改为调用 `ui_font_assets`
- `D:\esp32S3\111\main\ui\custom\custom.h`
  - 暴露 `ui_font_assets` 头文件给 hand-written 页面
- `D:\esp32S3\111\main\CMakeLists.txt`
  - 将 `ui_font_assets.c` 编入主组件
- `D:\esp32S3\111\docs\context\CHANGELOG.md`
  - 追加一条变更记录

### 明确不修改

- `D:\esp32S3\111\main\ui\generated\*`
- `D:\esp32S3\111\main\ui\generated\guider_fonts\*`
- `D:\esp32S3\111\main\ui\generated\gui_guider.h`

---

### Task 1: 固化字体资源层接口与源码测试

**Files:**
- Create: `D:\esp32S3\111\tests\test_ui_font_assets_source.py`
- Modify: `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`

- [ ] **Step 1: 写失败测试，约束 AI 页面必须改为走字体资源层**

```python
import pathlib
import unittest


ROOT = pathlib.Path(r"D:\esp32S3\111")
AI_UI = ROOT / "main" / "ui" / "custom" / "ai_ui_controller.c"


class TestUiFontAssetsSource(unittest.TestCase):
    def test_ai_ui_uses_font_assets_api(self):
        text = AI_UI.read_text(encoding="utf-8")
        self.assertIn('#include "ui_font_assets.h"', text)
        self.assertIn("ui_font_assets_title()", text)
        self.assertIn("ui_font_assets_body()", text)
        self.assertIn("ui_font_assets_meta()", text)

    def test_ai_ui_no_longer_directly_binds_generated_fonts(self):
        text = AI_UI.read_text(encoding="utf-8")
        self.assertNotIn("&lv_font_SourceHanSerifSC_Regular_22", text)
        self.assertNotIn("&lv_font_montserratMedium_16", text)
        self.assertNotIn("&lv_font_montserratMedium_27", text)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: 运行测试确认失败**

Run:

```powershell
python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- FAIL，提示找不到 `ui_font_assets.h` 或 `ui_font_assets_*()`
- FAIL，提示 AI 页面仍直接引用具体字体对象

- [ ] **Step 3: 在 AI 页面中预留字体资源层接缝**

目标修改：

- 在 `ai_ui_controller.c` 中加入：

```c
#include "ui_font_assets.h"
```

- 把直接字体对象改为资源层接口：

```c
lv_obj_set_style_text_font(s_title_label, ui_font_assets_title(), 0);
lv_obj_set_style_text_font(s_state_label, ui_font_assets_title(), 0);
lv_obj_set_style_text_font(s_hint_label, ui_font_assets_body(), 0);
lv_obj_set_style_text_font(s_ip_label, ui_font_assets_meta(), 0);
lv_obj_set_style_text_font(s_action_label, ui_font_assets_body(), 0);
lv_obj_set_style_text_font(back_label, ui_font_assets_body(), 0);
```

说明：

- 这一步先只建立调用点
- `ui_font_assets` 实现下一任务补上

- [ ] **Step 4: 重新运行测试，确认接口形态已就位**

Run:

```powershell
python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- 仍可能 FAIL，因为 `ui_font_assets` 文件还不存在
- 但 direct font 引用相关断言应开始接近通过

- [ ] **Step 5: 提交当前小步**

```bash
git add tests/test_ui_font_assets_source.py main/ui/custom/ai_ui_controller.c
git commit -m "测试：约束AI页面改走字体资源层接口"
```

### Task 2: 新增 ui_font_assets 资源层与回退字体

**Files:**
- Create: `D:\esp32S3\111\main\ui\custom\ui_font_assets.h`
- Create: `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`
- Modify: `D:\esp32S3\111\main\CMakeLists.txt`
- Modify: `D:\esp32S3\111\main\ui\custom\custom.h`
- Test: `D:\esp32S3\111\tests\test_ui_font_assets_source.py`

- [ ] **Step 1: 扩展测试，要求资源层提供初始化、就绪态和三类字体接口**

在 `tests/test_ui_font_assets_source.py` 追加：

```python
FONT_H = ROOT / "main" / "ui" / "custom" / "ui_font_assets.h"
FONT_C = ROOT / "main" / "ui" / "custom" / "ui_font_assets.c"


class TestUiFontAssetsFiles(unittest.TestCase):
    def test_header_declares_font_asset_api(self):
        text = FONT_H.read_text(encoding="utf-8")
        self.assertIn("esp_err_t ui_font_assets_init(void);", text)
        self.assertIn("bool ui_font_assets_ready(void);", text)
        self.assertIn("const lv_font_t *ui_font_assets_title(void);", text)
        self.assertIn("const lv_font_t *ui_font_assets_body(void);", text)
        self.assertIn("const lv_font_t *ui_font_assets_meta(void);", text)

    def test_source_contains_assets_and_fallback_paths(self):
        text = FONT_C.read_text(encoding="utf-8")
        self.assertIn("esp_partition_find_first", text)
        self.assertIn("assets", text)
        self.assertIn("lv_font_SourceHanSerifSC_Regular_22", text)
        self.assertIn("lv_font_montserratMedium_16", text)
```

- [ ] **Step 2: 运行测试确认失败**

Run:

```powershell
python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- FAIL，提示 `ui_font_assets.h` / `ui_font_assets.c` 不存在

- [ ] **Step 3: 编写最小头文件**

`D:\esp32S3\111\main\ui\custom\ui_font_assets.h`

```c
#ifndef UI_FONT_ASSETS_H_
#define UI_FONT_ASSETS_H_

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_font_assets_init(void);
bool ui_font_assets_ready(void);

const lv_font_t *ui_font_assets_title(void);
const lv_font_t *ui_font_assets_body(void);
const lv_font_t *ui_font_assets_meta(void);

#ifdef __cplusplus
}
#endif

#endif  // UI_FONT_ASSETS_H_
```

- [ ] **Step 4: 编写最小实现，先把回退路径打通**

`D:\esp32S3\111\main\ui\custom\ui_font_assets.c`

```c
#include "ui_font_assets.h"

#include "esp_log.h"
#include "esp_partition.h"
#include "gui_guider.h"

static const char *TAG = "ui_font_assets";
static bool s_ready = false;

esp_err_t ui_font_assets_init(void) {
    const esp_partition_t *assets =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                 "assets");
    if (assets == NULL) {
        ESP_LOGW(TAG, "assets partition not found, fallback to compiled fonts");
        s_ready = false;
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "assets partition found: size=%" PRIu32, assets->size);
    s_ready = false;
    return ESP_OK;
}

bool ui_font_assets_ready(void) {
    return s_ready;
}

const lv_font_t *ui_font_assets_title(void) {
    return &lv_font_SourceHanSerifSC_Regular_22;
}

const lv_font_t *ui_font_assets_body(void) {
    return &lv_font_SourceHanSerifSC_Regular_22;
}

const lv_font_t *ui_font_assets_meta(void) {
    return &lv_font_montserratMedium_16;
}
```

说明：

- 这一步先把“统一接口 + assets 分区探测 + 回退字体”立住
- 真正字体资产解析留到下一任务

- [ ] **Step 5: 将资源层接入构建与 custom 头文件**

在 `D:\esp32S3\111\main\CMakeLists.txt` 中把 `ui_font_assets.c` 加入 `SRCS`

示例片段：

```cmake
set(srcs
    "111.c"
    "hardware_init.c"
    "network_service.c"
    "official_chat_service.c"
    "ui/custom/ai_ui_controller.c"
    "ui/custom/ui_font_assets.c"
)
```

在 `D:\esp32S3\111\main\ui\custom\custom.h` 中加入：

```c
#include "ui_font_assets.h"
```

- [ ] **Step 6: 运行源码测试确认通过**

Run:

```powershell
python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- PASS

- [ ] **Step 7: 提交当前小步**

```bash
git add main/ui/custom/ui_font_assets.h main/ui/custom/ui_font_assets.c main/CMakeLists.txt main/ui/custom/custom.h tests/test_ui_font_assets_source.py
git commit -m "功能：新增AI页面字体资源层与回退字体"
```

### Task 3: 将 AI 页面改为统一字体资源入口

**Files:**
- Modify: `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
- Test: `D:\esp32S3\111\tests\test_ui_font_assets_source.py`

- [ ] **Step 1: 扩展测试，要求关键中文控件使用 title/body/meta 三类资源字体**

在 `tests/test_ui_font_assets_source.py` 中补充：

```python
    def test_ai_ui_maps_title_body_meta_fonts(self):
        text = AI_UI.read_text(encoding="utf-8")
        self.assertIn("lv_obj_set_style_text_font(s_title_label, ui_font_assets_title()", text)
        self.assertIn("lv_obj_set_style_text_font(s_state_label, ui_font_assets_title()", text)
        self.assertIn("lv_obj_set_style_text_font(s_hint_label, ui_font_assets_body()", text)
        self.assertIn("lv_obj_set_style_text_font(s_action_label, ui_font_assets_body()", text)
        self.assertIn("lv_obj_set_style_text_font(s_ip_label, ui_font_assets_meta()", text)
```

- [ ] **Step 2: 运行测试确认失败**

Run:

```powershell
python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- FAIL，提示 `ai_ui_controller.c` 还未完全切到资源层字体

- [ ] **Step 3: 修改 AI 页面字体映射**

在 `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c` 中确保：

```c
lv_obj_set_style_text_font(s_title_label, ui_font_assets_title(), 0);
lv_obj_set_style_text_font(s_state_label, ui_font_assets_title(), 0);
lv_obj_set_style_text_font(s_hint_label, ui_font_assets_body(), 0);
lv_obj_set_style_text_font(s_ip_label, ui_font_assets_meta(), 0);
lv_obj_set_style_text_font(s_action_label, ui_font_assets_body(), 0);
lv_obj_set_style_text_font(back_label, ui_font_assets_body(), 0);
```

并在页面初始化早期调用：

```c
(void)ui_font_assets_init();
```

调用位置要求：

- 在首次创建 AI 页面控件前
- 不要求初始化失败即中断页面创建

- [ ] **Step 4: 运行源码测试确认通过**

Run:

```powershell
python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- PASS

- [ ] **Step 5: 构建确认**

Run:

```powershell
. \"$env:IDF_PATH\\export.ps1\"; idf.py build
```

Expected:

- BUILD SUCCESSFUL
- 不引入新的编译错误

- [ ] **Step 6: 提交当前小步**

```bash
git add main/ui/custom/ai_ui_controller.c tests/test_ui_font_assets_source.py
git commit -m "功能：AI页面改为统一字体资源入口"
```

### Task 4: 补日志、上下文和验证文档

**Files:**
- Create: `D:\esp32S3\111\docs\context\knowledge\project\ai-font-assets.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] **Step 1: 写项目知识卡**

`D:\esp32S3\111\docs\context\knowledge\project\ai-font-assets.md`

内容要求至少包含：

```md
---
id: ai-font-assets
tags: [project, ui, lvgl, fonts, assets, official-chat]
summary: 记录 AI 页面 hand-written 字体资源层，优先从 assets 分区加载，失败回退到 GUI Guider 编译字体。
last_reviewed: 2026-03-31
---

# AI 页面字体资源层

- 仅服务 `main/ui/custom` 下的 hand-written 页面
- 优先从 `assets` 分区加载字体
- 当前回退到：
  - `lv_font_SourceHanSerifSC_Regular_22`
  - `lv_font_montserratMedium_16`
- GUI Guider 页面保持原有字体链
```

- [ ] **Step 2: 更新 CHANGELOG**

在 `D:\esp32S3\111\docs\context\CHANGELOG.md` 追加一行：

```md
- 2026-03-31: 新增 AI 页面 hand-written 字体资源层，优先探测 assets 分区字体并在失败时回退到现有编译字体。
```

- [ ] **Step 3: 重建上下文索引**

Run:

```powershell
uv run python scripts/context/build_index.py
uv run python scripts/context/check.py
```

Expected:

- `错误: 0，警告: 0`

- [ ] **Step 4: 运行最终回归**

Run:

```powershell
python -m unittest tests.test_ui_font_assets_source -v
. \"$env:IDF_PATH\\export.ps1\"; idf.py build
uv run python scripts/context/build_index.py
uv run python scripts/context/check.py
```

Expected:

- 全部通过

- [ ] **Step 5: 提交当前小步**

```bash
git add docs/context/knowledge/project/ai-font-assets.md docs/context/CHANGELOG.md
git commit -m "文档：记录AI页面字体资源层方案"
```

## 自检

### Spec coverage

- 资产化字体资源层：Task 2
- AI 页面改为统一字体入口：Task 1 + Task 3
- `assets` 分区探测：Task 2
- 回退策略：Task 2
- GUI Guider 页面不动：文件边界已锁定，所有任务均未触碰 generated 层
- 文档与上下文更新：Task 4

无明显漏项。

### Placeholder scan

- 已避免 `TODO / TBD / 后续实现` 这类空步骤
- 每个任务都给了具体文件和命令

### Type consistency

- `ui_font_assets_init`
- `ui_font_assets_ready`
- `ui_font_assets_title`
- `ui_font_assets_body`
- `ui_font_assets_meta`

命名在所有任务中保持一致。
