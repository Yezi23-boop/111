# AI 字体资产链与 AI 页面重构 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 迁入 `xiaozhi-esp32` 的字体资产格式与 `index.json` 约定，打通 `assets` 分区运行时字体链，并同步重构正式 AI 页与独立实验页为统一的 AI-first 中文布局。

**Architecture:** 继续保留 `ui_font_assets` 作为 hand-written 页面统一字体入口，但将其底层升级为 `assets` 分区 mmap + `index.json` + `cbin_font` 运行时解析链。`official_chat_service` 补最近一轮用户/助手文本缓存，`ai_ui_controller` 与 `ai_experiment_ui` 通过共用的 hand-written 布局 helper 渲染同一套“静态 AI 图标卡片 + 最近一轮对话”结构。

**Tech Stack:** ESP-IDF 5.5, LVGL 9.2, C/C++, `esp_partition_mmap`, `cJSON`, `official_chat_service`, PowerShell, Python `unittest`

---

## 文件边界

### 新增文件

- `D:\esp32S3\111\main\ui\custom\ai_conversation_view.h`
  - hand-written AI 页面共用布局与控件句柄定义
- `D:\esp32S3\111\main\ui\custom\ai_conversation_view.c`
  - 静态 AI 图标卡片、状态栏、最近一轮对话区和按钮区构建逻辑
- `D:\esp32S3\111\main\ui\custom\cbin_font_bridge.h`
  - 对接 `xiaozhi-esp32` 的 `cbin_font` 最小桥接头
- `D:\esp32S3\111\main\ui\custom\cbin_font_bridge.c`
  - 运行时字体创建/释放最小桥接实现
- `D:\esp32S3\111\assets\ai-fonts\index.json`
  - 当前仓库用于生成 `assets` 分区字体资源的索引
- `D:\esp32S3\111\assets\ai-fonts\README.md`
  - 记录字体资源来源、格式与更新方式
- `D:\esp32S3\111\tests\test_ai_conversation_view_source.py`
  - 源码级验证两套 AI 页共用布局 helper 和最近一轮对话区
- `D:\esp32S3\111\tests\test_official_chat_service_chat_memory_source.py`
  - 源码级验证最近一轮对话缓存接口

### 修改文件

- `D:\esp32S3\111\main\ui\custom\ui_font_assets.h`
- `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`
- `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
- `D:\esp32S3\111\main\ai_experiment_ui.c`
- `D:\esp32S3\111\main\official_chat_service.h`
- `D:\esp32S3\111\main\official_chat_service.c`
- `D:\esp32S3\111\main\ui\custom\custom.h`
- `D:\esp32S3\111\main\CMakeLists.txt`
- `D:\esp32S3\111\tests\test_ui_font_assets_source.py`
- `D:\esp32S3\111\docs\context\knowledge\project\ai-font-assets.md`
- `D:\esp32S3\111\docs\context\CHANGELOG.md`

### 明确不修改

- `D:\esp32S3\111\main\ui\generated\*`
- `D:\esp32S3\111\main\ui\generated\guider_fonts\*`
- `D:\esp32S3\111\main\ui\generated\gui_guider.h`

---

### Task 1: 打通 `xiaozhi-esp32` 风格字体资产读取链

**Files:**
- Create: `D:\esp32S3\111\main\ui\custom\cbin_font_bridge.h`
- Create: `D:\esp32S3\111\main\ui\custom\cbin_font_bridge.c`
- Modify: `D:\esp32S3\111\main\ui\custom\ui_font_assets.h`
- Modify: `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`
- Modify: `D:\esp32S3\111\main\CMakeLists.txt`
- Test: `D:\esp32S3\111\tests\test_ui_font_assets_source.py`

- [ ] **Step 1: 先扩展失败测试，约束 `ui_font_assets` 不再停留在探测占位态**

```python
def test_font_assets_source_uses_partition_mmap_and_index_json(self) -> None:
    source = FONT_ASSETS_SOURCE.read_text(encoding="utf-8")

    self.assertIn("esp_partition_mmap", source)
    self.assertIn("index.json", source)
    self.assertIn("text_font", source)
    self.assertIn("icon_font", source)

def test_font_assets_source_no_longer_returns_not_supported(self) -> None:
    source = FONT_ASSETS_SOURCE.read_text(encoding="utf-8")

    self.assertNotIn("return ESP_ERR_NOT_SUPPORTED;", source)
    self.assertNotIn("return false;", source)
    self.assertIn("cbin_font_create", source)
```

- [ ] **Step 2: 跑测试，确认当前实现确实失败**

Run:

```powershell
uv run python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- FAIL，提示缺少 `esp_partition_mmap` / `index.json` / `cbin_font_create`
- FAIL，提示仍存在 `ESP_ERR_NOT_SUPPORTED` 或占位返回

- [ ] **Step 3: 新增最小 `cbin_font` 桥接层**

`D:\esp32S3\111\main\ui\custom\cbin_font_bridge.h`

```c
#ifndef CBIN_FONT_BRIDGE_H_
#define CBIN_FONT_BRIDGE_H_

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_font_t *cbin_font_bridge_create(void *data);
void cbin_font_bridge_destroy(lv_font_t *font);

#ifdef __cplusplus
}
#endif

#endif  // CBIN_FONT_BRIDGE_H_
```

`D:\esp32S3\111\main\ui\custom\cbin_font_bridge.c`

```c
#include "cbin_font_bridge.h"

#include "cbin_font.h"

lv_font_t *cbin_font_bridge_create(void *data) {
    return cbin_font_create((uint8_t *)data);
}

void cbin_font_bridge_destroy(lv_font_t *font) {
    if (font != NULL) {
        cbin_font_delete(font);
    }
}
```

- [ ] **Step 4: 把 `ui_font_assets` 升级为 mmap + `index.json` + 运行时字体缓存**

核心实现方向：

```c
typedef struct {
    bool init_done;
    bool ready;
    const esp_partition_t *partition;
    spi_flash_mmap_handle_t mmap_handle;
    const uint8_t *mmap_root;
    lv_font_t *title_font;
    lv_font_t *body_font;
    lv_font_t *meta_font;
    lv_font_t *icon_font;
} ui_font_assets_runtime_t;
```

并落实这些调用：

```c
esp_partition_find_first(..., "assets");
esp_partition_mmap(...);
cJSON_ParseWithLength(...);
cJSON_GetObjectItem(root, "text_font");
cJSON_GetObjectItem(root, "icon_font");
cbin_font_bridge_create(...);
```

要求：

- `ui_font_assets_init()` 在字体真正解析成功时返回 `ESP_OK`
- `ui_font_assets_ready()` 只有在字体对象可用时返回 `true`
- 失败时打印明确日志并回退到编译字体

- [ ] **Step 5: 在 `main/CMakeLists.txt` 中编入新桥接源文件**

```cmake
list(APPEND main_common_srcs
    ui/custom/ui_font_assets.c
    ui/custom/cbin_font_bridge.c
)
```

- [ ] **Step 6: 重新运行测试，确认字体资源层源码约束通过**

Run:

```powershell
uv run python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- PASS
- `ui_font_assets` 不再是占位实现

- [ ] **Step 7: 提交这一小步**

```bash
git add main/ui/custom/cbin_font_bridge.h main/ui/custom/cbin_font_bridge.c main/ui/custom/ui_font_assets.h main/ui/custom/ui_font_assets.c main/CMakeLists.txt tests/test_ui_font_assets_source.py
git commit -m "功能：迁入AI字体资产读取链"
```

### Task 2: 补当前仓库最小字体资产打包入口

**Files:**
- Create: `D:\esp32S3\111\assets\ai-fonts\index.json`
- Create: `D:\esp32S3\111\assets\ai-fonts\README.md`
- Modify: `D:\esp32S3\111\main\ui\custom\ui_font_assets.c`
- Test: `D:\esp32S3\111\tests\test_ui_font_assets_source.py`

- [ ] **Step 1: 写失败测试，约束仓库内存在 `xiaozhi-esp32` 风格字体索引**

```python
ASSET_INDEX = REPO_ROOT / "assets" / "ai-fonts" / "index.json"

def test_ai_font_asset_index_exists_and_declares_fonts(self) -> None:
    text = ASSET_INDEX.read_text(encoding="utf-8")
    self.assertIn('"version"', text)
    self.assertIn('"text_font"', text)
    self.assertIn('"icon_font"', text)
```

- [ ] **Step 2: 跑测试，确认索引文件当前不存在**

Run:

```powershell
uv run python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- FAIL，提示 `assets/ai-fonts/index.json` 不存在

- [ ] **Step 3: 创建仓库内最小字体索引**

`D:\esp32S3\111\assets\ai-fonts\index.json`

```json
{
  "version": 1,
  "text_font": "font_puhui_common_20_4.bin",
  "icon_font": "font_awesome_20_4.bin"
}
```

- [ ] **Step 4: 补充说明文档，写清资源来源和当前约束**

`D:\esp32S3\111\assets\ai-fonts\README.md`

```md
# AI Fonts Assets

- 本目录用于生成与 `xiaozhi-esp32` 兼容的 AI 页面字体资产。
- `index.json` 约定了 `text_font` 与 `icon_font` 资源键。
- 第一版只保证 AI 页面和 `ai_experiment_ui` 使用这套字体资产。
```

- [ ] **Step 5: 让 `ui_font_assets` 在日志中打印正在读取的字体键**

```c
ESP_LOGI(TAG, "loading text font asset: %s", text_font_name);
ESP_LOGI(TAG, "loading icon font asset: %s", icon_font_name);
```

- [ ] **Step 6: 重新运行测试，确认仓库资产入口已落地**

Run:

```powershell
uv run python -m unittest tests.test_ui_font_assets_source -v
```

Expected:

- PASS
- 资产索引文件相关断言通过

- [ ] **Step 7: 提交这一小步**

```bash
git add assets/ai-fonts/index.json assets/ai-fonts/README.md main/ui/custom/ui_font_assets.c tests/test_ui_font_assets_source.py
git commit -m "功能：补充AI字体资产索引入口"
```

### Task 3: 给 `official_chat_service` 增加最近一轮对话缓存

**Files:**
- Modify: `D:\esp32S3\111\main\official_chat_service.h`
- Modify: `D:\esp32S3\111\main\official_chat_service.c`
- Create: `D:\esp32S3\111\tests\test_official_chat_service_chat_memory_source.py`

- [ ] **Step 1: 写失败测试，约束服务层暴露最近一句用户话和助手话**

```python
def test_official_chat_service_exposes_recent_chat_getters(self) -> None:
    header = SERVICE_HEADER.read_text(encoding="utf-8")
    self.assertIn("const char *official_chat_service_last_user_text(void);", header)
    self.assertIn("const char *official_chat_service_last_assistant_text(void);", header)
```

```python
def test_official_chat_service_caches_stt_and_assistant_text(self) -> None:
    source = SERVICE_SOURCE.read_text(encoding="utf-8")
    self.assertIn("stt text", source)
    self.assertIn("assistant text", source)
    self.assertIn("s_last_user_text", source)
    self.assertIn("s_last_assistant_text", source)
```

- [ ] **Step 2: 跑测试，确认接口当前不存在**

Run:

```powershell
uv run python -m unittest tests.test_official_chat_service_chat_memory_source -v
```

Expected:

- FAIL，提示 getter 或缓存字段不存在

- [ ] **Step 3: 在服务层增加缓存字段与只读 getter**

`D:\esp32S3\111\main\official_chat_service.h`

```c
const char *official_chat_service_last_user_text(void);
const char *official_chat_service_last_assistant_text(void);
```

`D:\esp32S3\111\main\official_chat_service.c`

```c
static char s_last_user_text[128] = {0};
static char s_last_assistant_text[192] = {0};
```

并在事件处理处更新：

```c
static void official_chat_service_update_last_user_text(const char *text);
static void official_chat_service_update_last_assistant_text(const char *text);
```

- [ ] **Step 4: 重新运行测试，确认缓存接口通过**

Run:

```powershell
uv run python -m unittest tests.test_official_chat_service_chat_memory_source -v
```

Expected:

- PASS

- [ ] **Step 5: 提交这一小步**

```bash
git add main/official_chat_service.h main/official_chat_service.c tests/test_official_chat_service_chat_memory_source.py
git commit -m "功能：缓存AI最近一轮对话文本"
```

### Task 4: 抽共用 AI 布局 helper 并重构正式 AI 页面

**Files:**
- Create: `D:\esp32S3\111\main\ui\custom\ai_conversation_view.h`
- Create: `D:\esp32S3\111\main\ui\custom\ai_conversation_view.c`
- Modify: `D:\esp32S3\111\main\ui\custom\ai_ui_controller.c`
- Modify: `D:\esp32S3\111\main\ui\custom\custom.h`
- Modify: `D:\esp32S3\111\main\CMakeLists.txt`
- Create: `D:\esp32S3\111\tests\test_ai_conversation_view_source.py`

- [ ] **Step 1: 写失败测试，约束正式 AI 页改用共用布局 helper**

```python
def test_ai_ui_controller_uses_conversation_view(self) -> None:
    source = AI_UI_SOURCE.read_text(encoding="utf-8")
    self.assertIn('#include "ai_conversation_view.h"', source)
    self.assertIn("ai_conversation_view_create", source)
    self.assertIn("official_chat_service_last_user_text()", source)
    self.assertIn("official_chat_service_last_assistant_text()", source)
```

```python
def test_conversation_view_contains_recent_chat_sections(self) -> None:
    source = VIEW_SOURCE.read_text(encoding="utf-8")
    self.assertIn("你刚刚说", source)
    self.assertIn("小智回答", source)
    self.assertIn("lv_image_create", source)
```

- [ ] **Step 2: 跑测试，确认当前正式 AI 页尚未接共用布局**

Run:

```powershell
uv run python -m unittest tests.test_ai_conversation_view_source -v
```

Expected:

- FAIL

- [ ] **Step 3: 新增共用布局 helper**

`D:\esp32S3\111\main\ui\custom\ai_conversation_view.h`

```c
typedef struct {
    lv_obj_t *root;
    lv_obj_t *network_label;
    lv_obj_t *state_label;
    lv_obj_t *hint_label;
    lv_obj_t *user_text_label;
    lv_obj_t *assistant_text_label;
    lv_obj_t *action_btn;
    lv_obj_t *action_label;
    lv_obj_t *back_btn;
} ai_conversation_view_t;

void ai_conversation_view_create(ai_conversation_view_t *view, lv_obj_t *screen,
                                 bool show_back_button);
```

- [ ] **Step 4: 在 helper 里实现 `B + 静态 AI 图标卡片` 布局**

核心布局要求：

```c
// 顶部：小智 + 网络状态
// 中部：AI 图标卡片 + 大状态字 + 提示
// 下部：最近一句用户话 / 助手话
// 底部：操作按钮 + 可选返回按钮
```

要求：

- 中文统一走 `ui_font_assets`
- 主视觉优先复用现有 AI 图标资源
- “你刚刚说 / 小智回答”固定存在

- [ ] **Step 5: 把 `ai_ui_controller.c` 改为驱动共用 view，而不是自己手拼控件**

```c
static ai_conversation_view_t s_view;

ai_conversation_view_create(&s_view, s_ai_screen, true);
lv_label_set_text(s_view.user_text_label, official_chat_service_last_user_text());
lv_label_set_text(s_view.assistant_text_label, official_chat_service_last_assistant_text());
```

- [ ] **Step 6: 重新运行源码测试**

Run:

```powershell
uv run python -m unittest tests.test_ai_conversation_view_source tests.test_ui_font_assets_source tests.test_official_chat_service_chat_memory_source -v
```

Expected:

- PASS

- [ ] **Step 7: 提交这一小步**

```bash
git add main/ui/custom/ai_conversation_view.h main/ui/custom/ai_conversation_view.c main/ui/custom/ai_ui_controller.c main/ui/custom/custom.h main/CMakeLists.txt tests/test_ai_conversation_view_source.py
git commit -m "功能：重构正式AI页面布局"
```

### Task 5: 同步独立实验页并补上下文

**Files:**
- Modify: `D:\esp32S3\111\main\ai_experiment_ui.c`
- Modify: `D:\esp32S3\111\docs\context\knowledge\project\ai-font-assets.md`
- Modify: `D:\esp32S3\111\docs\context\CHANGELOG.md`

- [ ] **Step 1: 写失败测试，约束实验页也走共用 view 与字体资源层**

在 `tests/test_ai_conversation_view_source.py` 追加：

```python
def test_ai_experiment_ui_uses_conversation_view_and_font_assets(self) -> None:
    source = AI_EXPERIMENT_SOURCE.read_text(encoding="utf-8")
    self.assertIn('#include "ai_conversation_view.h"', source)
    self.assertIn("ui_font_assets_init()", source)
    self.assertIn("official_chat_service_last_user_text()", source)
    self.assertIn("official_chat_service_last_assistant_text()", source)
```

- [ ] **Step 2: 跑测试，确认实验页当前仍是旧布局**

Run:

```powershell
uv run python -m unittest tests.test_ai_conversation_view_source -v
```

Expected:

- FAIL

- [ ] **Step 3: 把 `ai_experiment_ui.c` 改为复用共用布局 helper**

要求：

- 调用 `ui_font_assets_init()`
- 调用 `ai_conversation_view_create(..., false)`
- 保留自动 `enter_foreground` 行为
- 同步显示最近一句用户话 / 助手话

- [ ] **Step 4: 更新上下文文档**

`D:\esp32S3\111\docs\context\knowledge\project\ai-font-assets.md`

追加已验证事实：

```md
- 当前 `ui_font_assets` 已改为按 `xiaozhi-esp32` 的资源表与 `index.json` 读取字体资产。
- `ai_ui_controller` 与 `ai_experiment_ui` 都已切到统一的 hand-written AI-first 布局。
```

`D:\esp32S3\111\docs\context\CHANGELOG.md`

追加一行：

```md
- 2026-04-01：迁入 `xiaozhi-esp32` 风格 AI 字体资产读取链，并把正式 AI 页与独立实验页统一重构为静态 AI 图标卡片布局。
```

- [ ] **Step 5: 跑最终验证**

Run:

```powershell
uv run python -m unittest tests.test_ui_font_assets_source tests.test_official_chat_service_chat_memory_source tests.test_ai_conversation_view_source -v
```

Expected:

- 全部 PASS

Run:

```powershell
. "$env:IDF_PATH\export.ps1"; idf.py build
```

Expected:

- Build succeeds

Run:

```powershell
uv run python scripts/context/build_index.py
uv run python scripts/context/check.py
```

Expected:

- `错误: 0，警告: 0`

- [ ] **Step 6: 提交这一小步**

```bash
git add main/ai_experiment_ui.c docs/context/knowledge/project/ai-font-assets.md docs/context/CHANGELOG.md context/index/context-index.json
git commit -m "功能：统一AI实验页与字体资产链"
```

## Self-Review

- Spec coverage:
  - 字体资产格式与 `index.json` 迁入：Task 1 + Task 2
  - `official_chat_service` 最近一轮文本缓存：Task 3
  - 正式 AI 页改成 `B + 静态 AI 图标卡片`：Task 4
  - `ai_experiment_ui` 一起切过去：Task 5
  - 文档与上下文同步：Task 5
- Placeholder scan:
  - 无 `TODO/TBD/稍后实现` 占位词
- Type consistency:
  - `ui_font_assets_*()`、`official_chat_service_last_*()`、`ai_conversation_view_create()` 在各任务中命名一致
